---
name: test-writing
description: Write test cases with high coverage and simulate extreme system errors.
---

## When to use this skill

- Use this skill when you need to write tests in this project.

## How to use it

You should adhere to the following testing standards to ensure software's reliability:

## 1. Coverage Goals
- **100% Branch Coverage:** Every branch of a conditional statement must be executed in both directions (True/False).
- **MC/DC (Modified Condition/Decision Coverage):** Ensure that each condition in a complex decision affects the outcome of that decision independently.

## 2. Anomaly Testing
Don't just test if the code runs correctly, test if it "dies" gracefully:
- **OOM (Out-Of-Memory):** Simulate memory allocation failures (malloc failure) at every possible point and check if the system releases resources and stops safely.
- **I/O Error Simulation:** Simulate disk full errors, network disconnections, or write permission errors in the middle of operations.
- **Crash/Power Loss:** Simulate sudden power loss while writing data to ensure data integrity (Atomic Commit).

## 3. Additional Techniques
- **Fuzz Testing:** Send malformed, invalid, or "malicious" inputs to find hidden bugs.
- **Boundary Value:** Test boundary values (very large/small numbers, empty strings, NULL).
- **Assertions:** Use `assert()` heavily in the implementation code to detect state deviations immediately.

## 4. Response Format
- Always prioritize writing Test Cases before writing Implementation Code (TDD).
- Each test file must include a "problem description" describing the simulated condition (e.g., "Simulate I/O error at byte 1024").
