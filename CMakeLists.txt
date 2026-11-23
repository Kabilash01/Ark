cmake_minimum_required(VERSION 3.24)
project(ark_os_cpp VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
option(ARK_ENABLE_LIVE_MVP "Run the full ARK MVP against local services and arXiv" OFF)
include(FetchContent)
find_package(CURL 7.85 REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)
pkg_check_modules(DOCUMENT_LIBS REQUIRED IMPORTED_TARGET libzip poppler-cpp pangocairo cairo-pdf)
if(TARGET SQLite3::SQLite3)
    set(ARK_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(ARK_SQLITE_TARGET SQLite::SQLite3)
endif()

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(httplib
    URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/tags/v0.67.0
    URL_HASH SHA256=be711ddaf7a3655d8f23aa5655709250b24d247aedd4b2629989d18b6164e5be)
FetchContent_MakeAvailable(httplib)
find_package(nlohmann_json 3.12 QUIET)
if(NOT nlohmann_json_FOUND)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(ark_native
    cpp/src/benchmark.cpp
    cpp/src/browser.cpp
    cpp/src/browser_agent.cpp
    cpp/src/citation_graph.cpp
    cpp/src/coding.cpp
    cpp/src/conflict_resolver.cpp
    cpp/src/document_agent.cpp
    cpp/src/done_condition.cpp
    cpp/src/evaluation.cpp
    cpp/src/file_system.cpp
    cpp/src/github_agent.cpp
    cpp/src/http.cpp
    cpp/src/inference.cpp
    cpp/src/kernel.cpp
    cpp/src/message.cpp
    cpp/src/privacy.cpp
    cpp/src/reflection.cpp
    cpp/src/research.cpp
    cpp/src/runtime_files.cpp
    cpp/src/sandbox.cpp
    cpp/src/security_agent.cpp
    cpp/src/storage.cpp
    cpp/src/terminal.cpp
    cpp/src/vector_memory.cpp
    cpp/src/versions.cpp
    cpp/src/web_search.cpp)
target_compile_features(ark_native PUBLIC cxx_std_20)
target_include_directories(ark_native PUBLIC cpp/include)
target_link_libraries(ark_native PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl LibXml2::LibXml2 OpenSSL::Crypto Threads::Threads
    ${ARK_SQLITE_TARGET} PkgConfig::HIREDIS PkgConfig::LIBGIT2 PkgConfig::DOCUMENT_LIBS)
set_target_properties(ark_native PROPERTIES CXX_EXTENSIONS OFF)

add_library(ark_api STATIC cpp/src/api_server.cpp)
target_link_libraries(ark_api PUBLIC ark_native PRIVATE httplib::httplib)
foreach(asset IN ITEMS index.html styles.css app.js inspectors.js)
    set(path "${CMAKE_CURRENT_SOURCE_DIR}/dashboard/${asset}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
    file(READ "${path}" "dashboard_${asset}")
endforeach()
configure_file(dashboard/assets.hpp.in generated/ark/dashboard_assets.hpp @ONLY)
target_include_directories(ark_api PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(ark-cpp cpp/src/main.cpp cpp/src/tool_benchmark.cpp)
target_link_libraries(ark-cpp PRIVATE ark_api httplib::httplib)
install(TARGETS ark-cpp RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_executable(ark_benchmark_tests cpp/tests/benchmark_test.cpp)
    target_link_libraries(ark_benchmark_tests PRIVATE ark_native)
    add_test(NAME ark_benchmark_tests COMMAND ark_benchmark_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/arkbench/benchmark_v0.1.json")
    add_executable(ark_browser_tests cpp/tests/browser_test.cpp)
    target_link_libraries(ark_browser_tests PRIVATE ark_native httplib::httplib)
    add_test(NAME ark_browser_tests COMMAND ark_browser_tests)
    set_tests_properties(ark_browser_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 45)
    add_executable(ark_browser_agent_tests cpp/tests/browser_agent_test.cpp)
    target_link_libraries(ark_browser_agent_tests PRIVATE ark_native httplib::httplib)
    add_test(NAME ark_browser_agent_tests COMMAND ark_browser_agent_tests)
    set_tests_properties(ark_browser_agent_tests PROPERTIES TIMEOUT 45)
    add_executable(ark_native_tests cpp/tests/research_test.cpp)
    target_link_libraries(ark_native_tests PRIVATE ark_native)
    add_test(NAME ark_native_tests COMMAND ark_native_tests)
    add_executable(ark_kernel_tests cpp/tests/kernel_test.cpp)
    target_link_libraries(ark_kernel_tests PRIVATE ark_native)
    add_test(NAME ark_kernel_tests COMMAND ark_kernel_tests)
    add_executable(ark_conflict_tests cpp/tests/conflict_resolver_test.cpp)
    target_link_libraries(ark_conflict_tests PRIVATE ark_native)
    add_test(NAME ark_conflict_tests COMMAND ark_conflict_tests)
    set_tests_properties(ark_conflict_tests PROPERTIES TIMEOUT 15)
    add_executable(ark_done_condition_tests cpp/tests/done_condition_test.cpp)
    target_link_libraries(ark_done_condition_tests PRIVATE ark_native)
    add_test(NAME ark_done_condition_tests COMMAND ark_done_condition_tests)
    add_executable(ark_privacy_tests cpp/tests/privacy_test.cpp)
    target_link_libraries(ark_privacy_tests PRIVATE ark_native)
    add_test(NAME ark_privacy_tests COMMAND ark_privacy_tests)
    add_executable(ark_file_system_tests cpp/tests/file_system_test.cpp)
    target_link_libraries(ark_file_system_tests PRIVATE ark_native)
    add_test(NAME ark_file_system_tests COMMAND ark_file_system_tests)
    add_executable(ark_web_search_tests cpp/tests/web_search_test.cpp)
    target_link_libraries(ark_web_search_tests PRIVATE ark_native)
    add_test(NAME ark_web_search_tests COMMAND ark_web_search_tests)
    add_executable(ark_github_tests cpp/tests/github_test.cpp)
    target_link_libraries(ark_github_tests PRIVATE ark_native)
    add_test(NAME ark_github_tests COMMAND ark_github_tests)
    add_executable(ark_storage_tests cpp/tests/storage_test.cpp)
    target_link_libraries(ark_storage_tests PRIVATE ark_native)
    add_test(NAME ark_storage_tests COMMAND ark_storage_tests)
    add_executable(ark_memory_retention_tests cpp/tests/memory_retention_test.cpp)
    target_link_libraries(ark_memory_retention_tests PRIVATE ark_native ${ARK_SQLITE_TARGET})
    add_test(NAME ark_memory_retention_tests COMMAND ark_memory_retention_tests)
    add_executable(ark_version_tests cpp/tests/versions_test.cpp)
    target_link_libraries(ark_version_tests PRIVATE ark_native)
    add_test(NAME ark_version_tests COMMAND ark_version_tests)
    add_executable(ark_reflection_tests cpp/tests/reflection_test.cpp)
    target_link_libraries(ark_reflection_tests PRIVATE ark_native)
    add_test(NAME ark_reflection_tests COMMAND ark_reflection_tests)
    add_executable(ark_vector_memory_tests cpp/tests/vector_memory_test.cpp)
    target_link_libraries(ark_vector_memory_tests PRIVATE ark_native)
    add_test(NAME ark_vector_memory_tests COMMAND ark_vector_memory_tests)
    add_executable(ark_runtime_files_tests cpp/tests/runtime_files_test.cpp)
    target_link_libraries(ark_runtime_files_tests PRIVATE ark_native)
    add_test(NAME ark_runtime_files_tests COMMAND ark_runtime_files_tests)
    add_executable(ark_sandbox_tests cpp/tests/sandbox_test.cpp)
    target_link_libraries(ark_sandbox_tests PRIVATE ark_native)
    add_test(NAME ark_sandbox_tests COMMAND ark_sandbox_tests)
    add_executable(ark_coding_tests cpp/tests/coding_test.cpp)
    target_link_libraries(ark_coding_tests PRIVATE ark_native)
    add_test(NAME ark_coding_tests COMMAND ark_coding_tests)
    add_executable(ark_evaluation_tests cpp/tests/evaluation_test.cpp)
    target_link_libraries(ark_evaluation_tests PRIVATE ark_native)
    add_test(NAME ark_evaluation_tests COMMAND ark_evaluation_tests)
    add_executable(ark_api_tests cpp/tests/api_test.cpp)
    target_link_libraries(ark_api_tests PRIVATE ark_api httplib::httplib)
    add_test(NAME ark_api_tests COMMAND ark_api_tests)
    set_tests_properties(ark_api_tests PROPERTIES TIMEOUT 15)
    add_executable(ark_phase2_integration_tests cpp/tests/phase2_integration_test.cpp)
    target_link_libraries(ark_phase2_integration_tests PRIVATE ark_native httplib::httplib)
    add_test(NAME ark_phase2_integration_tests COMMAND ark_phase2_integration_tests)
    set_tests_properties(ark_phase2_integration_tests PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 LABELS integration)
    if(ARK_ENABLE_LIVE_MVP)
        add_test(NAME ark_live_mvp COMMAND ${CMAKE_COMMAND}
            -Dark=$<TARGET_FILE:ark-cpp> -Dsource_dir=${CMAKE_SOURCE_DIR}
            -P ${CMAKE_SOURCE_DIR}/cpp/tests/mvp_live.cmake)
        set_tests_properties(ark_live_mvp PROPERTIES TIMEOUT 330 LABELS live)
    endif()
endif()
