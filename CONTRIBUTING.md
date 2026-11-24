# Contributing

Thanks for taking the time to improve ARK OS. Changes should keep the native
C++20 runtime as the active implementation and preserve the frozen ARKMessage
JSON contract unless a migration plan explicitly addresses compatibility.

## Before submitting

1. Describe the behavior change and any affected agents, config, or persisted
   formats.
2. Keep network access, model calls, and external services optional in unit
   tests. Gate live integration checks on explicit local service settings.
3. Build and run the relevant native checks:

   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --parallel
   ctest --test-dir build --output-on-failure
   ```

4. Report skipped service-gated checks and any live behavior you did not verify.
   Do not describe fixtures as production or benchmark evidence.
5. Document configuration and workflow changes in `docs/native_cpp.md`.

Use focused commits with a clear summary. Do not commit credentials, local
model files, generated databases, or machine-specific build output. If a
change affects privacy, sandboxing, filesystem access, or external requests,
include the boundary in its documentation and tests.

## License

By contributing, you agree that your contributions are provided under the
repository's MIT License.
