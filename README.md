# <p align="center"><img src="gui/public/logo.svg" width="72" height="72" alt="JObfuscate Logo" /><br/>JObfuscate</p>

<p align="center">
  <a href="https://github.com/badges/shields"><img src="https://img.shields.io/badge/build-passing-brightgreen.svg?style=flat-square" alt="Build Status" /></a>
  <a href="https://github.com/dwyl/repo-badges"><img src="https://img.shields.io/badge/Java-8%20--%2025-orange.svg?style=flat-square" alt="Java 8-25" /></a>
  <a href="https://isocpp.org"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square" alt="C++20" /></a>
  <a href="https://tauri.app"><img src="https://img.shields.io/badge/Tauri-2.0-blueviolet.svg?style=flat-square" alt="Tauri 2.0" /></a>
  <a href="https://react.dev"><img src="https://img.shields.io/badge/React-19-61dafb.svg?style=flat-square" alt="React 19" /></a>
  <a href="https://github.com/dwyl/repo-badges"><img src="https://img.shields.io/badge/tests-26%2F26%20passed-success.svg?style=flat-square" alt="Tests Passed" /></a>
</p>

---

A high-performance, enterprise-grade Java bytecode hardening suite and desktop GUI written in modern C++20 and Tauri 2.0 (React 19 + TypeScript + Tailwind CSS). Engineered to protect compiled Java archives (`.jar`) from Java 8 through Java 25 against cutting-edge decompilers (Fernflower, CFR, Procyon, JD-GUI, Ghidra).

---

## Features & Obfuscation Passes

| Pass | Name | Description |
| :--- | :--- | :--- |
| **01** | **Debug Stripping** | Strips `LineNumberTable`, `LocalVariableTable`, `LocalVariableTypeTable`, `SourceFile`, `SourceDebugExtension`, and method parameter metadata. |
| **02** | **Access Reduction** | Auto-privatizes unreferenced `public` and `protected` methods and fields to `private` while respecting inheritance and reflection contracts. |
| **03** | **Dynamic Reflection Proxy** | Rewrites external invocations (e.g. `com.*`, third-party APIs, shaded dependencies) into dynamic runtime reflection, sanitizing Constant Pool `CONSTANT_Class_info` entries to eliminate decompiler imports. |
| **04** | **Multi-Type Number Obfuscation** | Decomposes constants across `int`, `long`, `float`, and `double` into multi-step arithmetic splits, bitwise XOR/negation networks, and IEEE-754 bitcast transforms (`Float.intBitsToFloat`, `Double.longBitsToDouble`). |
| **05** | **Identifier Renaming** | Scrambles package, class, method, and field names into confusing homoglyphic token chains (`IlIIllIlIll_...`). Supports package flattening, enum constant obfuscation, and `-adaptclassstrings` (ProGuard/R8) reflection mapping. |
| **06** | **String Encryption** | Encrypts string literals using rotating multi-cipher XOR algorithms and injects synthesized decryption helpers. Includes toggles for annotation string literal protection. |
| **07** | **Control Flow Flattening** | Obfuscates method control flow using opaque predicates and switch-dispatch flattening to break decompiler control-flow graph analysis. |

---

## Supported Java Versions

Supports standard classfile formats from **Java 8 (v52)** through **Java 25 (v69)**:
- **Java 8**: Lambdas, default interface methods, streams.
- **Java 11**: Nestmates, `var` in lambdas, String API enhancements.
- **Java 17**: Records, sealed classes/interfaces, pattern matching for `instanceof`, text blocks.
- **Java 21**: Sequenced collections, virtual threads, record patterns, pattern matching `switch`.
- **Java 25**: Flexible constructor bodies, unnamed variables & patterns (`_`).

---

## CLI Usage

```bash
obfuscator_cli [options] -i <input.jar> -o <output.jar>
```

### Basic Example
```bash
# Obfuscate an input archive with default full protection
obfuscator_cli -i myapp.jar -o myapp-obfuscated.jar
```

---

## Command Line Options

### Input & Output
| Flag | Description |
| :--- | :--- |
| `-i`, `--input <file>` | Path to the source input JAR archive (**Required**). |
| `-o`, `--output <file>` | Path for the obfuscated output JAR archive. |
| `-c`, `--config <file>` | Path to a JSON configuration profile. |
| `-h`, `--help` | Display CLI help menu and exit. |
| `-v`, `--verbose` | Enable verbose console logging. |

