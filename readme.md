# ARK OS

ARK is a personal operating environment for preserving knowledge and running
agent workflows. The native runtime builds as the C++20 executable `ark-cpp`.
See the [native setup and commands](docs/native_cpp.md) and
[implementation guide](Implementation%20Guide/ARK_OS_Daily_Build_Plan.md).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/ark-cpp --help
```

The Python implementation is available as a migration reference. The native
runtime uses local Ollama for models, Redis and Qdrant for shared data, SQLite
for local history and skills, and Docker for isolated code execution.
