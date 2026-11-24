#include "ark/vision_agent.hpp"

#include <cairo/cairo.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

using ark::Json;

void check(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

void write_chart(const std::filesystem::path& path) {
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 640, 420);
    auto* context = cairo_create(surface);
    cairo_set_source_rgb(context, 1, 1, 1);
    cairo_paint(context);
    cairo_set_source_rgb(context, 0.12, 0.14, 0.2);
    cairo_set_line_width(context, 2);
    cairo_move_to(context, 100, 40); cairo_line_to(context, 100, 340); cairo_line_to(context, 590, 340);
    cairo_stroke(context);
    cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(context, 18);
    for (const auto& [label, y] : std::array<std::pair<const char*, double>, 3>{{{"0", 340}, {"50", 190}, {"100", 40}}}) {
        cairo_move_to(context, 55, y + 6); cairo_show_text(context, label);
    }
    constexpr std::array<double, 3> heights{60, 150, 240};
    constexpr std::array<const char*, 3> labels{"2021", "2022", "2023"};
    constexpr std::array<double, 3> xs{180, 320, 460};
    cairo_set_source_rgb(context, 0.16, 0.43, 0.78);
    for (std::size_t i = 0; i < heights.size(); ++i) {
        cairo_rectangle(context, xs[i], 340 - heights[i], 72, heights[i]);
        cairo_fill(context);
        cairo_set_source_rgb(context, 0.12, 0.14, 0.2);
        cairo_move_to(context, xs[i] + 5, 372);
        cairo_show_text(context, labels[i]);
        cairo_set_source_rgb(context, 0.16, 0.43, 0.78);
    }
    cairo_set_source_rgb(context, 0.12, 0.14, 0.2);
    cairo_move_to(context, 210, 25);
    cairo_show_text(context, "Annual observations");
    check(cairo_surface_write_to_png(surface, path.c_str()) == CAIRO_STATUS_SUCCESS, "could not write PNG chart fixture");
    cairo_destroy(context);
    cairo_surface_destroy(surface);
}

std::string base64(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto remaining = bytes.size() - i;
        const unsigned int block = (static_cast<unsigned int>(bytes[i]) << 16)
            | (remaining > 1 ? static_cast<unsigned int>(bytes[i + 1]) << 8 : 0)
            | (remaining > 2 ? static_cast<unsigned int>(bytes[i + 2]) : 0);
        result.push_back(alphabet[(block >> 18) & 63]);
        result.push_back(alphabet[(block >> 12) & 63]);
        result.push_back(remaining > 1 ? alphabet[(block >> 6) & 63] : '=');
        result.push_back(remaining > 2 ? alphabet[block & 63] : '=');
    }
    return result;
}

Json fixture_analysis() {
    return {{"summary", "The bar chart rises from 20 in 2021 to 80 in 2023."},
        {"caption", "Annual observations increase from 20 in 2021 to 80 in 2023."},
        {"extracted_text", "Annual observations 0 50 100 2021 2022 2023"}, {"chart_type", "bar"},
        {"observations", Json::array({{{"label", "2021"}, {"value", "20"}, {"unit", ""}},
            {{"label", "2022"}, {"value", "50"}, {"unit", ""}}, {{"label", "2023"}, {"value", "80"}, {"unit", ""}}})},
        {"uncertainties", Json::array()}};
}

Json request(const std::filesystem::path& chart) {
    return ark::make_message("test", "vision_agent", "VISION",
        {{"instruction", "Read the bar chart. Give each year and its approximate value."}, {"image_path", chart.string()}});
}

void formats_images_for_ollama_and_llama(const std::filesystem::path& chart) {
    const auto encoded = base64(chart);
    int calls = 0;
    const auto transport = [&](const ark::Request& call) -> ark::Response {
        ++calls;
        const auto body = Json::parse(call.body);
        check(body.at("model") == "fixture-vision", "wrong vision model");
        check(body.at("messages").at(0).at("content").get<std::string>().find("confusion matrices") != std::string::npos,
            "scientific figure guidance was not sent to the model");
        if (call.url.ends_with("/api/chat")) {
            check(body.at("messages").at(1).at("content").get<std::string>().find("bar chart") != std::string::npos
                && body.at("messages").at(1).at("images").at(0) == encoded, "Ollama image request was malformed");
            return {200, Json{{"message", {{"content", fixture_analysis().dump()}}}, {"done_reason", "stop"}}.dump()};
        }
        check(call.url.ends_with("/v1/chat/completions"), "llama.cpp image request used the wrong endpoint");
        const auto content = body.at("messages").at(1).at("content");
        check(content.at(0).at("text").get<std::string>().find("bar chart") != std::string::npos
            && content.at(1).at("image_url").at("url") == "data:image/png;base64," + encoded,
            "OpenAI-compatible image content was malformed");
        return {200, Json{{"choices", Json::array({{{"message", {{"content", fixture_analysis().dump()}}},
            {"finish_reason", "stop"}}})}}.dump()};
    };
    const auto ollama = ark::VisionAgent(transport, "http://127.0.0.1:11434", ark::vision_profile("fixture-vision"));
    const auto ollama_result = ollama.run(request(chart));
    check(ollama_result.at("status") == "DONE" && ollama_result.at("output").at("chart_type") == "bar"
        && ollama_result.at("output").at("caption").get<std::string>().find("Annual") != std::string::npos
        && ollama_result.at("output").at("observations").size() == 3
        && !ollama_result.at("payload").contains("image_path"), "Ollama vision result was not parsed or image path leaked");
    const auto llama = ark::VisionAgent(transport, "http://127.0.0.1:18090", ark::vision_profile("fixture-vision"),
                                        ark::InferenceProvider::llama_cpp);
    const auto llama_result = llama.run(request(chart));
    check(llama_result.at("status") == "DONE" && llama_result.at("output").at("observations").size() == 3
        && !llama_result.at("payload").contains("image_path") && calls == 2,
        "llama.cpp vision result was not parsed or image path leaked");
}

