#include "ark/data_analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <set>
#include <stdexcept>

namespace ark {
namespace {

constexpr std::size_t max_csv_bytes = 10 * 1024 * 1024;
constexpr std::size_t max_rows = 100'000;
constexpr std::size_t max_columns = 50;

using Row = std::vector<std::string>;

std::vector<Row> parse_csv(std::string_view input) {
    std::vector<Row> rows;
    Row row;
    std::string field;
    bool quoted = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < input.size() && input[i + 1] == '"') { field.push_back('"'); ++i; }
                else quoted = false;
            } else field.push_back(c);
        } else if (c == '"') {
            if (!field.empty()) throw std::invalid_argument("CSV quote must begin a field");
            quoted = true;
        } else if (c == ',') {
            row.push_back(std::move(field)); field.clear();
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n') ++i;
            row.push_back(std::move(field)); field.clear();
            if (!(row.size() == 1 && row[0].empty())) rows.push_back(std::move(row));
            row.clear();
            if (rows.size() > max_rows + 1) throw std::invalid_argument("CSV exceeds 100,000 rows");
        } else field.push_back(c);
        if (field.size() > 1024 * 1024) throw std::invalid_argument("CSV field exceeds 1 MiB");
    }
    if (quoted) throw std::invalid_argument("CSV contains an unterminated quoted field");
    if (!field.empty() || !row.empty()) { row.push_back(std::move(field)); rows.push_back(std::move(row)); }
    if (rows.size() < 3 || rows.size() > max_rows + 1) throw std::invalid_argument("CSV requires a header and at least two data rows");
    const auto width = rows.front().size();
    if (width == 0 || width > max_columns) throw std::invalid_argument("CSV requires 1–50 columns");
    std::set<std::string> headers;
    for (std::size_t i = 0; i < width; ++i) {
        const auto header = rows.front()[i].empty() ? "column_" + std::to_string(i + 1) : rows.front()[i];
        if (!headers.insert(header).second) throw std::invalid_argument("CSV column headers must be unique");
    }
    for (const auto& record : rows) if (record.size() != width) throw std::invalid_argument("CSV rows have inconsistent column counts");
    return rows;
}

std::string load_csv(const Json& payload) {
    const bool has_content = payload.contains("csv");
    const bool has_path = payload.contains("csv_path");
    if (has_content == has_path) throw std::invalid_argument("Provide exactly one of csv or csv_path");
    if (has_content) {
        if (!payload.at("csv").is_string()) throw std::invalid_argument("csv must be text");
        const auto value = payload.at("csv").get<std::string>();
        if (value.empty() || value.size() > max_csv_bytes) throw std::invalid_argument("CSV must be between 1 byte and 10 MiB");
        return value;
    }
    const auto path = payload.at("csv_path").get<std::string>();
    if (path.empty() || path.size() > 4096) throw std::invalid_argument("Invalid csv_path");
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes == 0 || bytes > max_csv_bytes) throw std::invalid_argument("CSV file must be between 1 byte and 10 MiB");
    std::ifstream input(path, std::ios::binary);
    std::string content(static_cast<std::size_t>(bytes), '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!input || static_cast<std::size_t>(input.gcount()) != content.size()) throw std::runtime_error("Could not read CSV completely");
    return content;
}

bool number(std::string_view text, double& output) {
    if (text.empty()) return false;
    std::string copy(text);
    char* end = nullptr;
    output = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() + copy.size() && std::isfinite(output) && std::abs(output) <= 1e100;
}

std::string xml(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '"') out += "&quot;";
        else if (c == '\'') out += "&apos;";
        else if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t') out.push_back(c);
    }
    return out;
}

struct Column { std::string name; std::vector<double> values; };

Json svg_plot(std::string title, std::string body) {
    const std::string content = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"360\" viewBox=\"0 0 640 360\"><rect width=\"640\" height=\"360\" fill=\"white\"/><text x=\"24\" y=\"32\" font-family=\"sans-serif\" font-size=\"18\">" + xml(title) + "</text>" + body + "</svg>";
    return {{"title", std::move(title)}, {"format", "svg"}, {"content", content}};
}

} // namespace

