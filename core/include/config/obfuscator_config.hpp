#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>
#include <cstdint>

namespace obf::config {

enum class RenameMode {
    ShortAlpha,     // a, b, c, aa, ab...
    ConfusingChars, // _0O0, Il1, etc.
    Dictionary,     // random harmless words
    ConfusingLong,  // longer unreadable combinations like _0OoOO0O, Il1lI1l1
    RandomHex       // _a7f3c109
};

enum class StringEncryptionAlgorithm {
    XorRollingKey,  // Fast, low overhead XOR with rolling multi-byte key
    MultiLayerCrypto // Non-linear substitution + LCG keystream + permutation
};

struct StripOptions {
    bool strip_line_numbers{true};
    bool strip_local_variables{true};
    bool strip_source_file{true};
    bool strip_source_debug{true};
    bool strip_signatures{true};
    bool strip_parameters{true};
};

struct ParsedPackagePattern {
    std::string raw;              // e.g. "com.testapp.*"
    std::string match_prefix;     // e.g. "com/testapp/"
    std::string preserved_prefix; // e.g. "com/" (empty if "com.*" or "com")
    bool has_wildcard{false};
};

[[nodiscard]] ParsedPackagePattern parse_package_pattern(std::string_view pattern_str);

struct RenameOptions {
    bool enabled{true};
    bool rename_classes{true};
    bool rename_packages{true};
    bool remove_packages{false}; // removes matched packages, unnesting classes into the default package
    bool rename_methods{true};
    bool rename_fields{true};
    bool obfuscate_enums{true}; // scrambles enum constant names and clinit definitions
    bool keep_main_class{false};
    RenameMode mode{RenameMode::ConfusingLong};
    std::vector<std::string> exclusions; // packages or classes to preserve
};

struct StringEncryptionOptions {
    bool enabled{true};
    StringEncryptionAlgorithm algorithm{StringEncryptionAlgorithm::MultiLayerCrypto};
    uint32_t key_seed{0}; // 0 = randomly generated dynamic key per run
    bool encrypt_indy_concat{true}; // Encrypts invokedynamic makeConcatWithConstants recipes
    bool randomize_decryptor_names{true};
    size_t strings_per_decryptor_min{5};
    size_t strings_per_decryptor_max{10};
    bool encrypt_annotation_strings{true};
    std::vector<std::string> exclusions; // string regex patterns to skip
};

struct AccessReducerOptions {
    bool enabled{true}; // converts unused public/protected members to private
};

struct ControlFlowOptions {
    bool enabled{true};
    bool insert_opaque_predicates{true};
    bool flatten_blocks{true};
    int flattening_intensity{70}; // 0 - 100 percentage
};

struct ReflectionProxyOptions {
    bool enabled{true};
    bool proxy_invocations{true};
    bool proxy_java_imports{false}; // whether to also proxy java.* / javax.* classes
    std::vector<std::string> import_packages{"*"}; // default: {"*"}
};

struct NumberObfuscationOptions {
    bool enabled{true};
    int intensity{70}; // percentage of integers to split into XOR/arithmetic
};

struct ObfuscatorConfig {
    std::string profile_name{"Default"};
    
    // Global scope filter: packages/folders to obfuscate (e.g. {"*"}, {"com.*"}, {"com.testapp*"})
    // Default is {"*"} which obfuscates all classes across the entire JAR
    std::vector<std::string> target_packages{"*"};

    // Excluded packages: classes in these packages are never obfuscated (e.g. {"org.apache.*", "com.google.*"})
    std::vector<std::string> exclude_packages;

    // Excluded classes: specific classes never obfuscated
    std::vector<std::string> exclude_classes;

    // When true, automatically ignores common third-party / shaded dependencies (org.apache, com.google, etc.)
    bool ignore_dependencies{false};

    // Core transforms
    StripOptions strip;
    RenameOptions rename;
    StringEncryptionOptions string_encryption;
    ControlFlowOptions control_flow;
    ReflectionProxyOptions reflection_proxy;
    NumberObfuscationOptions number_obfuscation;
    AccessReducerOptions access_reducer;

    // Execution parameters
    std::filesystem::path input_jar;
    std::filesystem::path output_jar;
    std::optional<uint64_t> seed{std::nullopt}; // nullopt = high-entropy dynamic seed per run
    bool json_stream_mode{false}; // Output NDJSON on stdout for Tauri IPC
    bool verbose{false};

    // Helper to check if class falls within target_packages scope
    [[nodiscard]] bool is_class_in_scope(std::string_view class_name) const;

    // JSON serialization / deserialization helpers
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] static std::optional<ObfuscatorConfig> from_json(std::string_view json_str, std::string& error_msg);
    [[nodiscard]] static std::optional<ObfuscatorConfig> from_file(const std::filesystem::path& path, std::string& error_msg);
    bool save_to_file(const std::filesystem::path& path, std::string& error_msg) const;
};

} // namespace obf::config
