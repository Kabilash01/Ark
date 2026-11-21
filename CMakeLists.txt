cmake_minimum_required(VERSION 3.24)
project(ark_os_cpp VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(FetchContent)
find_package(CURL 7.85 REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
if(TARGET SQLite3::SQLite3)
    set(ARK_SQLITE_TARGET SQLite3::SQLite3)
else()
    set(ARK_SQLITE_TARGET SQLite::SQLite3)
endif()
find_package(nlohmann_json 3.12 QUIET)
if(NOT nlohmann_json_FOUND)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(nlohmann_json
        URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
        URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(ark_native
    cpp/src/coding.cpp
    cpp/src/http.cpp
    cpp/src/kernel.cpp
    cpp/src/message.cpp
    cpp/src/research.cpp
    cpp/src/runtime_files.cpp
    cpp/src/sandbox.cpp
    cpp/src/storage.cpp
    cpp/src/vector_memory.cpp)
target_compile_features(ark_native PUBLIC cxx_std_20)
target_include_directories(ark_native PUBLIC cpp/include)
target_link_libraries(ark_native PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl LibXml2::LibXml2 OpenSSL::Crypto Threads::Threads
    ${ARK_SQLITE_TARGET} PkgConfig::HIREDIS)
set_target_properties(ark_native PROPERTIES CXX_EXTENSIONS OFF)

add_executable(ark-cpp cpp/src/main.cpp)
target_link_libraries(ark-cpp PRIVATE ark_native)
install(TARGETS ark-cpp RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_executable(ark_native_tests cpp/tests/research_test.cpp)
    target_link_libraries(ark_native_tests PRIVATE ark_native)
    add_test(NAME ark_native_tests COMMAND ark_native_tests)
    add_executable(ark_kernel_tests cpp/tests/kernel_test.cpp)
    target_link_libraries(ark_kernel_tests PRIVATE ark_native)
    add_test(NAME ark_kernel_tests COMMAND ark_kernel_tests)
    add_executable(ark_storage_tests cpp/tests/storage_test.cpp)
    target_link_libraries(ark_storage_tests PRIVATE ark_native)
    add_test(NAME ark_storage_tests COMMAND ark_storage_tests)
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
endif()