void returns_scientific_figure_captions(const std::filesystem::path& chart) {
    ark::CurlTransport unused;
    const auto transport = [](const ark::Request& call) -> ark::Response {
        const auto body = Json::parse(call.body);
        check(body.at("messages").at(1).at("content").get<std::string>().find("confusion matrix") != std::string::npos,
            "confusion-matrix request was not forwarded");
        const Json analysis{{"summary", "The matrix has two correct and one incorrect prediction."},
            {"caption", "Confusion matrix for cat and dog predictions; one dog is misclassified as cat."},
            {"extracted_text", "cat dog"}, {"chart_type", "Confusion Matrix"},
            {"observations", Json::array({{{"label", "actual cat / predicted cat"}, {"value", "2"}, {"unit", "count"}},
                {{"label", "actual dog / predicted cat"}, {"value", "1"}, {"unit", "count"}}})},
            {"uncertainties", Json::array()}};
        return {200, Json{{"message", {{"content", analysis.dump()}}}, {"done_reason", "stop"}}.dump()};
    };
    const auto agent = ark::VisionAgent(transport, "http://127.0.0.1:11434", ark::vision_profile("fixture-vision"));
    auto message = request(chart);
    message["payload"]["instruction"] = "Analyze this confusion matrix and write a caption.";
    const auto result = agent.run(std::move(message));
    check(result.at("status") == "DONE" && result.at("output").at("chart_type") == "confusion matrix"
        && result.at("output").at("observations").size() == 2
        && result.at("output").at("caption").get<std::string>().find("misclassified") != std::string::npos,
        "scientific confusion-matrix response was not normalized or retained");
}

void rejects_invalid_images_before_inference(const std::filesystem::path& chart) {
    int calls = 0;
    const ark::VisionAgent agent([&](const ark::Request&) -> ark::Response { ++calls; return {503, "{}"}; },
        "http://127.0.0.1:11434", ark::vision_profile("fixture-vision"));
    auto wrong_mime = request(chart);
    wrong_mime["payload"].erase("image_path");
    wrong_mime["payload"]["image"] = {{"mime_type", "image/jpeg"}, {"data", base64(chart)}};
    const auto rejected = agent.run(std::move(wrong_mime));
    check(rejected.at("status") == "FAILED" && calls == 0, "mismatched image MIME reached the model");
    auto invalid = request(chart);
    invalid["payload"].erase("image_path");
    invalid["payload"]["image"] = {{"mime_type", "image/png"}, {"data", "not-base64"}};
    check(agent.run(std::move(invalid)).at("status") == "FAILED" && calls == 0, "malformed image data reached the model");
    const auto retry = agent.run(request(chart));
    check(retry.at("status") == "RETRY" && calls == 1, "transient local inference error was not retryable");
}

void optionally_reads_a_real_chart(const std::filesystem::path& chart) {
    const auto* url = std::getenv("ARK_TEST_VISION_URL");
    if (!url || !*url) return;
    const auto* selected = std::getenv("ARK_TEST_VISION_MODEL");
    const std::string model = selected && *selected ? selected : "ark-vision";
    ark::CurlTransport http;
    const ark::VisionAgent agent(std::ref(http), url, ark::vision_profile(model), ark::InferenceProvider::llama_cpp);
    const auto result = agent.run(request(chart));
    std::cout << result.at("output").dump(2) << '\n';
    check(result.at("status") == "DONE", result.dump(2));
    check(result.at("output").at("chart_type") == "bar" && result.at("output").at("observations").size() >= 3,
          "live vision model did not report the chart type and three observations");
}

} // namespace

int main() {
    try {
        const auto root = std::filesystem::temp_directory_path() / ("ark-vision-test-" + ark::random_uuid());
        std::filesystem::create_directories(root);
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); } } cleanup{root};
        const auto chart = root / "annual-observations.png";
        write_chart(chart);
        formats_images_for_ollama_and_llama(chart);
        returns_scientific_figure_captions(chart);
        rejects_invalid_images_before_inference(chart);
        optionally_reads_a_real_chart(chart);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