Json analyze_csv(Json message) {
    if (!message.is_object()) throw std::invalid_argument("Data Analysis requires an ARKMessage object");
    message["agent_version"] = "data_analysis_agent@1.0.0";
    try {
        if (!pending_for(message, "data_analysis_agent") || message.at("intent") != "ANALYZE_DATA") throw std::invalid_argument("Data Analysis Agent requires a pending ANALYZE_DATA message");
        const auto& payload = message.at("payload");
        const auto rows = parse_csv(load_csv(payload));
        const auto row_count = rows.size() - 1;
        std::vector<Column> columns;
        for (std::size_t col = 0; col < rows[0].size(); ++col) {
            std::vector<double> values;
            bool valid_column = true;
            for (std::size_t i = 1; i < rows.size(); ++i) {
                double parsed = 0;
                if (rows[i][col].empty()) values.push_back(std::numeric_limits<double>::quiet_NaN());
                else if (number(rows[i][col], parsed)) values.push_back(parsed);
                else { valid_column = false; break; }
            }
            if (valid_column && std::any_of(values.begin(), values.end(), [](double v) { return std::isfinite(v); })) {
                auto name = rows[0][col].empty() ? "column_" + std::to_string(col + 1) : rows[0][col];
                columns.push_back({std::move(name), std::move(values)});
            }
        }
        if (columns.empty()) throw std::invalid_argument("CSV contains no numeric columns");

        Json statistics = Json::array();
        std::ostringstream summary;
        summary << "Analyzed " << row_count << " rows; found " << columns.size() << " numeric columns. ";
        for (const auto& column : columns) {
            std::size_t count = 0; double mean = 0, m2 = 0;
            for (const auto value : column.values) if (std::isfinite(value)) {
                ++count; const auto delta = value - mean; mean += delta / static_cast<double>(count); m2 += delta * (value - mean);
            }
            const auto deviation = count > 1 ? std::sqrt(m2 / static_cast<double>(count - 1)) : 0.0;
            statistics.push_back({{"column", column.name}, {"count", count}, {"mean", mean}, {"stddev_sample", deviation}});
            summary << column.name << ": n=" << count << ", mean=" << std::setprecision(6) << mean << ", sample SD=" << deviation << ". ";
        }

        Json correlation = Json::array();
        for (const auto& left : columns) {
            Json row = Json::object();
            for (const auto& right : columns) {
                std::size_t n = 0; double mx = 0, my = 0, xx = 0, yy = 0, xy = 0;
                for (std::size_t i = 0; i < left.values.size(); ++i) if (std::isfinite(left.values[i]) && std::isfinite(right.values[i])) {
                    ++n; const auto dx = left.values[i] - mx; const auto dy = right.values[i] - my;
                    mx += dx / static_cast<double>(n); my += dy / static_cast<double>(n);
                    xx += dx * (left.values[i] - mx); yy += dy * (right.values[i] - my); xy += dx * (right.values[i] - my);
                }
                row[right.name] = n < 2 || xx <= 0 || yy <= 0 ? Json(nullptr) : Json((xy / std::sqrt(xx)) / std::sqrt(yy));
            }
            correlation.push_back({{"column", left.name}, {"values", std::move(row)}});
        }

        const auto& first = columns.front();
        double low = std::numeric_limits<double>::infinity(), high = -std::numeric_limits<double>::infinity();
        for (const auto value : first.values) if (std::isfinite(value)) { low = std::min(low, value); high = std::max(high, value); }
        if (low == high) { low -= 0.5; high += 0.5; }
        constexpr int bins = 12;
        std::array<std::size_t, bins> counts{};
        for (const auto value : first.values) if (std::isfinite(value)) {
            const auto index = std::clamp(static_cast<int>((value - low) / (high - low) * bins), 0, bins - 1);
            ++counts[static_cast<std::size_t>(index)];
        }
        const auto peak = std::max<std::size_t>(1, *std::max_element(counts.begin(), counts.end()));
        std::string hist = "<g fill=\"#3979c3\">";
        for (int i = 0; i < bins; ++i) {
            const auto height = static_cast<int>(220.0 * counts[static_cast<std::size_t>(i)] / peak);
            hist += "<rect x=\"" + std::to_string(48 + i * 44) + "\" y=\"" + std::to_string(300 - height) + "\" width=\"38\" height=\"" + std::to_string(height) + "\"/>";
        }
        hist += "</g><text x=\"48\" y=\"330\" font-family=\"sans-serif\" font-size=\"12\">" + xml(first.name) + " distribution</text>";

        std::string scatter = "<g fill=\"#e17832\" opacity=\"0.7\">";
        const auto& second = columns.size() > 1 ? columns[1] : first;
        double ymin = std::numeric_limits<double>::infinity(), ymax = -ymin;
        for (const auto candidate : second.values) if (std::isfinite(candidate)) { ymin = std::min(ymin, candidate); ymax = std::max(ymax, candidate); }
        if (ymin == ymax) { ymin -= 0.5; ymax += 0.5; }
        std::size_t plotted = 0;
        for (std::size_t i = 0; i < row_count && plotted < 500; ++i) {
            const double x = first.values[i], y = second.values[i];
            if (!std::isfinite(x) || !std::isfinite(y)) continue;
            const double px = 48 + (x - low) / (high - low) * 540;
            const double py = 300 - (y - ymin) / (ymax - ymin) * 220;
            scatter += "<circle cx=\"" + std::to_string(px) + "\" cy=\"" + std::to_string(py) + "\" r=\"3\"/>";
            ++plotted;
        }
        scatter += "</g><text x=\"48\" y=\"330\" font-family=\"sans-serif\" font-size=\"12\">" + xml(first.name) + " vs " + xml(second.name) + "</text>";

        std::string heat = "<g font-family=\"sans-serif\" font-size=\"11\">";
        const auto cell = std::min(18, 480 / static_cast<int>(columns.size()));
        for (std::size_t i = 0; i < columns.size(); ++i) for (std::size_t j = 0; j < columns.size(); ++j) {
            std::size_t n = 0; double mx = 0, my = 0, xx = 0, yy = 0, xy = 0;
            for (std::size_t r = 0; r < row_count; ++r) if (std::isfinite(columns[i].values[r]) && std::isfinite(columns[j].values[r])) {
                ++n; const auto dx = columns[i].values[r] - mx; const auto dy = columns[j].values[r] - my;
                mx += dx / n; my += dy / n; xx += dx * (columns[i].values[r] - mx); yy += dy * (columns[j].values[r] - my); xy += dx * (columns[j].values[r] - my);
            }
            const double value = n > 1 && xx > 0 && yy > 0 ? (xy / std::sqrt(xx)) / std::sqrt(yy) : 0;
            const int shade = static_cast<int>((1.0 - std::abs(value)) * 180 + 40);
            heat += "<rect x=\"" + std::to_string(90 + j * cell) + "\" y=\"" + std::to_string(58 + i * cell) + "\" width=\"" + std::to_string(cell - 1) + "\" height=\"" + std::to_string(cell - 1) + "\" fill=\"rgb(" + std::to_string(value < 0 ? shade : 40) + "," + std::to_string(shade) + "," + std::to_string(value > 0 ? shade : 210) + ")\"/><text x=\"" + std::to_string(90 + j * cell + 2) + "\" y=\"" + std::to_string(72 + i * cell) + "\" fill=\"white\">" + std::to_string(value).substr(0, 5) + "</text>";
        }
        heat += "</g>";

        Json plots = Json::array({svg_plot("Histogram: " + first.name, std::move(hist)),
            svg_plot("Scatter: " + first.name + " vs " + second.name, std::move(scatter)),
            svg_plot("Numeric correlation heatmap", std::move(heat))});
        message["status"] = "DONE";
        message["quality_score"] = -1.0;
        message["output"] = {{"rows", row_count}, {"numeric_columns", statistics}, {"correlation", std::move(correlation)},
            {"summary", summary.str()}, {"plots", std::move(plots)}};
        message["payload"].erase("csv"); message["payload"].erase("csv_path");
    } catch (const std::exception& error) {
        message["status"] = "FAILED"; message["quality_score"] = -1.0;
        message["output"] = {{"error_type", "DataAnalysisFailed"}, {"error_message", std::string(error.what()).substr(0, 300)}};
        if (message.contains("payload")) { message["payload"].erase("csv"); message["payload"].erase("csv_path"); }
    }
    return message;
}

} // namespace ark
