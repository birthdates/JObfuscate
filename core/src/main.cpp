#include "config/obfuscator_config.hpp"
#include "config/keep_rule.hpp"
#include "engine/obfuscator_engine.hpp"
#include "passes/symbol_mapping.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

void print_help(const char* prog_name) {
    std::cout << "JObfuscate (Java 8 -> 25) - CLI\n"
              << "Usage: " << prog_name << " [options] -i <input.jar> -o <output.jar>\n"
              << "       " << prog_name << " --retrace <mapping.txt> [stacktrace.txt]\n\n"
              << "Options:\n"
              << "  -i, --input <file>          Input JAR file path (Required)\n"
              << "  -o, --output <file>         Output obfuscated JAR file path\n"
              << "  -c, --config <file>         Path to JSON configuration profile\n"
              << "  -m, --mapping <file>        Export symbol mapping.txt file\n"
              << "  --retrace <map> [trace]     Retrace/deobfuscate stack trace using mapping file\n"
              << "  -p, --packages <pkgs>       Target packages (e.g. *, com.*, com.testapp*) (Default: *)\n"
              << "  -x, --exclude-packages <pkgs> Packages to ignore (e.g. org.apache.*, com.google.*)\n"
              << "  --exclude-classes <cls>     Specific classes to ignore from obfuscation\n"
              << "  --keep <rule>               ProGuard-style keep rule (repeatable), e.g. --keep 'class com.foo.Bar { *; }'\n"
              << "  --keepclassmembers <rule>   Keep matching members only (class still shrinks/renames)\n"
              << "  --keep-file <file>          File with keep rules (# comments, braces may span lines)\n"
              << "  --ignore-dependencies       Ignore common shaded dependencies (org.apache, com.google, etc.)\n"
              << "  --json                      Enable NDJSON streaming mode for GUI/IPC\n"
              << "  -v, --verbose               Verbose console logging\n"
              << "  --seed <n>                  Fixed RNG seed for deterministic output (default: random per run)\n"
              << "  --rename-classes            Enable class name renaming (Default: on)\n"
              << "  --no-rename-classes         Disable class name renaming\n"
              << "  --rename-packages           Enable package hierarchy scrambling\n"
              << "  --no-rename-packages        Disable package hierarchy scrambling\n"
              << "  --remove-packages           Remove matched packages (unnest to default package)\n"
              << "  --keep-main-class           Preserve the Main-Class name\n"
              << "  --rename-mode <mode>        Renaming mode: short, confusing, long, hex\n"
              << "  --obfuscate-enums           Scramble enum constant names (Default: on)\n"
              << "  --no-obfuscate-enums        Disable enum constant scrambling\n"
              << "  --import-packages <pkgs>    Comma-separated packages to proxy via reflection\n"
              << "  --import-java               Proxy java.*, javax.*, and kotlin.* imports via reflection\n"
              << "  --mh-lookup-min <n>         Min shared MethodHandle lookup helpers in cache class (default: 2)\n"
              << "  --mh-lookup-max <n>         Max shared MethodHandle lookup helpers in cache class (default: 4)\n"
              << "  --no-strip                  Disable debug metadata stripping\n"
              << "  --strip-kotlin-metadata     Drop kotlin.Metadata name tables (Default: on)\n"
              << "  --no-strip-kotlin-metadata  Preserve kotlin.Metadata annotations\n"
              << "  --no-rename                 Disable all identifier renaming\n"
              << "  --no-strings                Disable string literal encryption\n"
              << "  --string-algo <algo>        String encryption algorithm: xor, multilayer (default: multilayer)\n"
              << "  --no-control-flow           Disable control flow obfuscation\n"
              << "  --no-numbers                Disable number constant splitting\n"
              << "  --no-reflection             Disable dynamic reflection proxies\n"
              << "  --strings-per-decryptor-min <n> Min strings per decryptor method (default: 5)\n"
              << "  --strings-per-decryptor-max <n> Max strings per decryptor method (default: 10)\n"
              << "  --no-annotation-strings     Disable obfuscation of annotation string literals\n"
              << "  --encrypt-annotation-strings Encrypt annotation string literals (Default: on)\n"
              << "  --split-keys                Build string keys on the stack from split parts (default: on)\n"
              << "  --no-split-keys             Push each string key as a single constant\n"
              << "  --disable-access-reduction  Disable access reduction / auto-privatization (Default: on)\n"
              << "  --reduce-access             Enable access reduction / auto-privatization\n"
              << "  --indy-calls                Enable invokedynamic call site indirection & BSM encryption\n"
              << "  --no-indy-calls             Disable invokedynamic call site indirection\n"
              << "  --flatten-control-flow      Enable Chenxi Wang control flow switch flattening\n"
              << "  --no-flatten-control-flow   Disable control flow switch flattening\n"
              << "  --flattening-intensity <n>  Percentage of methods to flatten (0-100)\n"
              << "  --decompiler-traps          Enable irreducible loop & overlapping exception traps (Default: on)\n"
              << "  --no-decompiler-traps       Disable decompiler traps\n"
              << "  --exception-dispatch        Route decoy edges through try/catch handlers (Default: on)\n"
              << "  --no-exception-dispatch     Disable exception-dispatch hardening\n"
              << "  --shrink                    Enable code shrinking (detects & removes unused classes/methods/fields)\n"
              << "  --no-shrink                 Disable code shrinking\n"
              << "  --shrink-classes            Enable unused class removal\n"
              << "  --no-shrink-classes         Disable unused class removal\n"
              << "  --shrink-methods            Enable unused method removal\n"
              << "  --no-shrink-methods         Disable unused method removal\n"
              << "  --shrink-fields             Enable unused field removal\n"
              << "  --no-shrink-fields          Disable unused field removal\n"
              << "  --shrink-attributes         Enable non-essential attribute stripping\n"
              << "  --no-shrink-attributes      Disable non-essential attribute stripping\n"
              << "  --encrypt-classes           Encrypt application classes into in-memory ClassLoader bundle\n"
              << "  --no-encrypt-classes        Disable class encryption (Default)\n"
              << "  --launcher-class <name>     Custom bootstrap launcher class name (Default: Bootstrap)\n"
              << "  --encrypted-resource <path> Resource path for encrypted payload (Default: META-INF/.data/app.bin)\n"
              << "  --main-class <name>         Override application main class entry point\n"
              << "  --condy                     Enable ConstantDynamic (condy) call sites (Java 11+)\n"
              << "  --no-condy                  Disable ConstantDynamic call sites\n"
              << "  --decompiler-deterrence     Enable decompiler deterrence suite\n"
              << "  --no-decompiler-deterrence  Disable decompiler deterrence suite\n"
              << "  --overload-return-types     Overload methods differentiated only by return type\n"
              << "  --no-overload-return-types  Disable return-type differentiated method overloads\n"
              << "  --synthetic-bridge-tags     Mark internal members with ACC_SYNTHETIC & ACC_BRIDGE\n"
              << "  --phantom-inner-classes     Inject phantom InnerClasses entries\n"
              << "  --custom-attributes         Inject standards-compliant custom attributes\n"
              << "  --scramble-methods          Permute method declaration order\n"
              << "  --no-scramble-methods       Disable method declaration order permutation (default: enabled)\n"
              << "  --remap-resources           Remap class references in plugin.yml & descriptors (default: enabled)\n"
              << "  --no-remap-resources        Disable resource and descriptor remapping\n"
              << "  --remap-plugin-yml          Alias for --remap-resources\n"
              << "  --resource-patterns <list>  Comma-separated resource patterns to remap\n"
              << "  -h, --help                  Show this help message\n";
}

