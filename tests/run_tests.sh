#!/usr/bin/env bash
# Automated Test Harness for Java Bytecode Obfuscator (POSIX Wrapper)
# Executes the cross-platform Node.js test runner (tests/run_tests.js)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Verify Node.js is installed
if ! command -v node >/dev/null 2>&1; then
    echo "[ERROR] Node.js executable not found in PATH! Please install Node.js (v18+) to run the test suite." >&2
    exit 1
fi

exec node "${SCRIPT_DIR}/run_tests.js" "$@"
