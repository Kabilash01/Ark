#include "ark/document_agent.hpp"

#include <cairo-pdf.h>
#include <libxml/parser.h>
#include <pango/pangocairo.h>
#include <poppler-document.h>
#include <poppler-page.h>
#include <zip.h>

#include <filesystem>
#include <map>
#include <memory>

namespace ark {
namespace {

using Path = std::filesystem::path;
using Parts = std::map<std::string, std::string>;
constexpr auto member_limit = 8 * 1024 * 1024;
constexpr auto word_ns = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
constexpr auto sheet_ns = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr auto rel_ns = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr auto package_rel_ns = "http://schemas.openxmlformats.org/package/2006/relationships";
constexpr auto content_ns = "http://schemas.openxmlformats.org/package/2006/content-types";

std::string escape(const std::string& text) {
    if (text.find('\0') != std::string::npos || !xmlCheckUTF8(BAD_CAST text.c_str())) {
        throw std::invalid_argument("Document text must be valid UTF-8");
    }
    for (unsigned char c : text) {
        if (c < 32 && c != '\t' && c != '\n' && c != '\r') {
            throw std::invalid_argument("Document text contains an unsupported control character");
        }
    }
    auto* escaped = xmlEncodeSpecialChars(nullptr, BAD_CAST text.c_str());
    if (!escaped) throw std::runtime_error("Could not escape document text");
    const std::unique_ptr<xmlChar, decltype(xmlFree)> value(escaped, xmlFree);
    return reinterpret_cast<const char*>(escaped);
}

std::string member(zip_t* archive, const char* name, bool required = true) {
    zip_stat_t stat{};
    if (zip_stat(archive, name, ZIP_FL_ENC_GUESS, &stat) < 0) {
        if (!required && zip_error_code_zip(zip_get_error(archive)) == ZIP_ER_NOENT) return {};
        throw std::runtime_error(std::string("Missing archive member ") + name);
    }
    if (stat.size > member_limit) throw std::runtime_error("Document archive member exceeds 8 MiB");
    const std::unique_ptr<zip_file_t, decltype(&zip_fclose)> input(zip_fopen(archive, name, 0), zip_fclose);
    if (!input) throw std::runtime_error("Could not open document archive member");
    std::string data(stat.size, '\0');
    std::size_t read = 0;
    while (read < data.size()) {
        const auto count = zip_fread(input.get(), data.data() + read, data.size() - read);
        if (count <= 0) throw std::runtime_error("Could not read complete document archive member");
        read += static_cast<std::size_t>(count);
    }
    return data;
}

auto parse_xml(const std::string& source) {
    std::unique_ptr<xmlDoc, decltype(&xmlFreeDoc)> document(
        xmlReadMemory(source.data(), static_cast<int>(source.size()), nullptr, nullptr,
                      XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING), xmlFreeDoc);
    if (!document || document->intSubset || document->extSubset) throw std::runtime_error("Invalid document XML");
    return document;
}

bool element(xmlNode* node, const char* name, const char* ns) {
    return node->type == XML_ELEMENT_NODE && node->ns && xmlStrEqual(node->name, BAD_CAST name)
        && xmlStrEqual(node->ns->href, BAD_CAST ns);
}

void children(xmlNode* parent, const char* name, const char* ns, const auto& fn) {
    for (auto* node = parent ? parent->children : nullptr; node; node = node->next) {
        if (element(node, name, ns)) fn(node);
    }
}

void descendants(xmlNode* parent, const char* name, const char* ns, const auto& fn) {
    for (auto* node = parent ? parent->children : nullptr; node; node = node->next) {
        if (element(node, name, ns)) fn(node);
        descendants(node, name, ns, fn);
    }
}

using XmlText = std::unique_ptr<xmlChar, decltype(xmlFree)>;

std::string value(xmlChar* input) {
    const XmlText text(input, xmlFree);
    return text ? reinterpret_cast<const char*>(text.get()) : "";
}

std::string attr(xmlNode* node, const char* name, const char* ns = nullptr) {
    return value(ns ? xmlGetNsProp(node, BAD_CAST name, BAD_CAST ns) : xmlGetProp(node, BAD_CAST name));
}

std::string combined_text(xmlNode* parent, const char* ns) {
    std::string text;
    descendants(parent, "t", ns, [&](auto* node) { text += value(xmlNodeGetContent(node)); });
    return text;
}

Json read_docx(zip_t* archive) {
    const auto doc = parse_xml(member(archive, "word/document.xml"));
    auto paragraphs = Json::array();
    descendants(xmlDocGetRootElement(doc.get()), "p", word_ns, [&](auto* node) {
        paragraphs.push_back(combined_text(node, word_ns));
    });
    return {{"paragraphs", std::move(paragraphs)}};
}

std::size_t column_index(const std::string& reference) {
    std::size_t index = 0;
    for (char c : reference) {
        if (c < 'A' || c > 'Z') break;
        index = index * 26 + c - 'A' + 1;
        if (index > 16384) throw std::runtime_error("Invalid spreadsheet column");
    }
    if (index == 0) throw std::runtime_error("Missing spreadsheet column");
    return index - 1;
}

Json read_xlsx(zip_t* archive) {
    const auto workbook = parse_xml(member(archive, "xl/workbook.xml"));
    const auto rels = parse_xml(member(archive, "xl/_rels/workbook.xml.rels"));
    std::map<std::string, std::string> targets;
    children(xmlDocGetRootElement(rels.get()), "Relationship", package_rel_ns, [&](auto* node) {
        const auto target = attr(node, "Target");
        if (attr(node, "Type") == std::string(rel_ns) + "/worksheet" && attr(node, "TargetMode") != "External") {
            const auto path = (target.starts_with('/') ? Path(target.substr(1)) : Path("xl") / target).lexically_normal();
            if (!path.generic_string().starts_with("xl/")) throw std::runtime_error("Invalid worksheet target");
            targets.emplace(attr(node, "Id"), path.generic_string());
        }
    });
    std::vector<std::string> shared;
    const auto strings = member(archive, "xl/sharedStrings.xml", false);
    if (!strings.empty()) {
        const auto doc = parse_xml(strings);
        children(xmlDocGetRootElement(doc.get()), "si", sheet_ns, [&](auto* node) { shared.push_back(combined_text(node, sheet_ns)); });
    }
    auto sheets = Json::array();
    std::size_t cells = 0;
    descendants(xmlDocGetRootElement(workbook.get()), "sheet", sheet_ns, [&](auto* node) {
        const auto found = targets.find(attr(node, "id", rel_ns));
        if (found == targets.end()) return;
        if (sheets.size() >= 32) throw std::runtime_error("Document exceeds 32 worksheets");
        const auto sheet = parse_xml(member(archive, found->second.c_str()));
        auto rows = Json::array();
        descendants(xmlDocGetRootElement(sheet.get()), "row", sheet_ns, [&](auto* row) {
            auto values = Json::array();
            children(row, "c", sheet_ns, [&](auto* cell) {
                const auto ref = attr(cell, "r");
                const auto index = ref.empty() ? values.size() : column_index(ref);
                if (index < values.size()) throw std::runtime_error("Worksheet cells must be in column order");
                cells += index + 1 - values.size();
                if (cells > 100000) throw std::runtime_error("Workbook exceeds 100000 cells");
                auto text = std::string{};
                const auto type = attr(cell, "t");
                if (type == "inlineStr") text = combined_text(cell, sheet_ns);
                else children(cell, "v", sheet_ns, [&](auto* v) { text = value(xmlNodeGetContent(v)); });
                Json content = text.empty() && type.empty() ? Json(nullptr) : Json(text);
                if (type == "s") {
                    std::size_t end = 0;
                    const auto i = std::stoul(text, &end);
                    if (end != text.size() || i >= shared.size()) throw std::runtime_error("Bad shared string index");
                    content = shared[i];
                } else if (type == "b") content = (text == "1");
                else if ((type.empty() || type == "n") && !text.empty()) {
                    content = Json::parse(text, nullptr, false);
                    if (!content.is_number()) content = text;
                }
                values[index] = std::move(content);
            });
            if (rows.size() >= 100000) throw std::runtime_error("Worksheet exceeds 100000 rows");
            rows.push_back(std::move(values));
        });
        sheets.push_back({{"name", attr(node, "name")}, {"rows", std::move(rows)}});
    });
    return {{"sheets", std::move(sheets)}};
}

Json read_pdf(const Path& path) {
    const std::unique_ptr<poppler::document> pdf(poppler::document::load_from_file(path.string()));
    if (!pdf || pdf->is_locked()) throw std::runtime_error("Could not open PDF");
    if (pdf->pages() > 200) throw std::runtime_error("PDF exceeds 200 pages");
    auto pages = Json::array();
    std::size_t size = 0;
    for (int i = 0; i < pdf->pages(); ++i) {
        const std::unique_ptr<poppler::page> page(pdf->create_page(i));
        if (!page) throw std::runtime_error("Could not load PDF page");
        const auto bytes = page->text().to_utf8();
        size += bytes.size();
        if (size > member_limit) throw std::runtime_error("Extracted PDF text exceeds 8 MiB");
        pages.push_back(std::string(bytes.begin(), bytes.end()));
    }
    return {{"pages", std::move(pages)}};
}

std::string relationship(const std::string& target, const std::string& type) {
    return "<Relationships xmlns=\"" + std::string(package_rel_ns) + "\"><Relationship Id=\"rId1\" Type=\""
        + rel_ns + "/" + type + "\" Target=\"" + target + "\"/></Relationships>";
}

std::string content_types(const std::string& path, const std::string& type, std::string extra = {}) {
    return "<Types xmlns=\"" + std::string(content_ns) + "\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/" + path + "\" ContentType=\"application/vnd.openxmlformats-officedocument."
        + type + "\"/>" + extra + "</Types>";
}

void write_zip(const Path& path, const Parts& parts) {
    auto* raw = zip_open(path.c_str(), ZIP_CREATE | ZIP_EXCL, nullptr);
    if (!raw) throw std::runtime_error("Could not create document archive");
    auto archive = std::unique_ptr<zip_t, decltype(&zip_discard)>(raw, zip_discard);
    for (const auto& [name, text] : parts) {
        if (text.size() > member_limit) throw std::invalid_argument("Document archive member exceeds 8 MiB");
        auto* source = zip_source_buffer(raw, text.data(), text.size(), 0);
        if (!source) throw std::runtime_error("Could not create archive member");
        if (zip_file_add(raw, name.c_str(), source, ZIP_FL_ENC_UTF_8) < 0) {
            zip_source_free(source);
            throw std::runtime_error("Could not add archive member");
        }
    }
    if (zip_close(raw) < 0) throw std::runtime_error("Could not finish document archive");
    archive.release();
}

std::vector<std::string> paragraphs(const Json& request) {
    auto values = request.at("paragraphs").get<std::vector<std::string>>();
    if (values.size() > 10000) throw std::invalid_argument("Document exceeds 10000 paragraphs");
    return values;
}

void write_docx(const Path& path, const Json& request) {
    std::string body = "<w:document xmlns:w=\"" + std::string(word_ns) + "\"><w:body>";
    if (request.contains("title")) {
        body += "<w:p><w:r><w:rPr><w:b/><w:sz w:val=\"36\"/></w:rPr><w:t xml:space=\"preserve\">"
            + escape(request.at("title").get<std::string>()) + "</w:t></w:r></w:p>";
    }
    for (const auto& text : paragraphs(request)) {
        body += "<w:p><w:r><w:t xml:space=\"preserve\">" + escape(text) + "</w:t></w:r></w:p>";
    }
    body += "</w:body></w:document>";
    write_zip(path, {{"[Content_Types].xml", content_types("word/document.xml", "wordprocessingml.document.main+xml")},
                     {"_rels/.rels", relationship("word/document.xml", "officeDocument")},
                     {"word/document.xml", std::move(body)}});
}

std::string column_name(std::size_t number) {
    std::string name;
    for (; number; number = (number - 1) / 26) name.insert(name.begin(), static_cast<char>('A' + (number - 1) % 26));
    return name;
}

void write_xlsx(const Path& path, const Json& request) {
    const auto& rows = request.at("rows");
    if (!rows.is_array() || rows.size() > 100000) throw std::invalid_argument("Spreadsheet rows must be an array");
    std::string data = "<worksheet xmlns=\"" + std::string(sheet_ns) + "\"><sheetData>";
    std::size_t count = 0;
    for (std::size_t r = 0; r < rows.size(); ++r) {
        const auto& row = rows.at(r);
        count += row.size();
        if (!row.is_array() || row.size() > 16384 || count > 100000) throw std::invalid_argument("Invalid spreadsheet row");
        const auto line = std::to_string(r + 1);
        data += "<row r=\"" + line + "\">";
        for (std::size_t c = 0; c < row.size(); ++c) {
            const auto& cell = row.at(c);
            const auto start = "<c r=\"" + column_name(c + 1) + line + "\"";
            if (cell.is_string()) data += start + " t=\"inlineStr\"><is><t xml:space=\"preserve\">"
                + escape(cell.get_ref<const std::string&>()) + "</t></is></c>";
            else if (cell.is_boolean()) data += start + " t=\"b\"><v>" + (cell == true ? "1" : "0") + "</v></c>";
            else if (cell.is_number()) data += start + "><v>" + cell.dump() + "</v></c>";
            else if (cell.is_null()) data += start + "/>";
            else throw std::invalid_argument("Spreadsheet cells must be scalar values");
        }
        data += "</row>";
        if (data.size() > member_limit) throw std::invalid_argument("Worksheet exceeds 8 MiB");
    }
    data += "</sheetData></worksheet>";
    write_zip(path, {{"[Content_Types].xml", content_types("xl/workbook.xml", "spreadsheetml.sheet.main+xml",
                        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")},
                    {"_rels/.rels", relationship("xl/workbook.xml", "officeDocument")},
                    {"xl/workbook.xml", "<workbook xmlns=\"" + std::string(sheet_ns) + "\" xmlns:r=\"" + rel_ns
                        + "\"><sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>"},
                    {"xl/_rels/workbook.xml.rels", relationship("worksheets/sheet1.xml", "worksheet")},
                    {"xl/worksheets/sheet1.xml", std::move(data)}});
}

void write_pdf(const Path& path, const Json& request) {
    constexpr double width = 595;
    constexpr double height = 842;
    constexpr double margin = 48;
    const std::unique_ptr<cairo_surface_t, decltype(&cairo_surface_destroy)> surface(
        cairo_pdf_surface_create(path.c_str(), width, height), cairo_surface_destroy);
    const std::unique_ptr<cairo_t, decltype(&cairo_destroy)> cr(cairo_create(surface.get()), cairo_destroy);
    const auto unref = [](PangoLayout* layout) { g_object_unref(layout); };
    const std::unique_ptr<PangoLayout, decltype(unref)> layout(pango_cairo_create_layout(cr.get()), unref);
    pango_layout_set_width(layout.get(), static_cast<int>((width - 2 * margin) * PANGO_SCALE));
    pango_layout_set_wrap(layout.get(), PANGO_WRAP_WORD_CHAR);
    double y = margin;
    const auto draw = [&](const std::string& text, const char* font) {
        if (text.size() > member_limit || text.find('\0') != std::string::npos || !g_utf8_validate(text.data(), text.size(), nullptr)) {
            throw std::invalid_argument("PDF paragraph must contain valid UTF-8 text");
        }
        const std::unique_ptr<PangoFontDescription, decltype(&pango_font_description_free)> description(
            pango_font_description_from_string(font), pango_font_description_free);
        pango_layout_set_font_description(layout.get(), description.get());
        pango_layout_set_text(layout.get(), text.data(), static_cast<int>(text.size()));
        int w = 0;
        int h = 0;
        pango_layout_get_pixel_size(layout.get(), &w, &h);
        if (h > height - 2 * margin) throw std::invalid_argument("PDF paragraph exceeds a page");
        if (y + h > height - margin) {
            cairo_show_page(cr.get());
            y = margin;
        }
        cairo_move_to(cr.get(), margin, y);
        pango_cairo_show_layout(cr.get(), layout.get());
        y += h + 8;
    };
    if (request.contains("title")) draw(request.at("title").get<std::string>(), "Sans Bold 18");
    for (const auto& text : paragraphs(request)) draw(text, "Sans 10");
    cairo_show_page(cr.get());
    cairo_surface_finish(surface.get());
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS || cairo_status(cr.get()) != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Could not finish PDF");
    }
}

Json execute(const Json& request) {
    const auto action = request.at("action").get<std::string>();
    const auto name = request.at("path").get<std::string>();
    if (name.empty() || name.find('\0') != std::string::npos) throw std::invalid_argument("Expected a document path");
    const Path path(name);
    if (action == "read_pdf" || action == "read_docx" || action == "read_xlsx") {
        if (!std::filesystem::is_regular_file(path)) throw std::invalid_argument("Document input must be a regular file");
        if (action == "read_pdf") return read_pdf(path);
        const std::unique_ptr<zip_t, decltype(&zip_discard)> archive(zip_open(path.c_str(), ZIP_RDONLY, nullptr), zip_discard);
        if (!archive) throw std::runtime_error("Could not open Office document");
        return action == "read_docx" ? read_docx(archive.get()) : read_xlsx(archive.get());
    }
    const Path pending(name + ".ark-" + random_uuid());
    struct Cleanup {
        const Path& path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{pending};
    if (action == "write_docx") write_docx(pending, request);
    else if (action == "write_xlsx") write_xlsx(pending, request);
    else if (action == "write_pdf") write_pdf(pending, request);
    else throw std::invalid_argument("Unknown document action");
    std::filesystem::rename(pending, path);
    return {{"path", name}};
}

} // namespace

Json DocumentAgent::run(Json message) const {
    if (!message.is_object()) throw std::invalid_argument("Expected an ARKMessage");
    message["agent_version"] = "document_agent@1.0.0";
    try {
        if (!pending_for(message, "document_agent") || message.at("intent") != "DOCUMENT") {
            throw std::invalid_argument("DocumentAgent requires a pending DOCUMENT message");
        }
        message["output"] = execute(message.at("payload"));
        message["status"] = "DONE";
    } catch (const std::exception& error) {
        message["status"] = "FAILED";
        message["quality_score"] = 0.0;
        message["output"] = {{"error_type", "DocumentError"}, {"error_message", error.what()}};
    }
    return message;
}

} // namespace ark
