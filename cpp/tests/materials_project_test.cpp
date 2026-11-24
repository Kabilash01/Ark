#include "ark/materials_project.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using ark::Json;
void check(bool condition, std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }

void queries_summary_with_secret_in_header_and_encoded_parameters() {
    bool called = false;
    ark::MaterialsProjectClient client([&](const ark::Request& request) {
        called = true;
        check(request.method == "GET", "Materials Project request was not GET");
        check(request.url.starts_with("https://api.materialsproject.org/materials/summary/?"), "wrong Materials Project route");
        check(request.url.find("mp-149%2Cmp-13") != std::string::npos, "material IDs were not URL encoded");
        check(request.url.find("band_gap%2Cformula_pretty") != std::string::npos, "fields were not URL encoded");
        check(request.url.find("secret") == std::string::npos, "API key leaked into URL");
        check(std::find(request.headers.begin(), request.headers.end(), "X-API-KEY: secret-test-key") != request.headers.end(), "API key header missing");
        check(request.url.find("_limit=25") != std::string::npos, "limit missing");
        return ark::Response{200, R"({"data":[{"material_id":"mp-149","formula_pretty":"Si"}]})"};
    }, "secret-test-key");
    const auto result = client.summaries({"mp-149", "mp-13"}, {"band_gap", "formula_pretty"}, 25);
    check(called && result.size() == 1 && result.at(0).at("material_id") == "mp-149", "summary documents were not returned");
}

void rejects_invalid_input_and_response() {
    ark::MaterialsProjectClient client([](const ark::Request&) { return ark::Response{200, "{}"}; }, "key", "http://127.0.0.1:8099");
    bool no_ids = false;
    try { (void)client.summaries({}); } catch (const std::invalid_argument&) { no_ids = true; }
    check(no_ids, "empty ID query was accepted");
    bool bad_response = false;
    try { (void)client.summaries({"mp-149"}); } catch (const std::runtime_error&) { bad_response = true; }
    check(bad_response, "response without a data array was accepted");
    bool insecure = false;
    try { ark::MaterialsProjectClient rejected([](const ark::Request&) { return ark::Response{200, "{}"}; }, "key", "http://materialsproject.org"); }
    catch (const std::invalid_argument&) { insecure = true; }
    check(insecure, "non-loopback HTTP endpoint was accepted");
}
}

int main() {
    try { queries_summary_with_secret_in_header_and_encoded_parameters(); rejects_invalid_input_and_response(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
