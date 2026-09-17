# Automated Test Harness for Java Bytecode Obfuscator (PowerShell Wrapper)
# Executes the cross-platform Node.js test runner (tests/run_tests.js)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Verify Node.js is installed
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Error "Node.js executable not found in PATH! Please install Node.js (v18+) to run the test suite."
    exit 1
}

& node "$ScriptDir\run_tests.js" @args
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
exit 0
