# Task Completion

No formal CI/lint pipeline. Manual checks:

1. Build the target(s) you changed: `cmake --build --preset debug --target <Target>`
2. If tests exist: `cmake --build --preset debug --target tests && ctest --preset debug`
3. Verify no compile errors in VS solution