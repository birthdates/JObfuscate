#include "config/obfuscator_config.hpp"
#include "engine/obfuscator_engine.hpp"
#include <iostream>
#include <string>
#include <vector>

void print_help(const char* prog_name) {
    std::cout << "JObfuscate (Java 8 -> 25) - CLI\n"
              << "Usage: " << prog_name << " [options] -i <input.jar> -o <output.jar>\n\n"
              << "Options:\n"
              << "  -i, --input <file>          Input JAR file path (Required)\n"
              << "  -o, --output <file>         Output obfuscated JAR file path\n"
              << "  -c, --config <file>         Path to JSON configuration profile\n"
              << "  -p, --packages <pkgs>       Target packages (e.g. *, com.*, com.testapp*) (Default: *)\n"
              << "  -x, --exclude-packages <pkgs> Packages to ignore (e.g. org.apache.*, com.google.*)\n"
              << "  --exclude-classes <cls>     Specific classes to ignore from obfuscation\n"
              << "  --ignore-dependencies       Ignore common shaded dependencies (org.apache, com.google, etc.)\n"
              << "  --json                      Enable NDJSON streaming mode for GUI/IPC\n"
              << "  -v, --verbose               Verbose console logging\n"
              << "  --rename-classes            Enable class name renaming (Default: on)\n"
              << "  --no-rename-classes         Disable class name renaming\n"
              << "  --rename-packages           Enable package hierarchy scrambling\n"
              << "  --remove-packages           Remove matched packages (unnest to default package)\n"
              << "  --keep-main-class           Preserve the Main-Class name\n"
              << "  --rename-mode <mode>        Renaming mode: short, confusing, long, hex\n"
              << "  --import-packages <pkgs>    Comma-separated packages to proxy via reflection\n"
              << "  --import-java               Proxy java.* and javax.* imports via reflection\n"
              << "  --no-strip                  Disable debug metadata stripping\n"
              << "  --no-rename                 Disable all identifier renaming\n"
              << "  --no-strings                Disable string literal encryption\n"
              << "  --no-control-flow           Disable control flow obfuscation\n"
              << "  --no-numbers                Disable number constant splitting\n"
              << "  --no-reflection             Disable dynamic reflection proxies\n"
              << "  --strings-per-decryptor-min <n> Min strings per decryptor method (default: 5)\n"
              << "  --strings-per-decryptor-max <n> Max strings per decryptor method (default: 10)\n"
              << "  --no-annotation-strings     Disable obfuscation of annotation string literals\n"
              << "  --disable-access-reduction  Disable access reduction / auto-privatization (Default: on)\n"
              << "  --reduce-access             Enable access reduction / auto-privatization\n"
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
        } else if ((arg == "-p" || arg == "--packages" || arg == "--target-packages") && i + 1 < argc) {
            config.target_packages = split_csv(argv[++i]);
        } else if ((arg == "-x" || arg == "--exclude-packages" || arg == "--ignore-packages") && i + 1 < argc) {
            config.exclude_packages = split_csv(argv[++i]);
        } else if (arg == "--exclude-classes" && i + 1 < argc) {
            config.exclude_classes = split_csv(argv[++i]);
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
        } else if (arg == "--strip-signatures") {
            config.strip.strip_signatures = true;
        } else if (arg == "--no-strip-signatures") {
            config.strip.strip_signatures = false;
        } else if (arg == "--no-rename") {
            config.rename.enabled = false;
        } else if (arg == "--no-strings") {
            config.string_encryption.enabled = false;
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
        } else if (arg == "--strings-per-decryptor-min" && i + 1 < argc) {
            config.string_encryption.strings_per_decryptor_min = std::stoull(argv[++i]);
        } else if (arg == "--strings-per-decryptor-max" && i + 1 < argc) {
            config.string_encryption.strings_per_decryptor_max = std::stoull(argv[++i]);
        } else if (arg == "--no-annotation-strings" || arg == "--no-annotation-string-obf" || arg == "--no-annotation-string-obfuscation") {
            config.string_encryption.encrypt_annotation_strings = false;
        } else if (arg == "--encrypt-annotation-strings") {
            config.string_encryption.encrypt_annotation_strings = true;
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
        config.strip = loaded.strip;
        config.rename = loaded.rename;
        config.string_encryption = loaded.string_encryption;
        config.control_flow = loaded.control_flow;
        config.reflection_proxy = loaded.reflection_proxy;
        config.number_obfuscation = loaded.number_obfuscation;
        config.access_reducer = loaded.access_reducer;
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