### Scoping & Filtering
| Flag | Description | Default |
| :--- | :--- | :--- |
| `-p`, `--packages <pkgs>` | Comma-separated package wildcards to include (e.g. `*`, `com.*`, `com.myapp.*`). | `*` |
| `-x`, `--exclude-packages <pkgs>` | Comma-separated package patterns to exclude from obfuscation. | None |
| `--exclude-classes <cls>` | Comma-separated class names to ignore. | None |
| `--ignore-dependencies` | Automatically ignore common shaded dependencies (`org.apache.*`, `com.google.*`, etc.). | Off |

### Renaming & Hierarchy
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--rename-classes` / `--no-rename-classes` | Enable or disable class renaming. | Enabled |
| `--rename-packages` / `--no-rename-packages` | Scramble package hierarchy into confusing names. | Disabled |
| `--remove-packages` | Flatten and unnest matched classes directly to the root package. | Disabled |
| `--keep-main-class` | Preserve the entrypoint `Main-Class` name in the archive manifest. | Disabled |
| `--rename-mode <mode>` | Identifier naming strategy: `short`, `confusing`, `long`, `hex`. | `long` |
| `--obfuscate-enums` | Obfuscate enum constant names and rewrite static initializers. | Disabled |

### Reflection & Imports
| Flag | Description | Default |
| :--- | :--- | :--- |
| `--import-packages <pkgs>` | Packages to proxy via dynamic reflection (e.g. `*`, `com.*`). | `*` |
| `--import-java` | Enable reflection proxying for standard `java.*` and `javax.*` calls. | Disabled |
| `--no-reflection` | Disable dynamic reflection proxying entirely. | Enabled |

### Pass Toggles
| Flag | Description |
| :--- | :--- |
| `--no-strip` | Disable debug metadata stripping. |
| `--no-rename` | Disable all identifier renaming (classes, methods, fields). |
| `--no-strings` | Disable string literal encryption. |
| `--no-control-flow` | Disable control-flow flattening and opaque predicates. |
| `--no-numbers` | Disable constant number splitting. |
| `--disable-access-reduction` | Disable access reduction pass (auto-privatization). Default: enabled. |
| `--reduce-access` | Force enable access reduction pass. |
| `--no-annotation-strings` | Leave annotation string literals in plaintext even when string encryption is active. |
| `--encrypt-annotation-strings`| Force encrypt annotation string literals (default). |
| `--strings-per-decryptor-min <n>` | Minimum strings bundled per generated decryptor method (default: 5). |
| `--strings-per-decryptor-max <n>` | Maximum strings bundled per generated decryptor method (default: 10). |

---

## JSON Configuration Profile

Instead of passing command-line flags, you can supply a `-c config.json` profile:

```json
{
  "input_jar": "input.jar",
  "output_jar": "output.jar",
  "target_packages": ["*"],
  "exclude_packages": ["org.slf4j.*"],
  "ignore_dependencies": true,
  "strip": {
    "strip_line_numbers": true,
    "strip_local_variables": true,
    "strip_source_file": true,
    "strip_signatures": false
  },
  "rename": {
    "enabled": true,
    "rename_classes": true,
    "rename_methods": true,
    "rename_fields": true,
    "rename_packages": true,
    "remove_packages": false,
    "obfuscate_enums": false,
    "keep_main_class": true,
    "mode": "long"
  },
  "string_encryption": {
    "enabled": true,
    "encrypt_annotation_strings": true,
    "strings_per_decryptor_min": 5,
    "strings_per_decryptor_max": 10
  },
  "reflection_proxy": {
    "enabled": true,
    "import_packages": ["*"],
    "proxy_java_imports": false
  },
  "control_flow": {
    "enabled": true,
    "flatten_switch": true
  },
  "number_obfuscation": {
    "enabled": true
  },
  "access_reducer": {
    "enabled": true
  }
}
```

---

## NDJSON Streaming Mode for GUI / IPC (`--json`)

When invoked with `--json`, the CLI suppresses standard text logs and streams structured NDJSON (Newline Delimited JSON) events to `stdout`. This allows GUI wrappers (e.g. Tauri, Electron) to parse progress and telemetry in real time:

```bash
obfuscator_cli -i app.jar -o app_obf.jar --json
```

### Event Contract

- **Pass Start Event**:
  ```json
  {"type":"pass_start","pass":"StringEncrypt","description":"Encrypts string literals"}
  ```

- **Progress Event**:
  ```json
  {"type":"progress","phase":"Obfuscating classes","percent":45.0,"processed":12,"total":28}
  ```

- **Pass Complete Event**:
  ```json
  {"type":"pass_complete","pass":"StringEncrypt","duration_ms":14}
  ```

- **Execution Summary Event**:
  ```json
  {
    "type": "summary",
    "classes_processed": 28,
    "classes_renamed": 28,
    "methods_renamed": 89,
    "fields_renamed": 38,
    "strings_encrypted": 260,
    "methods_flattened": 69,
    "reflection_proxies_created": 73,
    "numbers_obfuscated": 345,
    "members_privatized": 0,
    "debug_attributes_stripped": 214,
    "total_time_ms": 104
  }
  ```

---

## Shaded Dependency & Import Scoping Logic

When an application bundles third-party libraries (e.g. `net.blah` shaded into `myapp.jar`), you often want to hide your application's interactions with that library without altering or corrupting the shaded classes themselves:

- **Target Application Scoping**: Specify `-p "com.testapp.*"` (or `--packages`). Only classes in `com.testapp` will be scrambled, access-reduced, and control-flow flattened. Shaded classes in `net.blah.*` remain unaltered.
- **Dynamic Reflection Proxying**: Specify `--import-packages "net.blah.*"`.
- **Sanitization & Erasure**: Any invocation from `com.testapp.*` to `net.blah.Test` is rewritten into a synthesized dynamic reflection proxy using `Class.forName("net.blah.Test")`, `getMethod(...)`, and `invoke(...)`. In addition, all `CONSTANT_Class_info` references to `net/blah/Test` inside `com.testapp`'s Constant Pool are replaced with `java/lang/Object`.
- **Result**: Modern decompilers (CFR, Fernflower, Procyon) will no longer detect that `com.testapp` imports or uses `net.blah`, while `net.blah` remains fully operational inside the shaded archive.

---

## Multi-Type Number Obfuscation

JObfuscate features multi-type numeric constant obfuscation covering **`int`**, **`long`**, **`float`**, and **`double`**:

1. **Integer Splitting**:
   - **Strategy 0**: Dynamic XOR decomposition `(val ^ k) ^ k`
   - **Strategy 1**: Arithmetic addition/subtraction `(val + k) - k`
   - **Strategy 2**: Arithmetic subtraction/addition `(val - k) + k`
   - **Strategy 3**: Double arithmetic negation `-(-val)`
   - **Strategy 4**: 3-way XOR chain `((val ^ k1) ^ k2) ^ (k1 ^ k2)`
2. **64-bit Long Splitting**:
   - 64-bit entropy masks with `lxor`, `ladd`, `lsub`, and `lneg` sequences.
3. **Floating-Point Deconstruction**:
   - **`float`**: Binary bit pattern decomposition with runtime IEEE-754 reconstruction via `java.lang.Float.intBitsToFloat(intBits)`.
   - **`double`**: 64-bit binary bit pattern decomposition with runtime IEEE-754 reconstruction via `java.lang.Double.longBitsToDouble(longBits)`.

---

## JObfuscate Desktop Application (Tauri 2.0 + React 19)

A lightweight, native cross-platform GUI built with **Tauri 2.0**, **React 19**, **TypeScript**, and **Tailwind CSS**.

### Key GUI Features:
- **Preset Profiles**: One-click switching between `Max Security`, `Balanced Production`, `Stealth (Strings Only)`, and `Library / API Mode`.
- **Custom Profile Management**: Create, save, load, and export configuration profiles directly to/from JSON.
- **Visual Archive Drag & Drop**: Select or drop target `.jar` files with automatic output filename derivation (`app_obf.jar`).
- **Comprehensive Scoping Controls**: Interactive tags for `-p` package scoping, `-x` package exclusions, `--exclude-classes`, and shaded dependency filtering.
- **Fine-Grained Pass Customization**: Interactive toggles and intensity sliders for all 7 obfuscation passes.
- **Live Telemetry & Pipeline Stepper**: Real-time status cards, per-pass duration tracking, and NDJSON live streaming console.

To launch the desktop GUI:
```bash
cd gui
npm install
npm run tauri dev
```

---

## Building from Source

### Prerequisites
- **C++ Compiler**: MSVC (Visual Studio 2022 / Community 2026), GCC 11+, or Clang 13+ with C++20 support.
- **CMake**: Version 3.20 or newer.
- **Java Development Kit**: JDK 8 through 25 (JDK 25 recommended for full language test coverage).

### Compilation Commands

```bash
# Generate build configuration
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build obfuscator core library and CLI
cmake --build build --config Release
```

The resulting binary will be located at:
- **Windows**: `build/core/Release/obfuscator_cli.exe`
- **Linux / macOS**: `build/core/obfuscator_cli`

---

## Running the Automated Test Suite

The project includes an automated test harness covering all passes, secrecy verification, decompiler constant pool cleanliness, and bytecode compatibility from Java 8 to 25:

```powershell
powershell -ExecutionPolicy Bypass -File tests\run_tests.ps1
```
or
```sh
./tests/run_tests.sh
```

All 26 test suites run end-to-end, validating:
1. Full obfuscation pass pipeline.
2. Production secrecy and string erasure.
3. Pass toggles (`--no-rename`, `--no-strings`, `--no-control-flow`, `--no-reflection`, `--disable-access-reduction`, etc.).
4. Package scoping, exclusion, root flattening, and access reduction.
5. Bytecode version compatibility across **Target Java 8, 11, 17, 21, and 25**.
6. Dynamic language feature showcases (`Java11`, `Java17`, `Java21`, `Java25`).
7. NDJSON IPC protocol integrity for GUI frontends.

---

## Contributing & Reporting Issues

Because every Java application possesses unique bytecode structures—ranging from differing compilers (`javac`, ECJ, `kotlinc`, `scalac`), shaded third-party libraries, complex classloaders, multi-release JARs, and dynamic reflection frameworks (Spring, Quarkus, Jackson, Hibernate)—edge cases and incompatibilities can arise.

Community contributions, bug reports, and bytecode edge-case submissions are welcome!

### Reporting a Bug or Compatibility Issue

When submitting an issue, please include:
1. **Target Java Version**: Output of `java -version` and the compilation target version (e.g. Java 8, 11, 17, 21, 25).
2. **Obfuscator Configuration**: The exact CLI command line or `config.json` profile used.
3. **Decompiler / Runtime Error**: The full exception stack trace or decompilation failure log (e.g. Fernflower, CFR, Procyon).
4. **Minimal Reproducible Sample**: A minimal standalone class or small sample JAR that demonstrates the failure. This drastically accelerates diagnosis and fix turnaround.

### Submitting Pull Requests

1. Fork the repository and create your branch from `master`.
2. Ensure any new bytecode feature or bugfix includes a corresponding test case in `tests/` or `sample_app/`.
3. Run the automated test suite to ensure all tests pass:
4. Submit your pull request with a clear description of the bytecode transformation affected and the rationale.

---

## Cross-Platform Builds & CI/CD

A unified GitHub Actions workflow is provided at [`.github/workflows/build-release.yml`](.github/workflows/build-release.yml) to build and package both the C++ CLI (`obfuscator_cli`) and the Tauri Desktop GUI (`JObfuscate`) on demand across Windows, Linux, and macOS.

### Generated Packages & Artifacts
| Platform | CLI Executable | GUI Desktop Installers & Bundles |
| :--- | :--- | :--- |
| **Windows** | `obfuscator_cli-windows-x64.exe` | `JObfuscate_<version>_x64_en-US.msi`<br/>`JObfuscate_<version>_x64-setup.exe` (NSIS) |
| **Linux** | `obfuscator_cli-linux-x64` | `JObfuscate_<version>_amd64.AppImage`<br/>`JObfuscate_<version>_amd64.deb` |
| **macOS** | `obfuscator_cli-macos` | `JObfuscate_<version>_x64.dmg` |

---

## License & Copyright

Copyright © 2026 Liam Kubicki. All rights reserved.

Distributed under a source-available non-commercial License. See `LICENSE` for details.

