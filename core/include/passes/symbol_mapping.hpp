#pragma once

#include "pass_context.hpp"
#include <string>
#include <string_view>
#include <filesystem>
#include <utility>
#include <vector>

namespace obf::passes {

struct FieldMappingRecord {
    std::string type;
    std::string orig_name;
    std::string obf_name;
};

struct MethodMappingRecord {
    std::string return_type;
    std::string orig_name;
    std::string params;
    std::string obf_name;
};

struct ClassMappingRecord {
    std::string orig_name; // Dotted, e.g. "com.testapp.service.CryptoService"
    std::string obf_name;  // Dotted, e.g. "a.b.c"
    std::vector<FieldMappingRecord> fields;
    std::vector<MethodMappingRecord> methods;
};

// Converts a JVM descriptor component to a Java source type
// e.g. "Ljava/lang/String;" -> "java.lang.String", "[B" -> "byte[]", "I" -> "int", "V" -> "void"
[[nodiscard]] std::string jvm_type_to_source(std::string_view desc);

// Converts a JVM method descriptor "(ILjava/lang/String;)V" to pair:
// return_type ("void") and comma-separated parameter types ("int,java.lang.String")
[[nodiscard]] std::pair<std::string, std::string> jvm_method_desc_to_source(std::string_view desc);

// Exports mapping records to standard mapping.txt format
bool write_symbol_mapping_file(const std::vector<ClassMappingRecord>& records,
                               const std::filesystem::path& out_path);

// Retraces/deobfuscates an obfuscated stack trace using mapping.txt content
[[nodiscard]] std::string retrace_stack_trace(std::string_view mapping_content,
                                              std::string_view stack_trace);

} // namespace obf::passes