static std::vector<std::string> split_csv(std::string_view s) {
    std::vector<std::string> res;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(',', start);
        if (end == std::string_view::npos) end = s.size();
        std::string token = std::string(s.substr(start, end - start));
        if (!token.empty()) res.push_back(token);
        start = end + 1;
    }
    return res;
}

int main(int argc, char* argv[]) {
    if (argc <= 1) {
        print_help(argv[0]);
        return 1;
    }

    if (argc >= 2 && std::string(argv[1]) == "--retrace") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " --retrace <mapping.txt> [stacktrace.txt]\n";
            return 1;
        }
        std::string mapping_path = argv[2];
        std::ifstream map_file(mapping_path);
        if (!map_file.is_open()) {
            std::cerr << "[ERROR] Could not open mapping file: " << mapping_path << "\n";
            return 1;
        }
        std::string mapping_content((std::istreambuf_iterator<char>(map_file)),
                                     std::istreambuf_iterator<char>());

        std::string stack_trace_content;
        if (argc >= 4) {
            std::string st_path = argv[3];
            std::ifstream st_file(st_path);
            if (!st_file.is_open()) {
                std::cerr << "[ERROR] Could not open stack trace file: " << st_path << "\n";
                return 1;
            }
            stack_trace_content.assign((std::istreambuf_iterator<char>(st_file)),
                                       std::istreambuf_iterator<char>());
        } else {
            stack_trace_content.assign((std::istreambuf_iterator<char>(std::cin)),
                                       std::istreambuf_iterator<char>());
        }

        std::string retraced = obf::passes::retrace_stack_trace(mapping_content, stack_trace_content);
        std::cout << retraced;
        return 0;
    }

    obf::config::ObfuscatorConfig config;
    std::string config_path;
    bool explicit_input = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if ((arg == "-i" || arg == "--input") && i + 1 < argc) {
            config.input_jar = argv[++i];
            explicit_input = true;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            config.output_jar = argv[++i];
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-m" || arg == "--mapping" || arg == "--export-mapping") && i + 1 < argc) {
            config.mapping_file = argv[++i];
        } else if ((arg == "-p" || arg == "--packages" || arg == "--target-packages") && i + 1 < argc) {
            config.target_packages = split_csv(argv[++i]);
        } else if ((arg == "-x" || arg == "--exclude-packages" || arg == "--ignore-packages") && i + 1 < argc) {
            config.exclude_packages = split_csv(argv[++i]);
        } else if (arg == "--exclude-classes" && i + 1 < argc) {
            config.exclude_classes = split_csv(argv[++i]);
        } else if (arg == "--keep" && i + 1 < argc) {
            config.keep_rules.push_back(argv[++i]);
        } else if (arg == "--keepclassmembers" && i + 1 < argc) {
            config.keep_rules.push_back("-keepclassmembers " + std::string(argv[++i]));
        } else if (arg == "--keep-file" && i + 1 < argc) {
            std::string kerr;
            if (!obf::config::read_keep_file(argv[++i], config.keep_rules, kerr)) {
                std::cerr << "[ERROR] " << kerr << "\n";
                return 1;
            }
        } else if (arg == "--ignore-dependencies" || arg == "--ignore-shaded-dependencies") {
            config.ignore_dependencies = true;
        } else if (arg == "--no-ignore-dependencies") {
            config.ignore_dependencies = false;
        } else if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoull(argv[++i]);
        } else if (arg == "--json") {
            config.json_stream_mode = true;
        } else if (arg == "-v" || arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--rename-classes") {
            config.rename.rename_classes = true;
        } else if (arg == "--no-rename-classes") {
            config.rename.rename_classes = false;
        } else if (arg == "--rename-packages") {
            config.rename.rename_packages = true;
        } else if (arg == "--no-rename-packages") {
            config.rename.rename_packages = false;
        } else if (arg == "--remove-packages" || arg == "--unnest-packages" || arg == "--flatten-packages") {
            config.rename.remove_packages = true;
        } else if (arg == "--keep-main-class") {
            config.rename.keep_main_class = true;
        } else if (arg == "--rename-mode" && i + 1 < argc) {
            std::string m = argv[++i];
            if (m == "short") config.rename.mode = obf::config::RenameMode::ShortAlpha;
            else if (m == "confusing") config.rename.mode = obf::config::RenameMode::ConfusingChars;
            else if (m == "long") config.rename.mode = obf::config::RenameMode::ConfusingLong;
            else if (m == "hex") config.rename.mode = obf::config::RenameMode::RandomHex;
        } else if (arg == "--import-packages" && i + 1 < argc) {
            config.reflection_proxy.import_packages = split_csv(argv[++i]);
        } else if (arg == "--import-java") {
            config.reflection_proxy.proxy_java_imports = true;
        } else if (arg == "--no-strip") {
            config.strip.strip_line_numbers = false;
            config.strip.strip_local_variables = false;
            config.strip.strip_source_file = false;
            config.strip.strip_source_debug = false;
            config.strip.strip_signatures = false;
            config.strip.strip_parameters = false;
            config.strip.strip_kotlin_metadata = false;
        } else if (arg == "--strip-signatures") {
            config.strip.strip_signatures = true;
        } else if (arg == "--no-strip-signatures") {
            config.strip.strip_signatures = false;
        } else if (arg == "--strip-kotlin-metadata") {
            config.strip.strip_kotlin_metadata = true;
        } else if (arg == "--no-strip-kotlin-metadata") {
            config.strip.strip_kotlin_metadata = false;
        } else if (arg == "--no-rename") {
            config.rename.enabled = false;
        } else if (arg == "--no-strings") {
            config.string_encryption.enabled = false;
        } else if ((arg == "--string-algo" || arg == "--string-algorithm") && i + 1 < argc) {
            std::string a = argv[++i];
            if (a == "xor" || a == "XorRollingKey" || a == "XorShift") {
                config.string_encryption.algorithm = obf::config::StringEncryptionAlgorithm::XorRollingKey;
            } else {
                config.string_encryption.algorithm = obf::config::StringEncryptionAlgorithm::MultiLayerCrypto;
            }
        } else if (arg == "--no-control-flow") {
            config.control_flow.enabled = false;
        } else if (arg == "--no-numbers" || arg == "--no-number-obfuscation") {
            config.number_obfuscation.enabled = false;
        } else if (arg == "--obfuscate-enums") {
            config.rename.obfuscate_enums = true;
        } else if (arg == "--no-obfuscate-enums") {
            config.rename.obfuscate_enums = false;
        } else if (arg == "--disable-access-reduction" || arg == "--no-access-reduction" || arg == "--no-reduce-access") {
            config.access_reducer.enabled = false;
        } else if (arg == "--reduce-access" || arg == "--auto-privatize" || arg == "--enable-access-reduction") {
            config.access_reducer.enabled = true;
        } else if (arg == "--no-reflection") {
            config.reflection_proxy.enabled = false;
        } else if (arg == "--mh-lookup-min" && i + 1 < argc) {
            config.reflection_proxy.mh_lookup_helpers_min = std::stoull(argv[++i]);
        } else if (arg == "--mh-lookup-max" && i + 1 < argc) {
            config.reflection_proxy.mh_lookup_helpers_max = std::stoull(argv[++i]);
        } else if (arg == "--strings-per-decryptor-min" && i + 1 < argc) {
            config.string_encryption.strings_per_decryptor_min = std::stoull(argv[++i]);
        } else if (arg == "--strings-per-decryptor-max" && i + 1 < argc) {
            config.string_encryption.strings_per_decryptor_max = std::stoull(argv[++i]);
        } else if (arg == "--no-annotation-strings" || arg == "--no-annotation-string-obf" || arg == "--no-annotation-string-obfuscation") {
            config.string_encryption.encrypt_annotation_strings = false;
        } else if (arg == "--encrypt-annotation-strings") {
            config.string_encryption.encrypt_annotation_strings = true;
        } else if (arg == "--split-keys" || arg == "--split-string-keys") {
            config.string_encryption.split_keys = true;
        } else if (arg == "--no-split-keys" || arg == "--no-split-string-keys") {
            config.string_encryption.split_keys = false;
        } else if (arg == "--indy-calls" || arg == "--indy-call-sites") {
            config.indy_call_sites.enabled = true;
        } else if (arg == "--no-indy-calls" || arg == "--no-indy-call-sites") {
            config.indy_call_sites.enabled = false;
        } else if (arg == "--flatten-control-flow" || arg == "--flatten-blocks") {
            config.control_flow.flatten_blocks = true;
        } else if (arg == "--no-flatten-control-flow" || arg == "--no-flatten-blocks") {
            config.control_flow.flatten_blocks = false;
        } else if (arg == "--flattening-intensity" && i + 1 < argc) {
            config.control_flow.flattening_intensity = std::stoi(argv[++i]);
        } else if (arg == "--decompiler-traps") {
            config.control_flow.decompiler_traps = true;
        } else if (arg == "--no-decompiler-traps") {
            config.control_flow.decompiler_traps = false;
        } else if (arg == "--exception-dispatch") {
            config.control_flow.exception_dispatch = true;
        } else if (arg == "--no-exception-dispatch") {
            config.control_flow.exception_dispatch = false;
        } else if (arg == "--shrink") {
            config.shrink.enabled = true;
        } else if (arg == "--no-shrink") {
            config.shrink.enabled = false;
        } else if (arg == "--shrink-classes") {
            config.shrink.enabled = true;
            config.shrink.shrink_classes = true;
        } else if (arg == "--no-shrink-classes") {
            config.shrink.shrink_classes = false;
        } else if (arg == "--shrink-methods") {
            config.shrink.enabled = true;
            config.shrink.shrink_methods = true;
        } else if (arg == "--no-shrink-methods") {
            config.shrink.shrink_methods = false;
        } else if (arg == "--shrink-fields") {
            config.shrink.enabled = true;
            config.shrink.shrink_fields = true;
        } else if (arg == "--no-shrink-fields") {
            config.shrink.shrink_fields = false;
        } else if (arg == "--shrink-attributes" || arg == "--remove-unused-attributes") {
            config.shrink.remove_unused_attributes = true;
        } else if (arg == "--no-shrink-attributes") {
            config.shrink.remove_unused_attributes = false;
        } else if (arg == "--encrypt-classes" || arg == "--pack") {
            config.class_encryption.enabled = true;
        } else if (arg == "--no-encrypt-classes" || arg == "--no-pack") {
            config.class_encryption.enabled = false;
        } else if (arg == "--launcher-class" && i + 1 < argc) {
            config.class_encryption.launcher_class = argv[++i];
        } else if (arg == "--encrypted-resource" && i + 1 < argc) {
            config.class_encryption.resource_name = argv[++i];
        } else if (arg == "--main-class" && i + 1 < argc) {
            config.class_encryption.main_class = argv[++i];
        } else if (arg == "--condy") {
            config.condy.enabled = true;
        } else if (arg == "--no-condy") {
            config.condy.enabled = false;
        } else if (arg == "--decompiler-deterrence") {
            config.decompiler_deterrence.enabled = true;
        } else if (arg == "--no-decompiler-deterrence") {
            config.decompiler_deterrence.enabled = false;
        } else if (arg == "--overload-return-types") {
            config.decompiler_deterrence.enabled = true;
            config.decompiler_deterrence.overload_return_types = true;
        } else if (arg == "--no-overload-return-types") {
            config.decompiler_deterrence.overload_return_types = false;
        } else if (arg == "--synthetic-bridge-tags") {
            config.decompiler_deterrence.enabled = true;
            config.decompiler_deterrence.synthetic_bridge_tags = true;
        } else if (arg == "--phantom-inner-classes") {
            config.decompiler_deterrence.enabled = true;
            config.decompiler_deterrence.phantom_inner_classes = true;
        } else if (arg == "--custom-attributes") {
            config.decompiler_deterrence.enabled = true;
            config.decompiler_deterrence.custom_attributes = true;
        } else if (arg == "--scramble-methods" || arg == "--scramble-method-order") {
            config.rename.scramble_methods = true;
            config.decompiler_deterrence.enabled = true;
            config.decompiler_deterrence.scramble_method_order = true;
        } else if (arg == "--no-scramble-methods" || arg == "--no-scramble-method-order") {
            config.rename.scramble_methods = false;
            config.decompiler_deterrence.scramble_method_order = false;
        } else if (arg == "--remap-resources" || arg == "--remap-plugin-yml") {
            config.rename.remap_resources = true;
        } else if (arg == "--no-remap-resources" || arg == "--no-remap-plugin-yml") {
            config.rename.remap_resources = false;
        } else if (arg == "--resource-patterns" && i + 1 < argc) {
            config.rename.resource_patterns = split_csv(argv[++i]);
        }
    }

    // Load config file if specified
    if (!config_path.empty()) {
        std::string err;
        auto loaded_opt = obf::config::ObfuscatorConfig::from_file(config_path, err);
        if (!loaded_opt) {
            std::cerr << "[ERROR] Could not load config file: " << err << "\n";
            return 1;
        }
        auto loaded = *loaded_opt;
        if (!explicit_input && !loaded.input_jar.empty()) config.input_jar = loaded.input_jar;
        if (config.output_jar.empty() && !loaded.output_jar.empty()) config.output_jar = loaded.output_jar;
        // Keep rules combine: file rules first, then CLI-provided ones.
        if (!loaded.keep_rules.empty()) {
            loaded.keep_rules.insert(loaded.keep_rules.end(), config.keep_rules.begin(), config.keep_rules.end());
            config.keep_rules = std::move(loaded.keep_rules);
        }
        config.strip = loaded.strip;
        config.shrink = loaded.shrink;
        config.rename = loaded.rename;
        config.string_encryption = loaded.string_encryption;
        config.control_flow = loaded.control_flow;
        config.reflection_proxy = loaded.reflection_proxy;
        config.number_obfuscation = loaded.number_obfuscation;
        config.access_reducer = loaded.access_reducer;
        config.indy_call_sites = loaded.indy_call_sites;
        config.class_encryption = loaded.class_encryption;
        config.condy = loaded.condy;
        config.decompiler_deterrence = loaded.decompiler_deterrence;
        if (config.mapping_file.empty() && !loaded.mapping_file.empty()) config.mapping_file = loaded.mapping_file;
    }

    if (config.input_jar.empty()) {
        std::cerr << "[ERROR] Input JAR must be specified via -i or --input\n";
        return 1;
    }

    if (config.output_jar.empty()) {
        std::string stem = config.input_jar.stem().string();
        config.output_jar = config.input_jar.parent_path() / (stem + "-obfuscated.jar");
    }

    obf::engine::ObfuscatorEngine engine(std::move(config));
    return engine.run() ? 0 : 1;
}
