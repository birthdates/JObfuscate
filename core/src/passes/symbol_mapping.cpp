#include "passes/symbol_mapping.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <cctype>

namespace obf::passes {

std::string jvm_type_to_source(std::string_view desc) {
    if (desc.empty()) return "";

    size_t array_dim = 0;
    while (array_dim < desc.size() && desc[array_dim] == '[') {
        array_dim++;
    }

    std::string_view base = desc.substr(array_dim);
    std::string type_name;

    if (base == "V") type_name = "void";
    else if (base == "Z") type_name = "boolean";
    else if (base == "B") type_name = "byte";
    else if (base == "C") type_name = "char";
    else if (base == "S") type_name = "short";
    else if (base == "I") type_name = "int";
    else if (base == "J") type_name = "long";
    else if (base == "F") type_name = "float";
    else if (base == "D") type_name = "double";
    else if (base.starts_with('L') && base.ends_with(';')) {
        type_name = std::string(base.substr(1, base.size() - 2));
        std::replace(type_name.begin(), type_name.end(), '/', '.');
    } else {
        type_name = std::string(base);
    }

    for (size_t i = 0; i < array_dim; ++i) {
        type_name += "[]";
    }
    return type_name;
}

std::pair<std::string, std::string> jvm_method_desc_to_source(std::string_view desc) {
    if (desc.empty() || desc.front() != '(') {
        return {"void", ""};
    }

    size_t close_paren = desc.find(')');
    if (close_paren == std::string_view::npos) {
        return {"void", ""};
    }

    std::string_view params_part = desc.substr(1, close_paren - 1);
    std::string_view ret_part = desc.substr(close_paren + 1);

    std::string ret_type = jvm_type_to_source(ret_part);

    std::vector<std::string> params;
    size_t i = 0;
    while (i < params_part.size()) {
        size_t start = i;
        while (i < params_part.size() && params_part[i] == '[') {
            i++;
        }
        if (i < params_part.size() && params_part[i] == 'L') {
            size_t semi = params_part.find(';', i);
            if (semi != std::string_view::npos) {
                i = semi + 1;
            } else {
                i = params_part.size();
            }
        } else if (i < params_part.size()) {
            i++;
        }
        std::string_view param_desc = params_part.substr(start, i - start);
        params.push_back(jvm_type_to_source(param_desc));
    }

    std::string params_str;
    for (size_t p = 0; p < params.size(); ++p) {
        if (p > 0) params_str += ",";
        params_str += params[p];
    }

    return {ret_type, params_str};
}

bool write_symbol_mapping_file(const std::vector<ClassMappingRecord>& records,
                               const std::filesystem::path& out_path) {
    std::error_code ec;
    auto parent = out_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(out_path, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }

    out << "# compiler: JObfuscate\n"
        << "# compiler_version: 1.0.0\n"
        << "# mapping_format: JObfuscate\n\n";

    // Sort classes alphabetically by original name
    auto sorted_records = records;
    std::sort(sorted_records.begin(), sorted_records.end(), [](const ClassMappingRecord& a, const ClassMappingRecord& b) {
        return a.orig_name < b.orig_name;
    });

    for (const auto& cls : sorted_records) {
        out << cls.orig_name << " -> " << cls.obf_name << ":\n";

        // Fields
        for (const auto& f : cls.fields) {
            out << "    " << f.type << " " << f.orig_name << " -> " << f.obf_name << "\n";
        }

        // Methods
        for (const auto& m : cls.methods) {
            out << "    " << m.return_type << " " << m.orig_name << "(" << m.params << ") -> " << m.obf_name << "\n";
        }
    }

    return true;
}

std::string retrace_stack_trace(std::string_view mapping_content, std::string_view stack_trace) {
    std::unordered_map<std::string, std::string> obf_to_orig_class;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> class_obf_methods;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> class_obf_fields;

    // 1. Parse mapping.txt
    std::istringstream map_stream{std::string(mapping_content)};
    std::string line;
    std::string current_obf_class;

    while (std::getline(map_stream, line)) {
        // Trim trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;

        if (line.front() != ' ' && line.front() != '\t') {
            // Class mapping: orig_name -> obf_name:
            size_t arrow_pos = line.find(" -> ");
            if (arrow_pos != std::string::npos && line.back() == ':') {
                std::string orig_cls = line.substr(0, arrow_pos);
                std::string obf_cls = line.substr(arrow_pos + 4, line.size() - (arrow_pos + 4) - 1);
                // Trim whitespace
                while (!orig_cls.empty() && std::isspace(static_cast<unsigned char>(orig_cls.front()))) orig_cls.erase(orig_cls.begin());
                while (!orig_cls.empty() && std::isspace(static_cast<unsigned char>(orig_cls.back()))) orig_cls.pop_back();
                while (!obf_cls.empty() && std::isspace(static_cast<unsigned char>(obf_cls.front()))) obf_cls.erase(obf_cls.begin());
                while (!obf_cls.empty() && std::isspace(static_cast<unsigned char>(obf_cls.back()))) obf_cls.pop_back();

                obf_to_orig_class[obf_cls] = orig_cls;
                current_obf_class = obf_cls;
            }
        } else if (!current_obf_class.empty()) {
            // Member mapping line
            size_t arrow_pos = line.find(" -> ");
            if (arrow_pos == std::string::npos) continue;

            std::string left = line.substr(0, arrow_pos);
            std::string obf_name = line.substr(arrow_pos + 4);

            // Trim
            while (!left.empty() && std::isspace(static_cast<unsigned char>(left.front()))) left.erase(left.begin());
            while (!left.empty() && std::isspace(static_cast<unsigned char>(left.back()))) left.pop_back();
            while (!obf_name.empty() && std::isspace(static_cast<unsigned char>(obf_name.front()))) obf_name.erase(obf_name.begin());
            while (!obf_name.empty() && std::isspace(static_cast<unsigned char>(obf_name.back()))) obf_name.pop_back();

            size_t paren_open = left.find('(');
            if (paren_open != std::string::npos) {
                // Method: [start:end:]return_type orig_name(params)
                std::string before_paren = left.substr(0, paren_open);
                size_t last_space = before_paren.find_last_of(' ');
                std::string orig_name = (last_space != std::string::npos) ? before_paren.substr(last_space + 1) : before_paren;
                class_obf_methods[current_obf_class][obf_name] = orig_name;
            } else {
                // Field: type orig_name
                size_t last_space = left.find_last_of(' ');
                std::string orig_name = (last_space != std::string::npos) ? left.substr(last_space + 1) : left;
                class_obf_fields[current_obf_class][obf_name] = orig_name;
            }
        }
    }

    // 2. Parse and retrace stack trace lines
    std::istringstream trace_stream{std::string(stack_trace)};
    std::ostringstream result_stream;

    // Pattern for "at package.Class.method(Source:123)"
    static const std::regex at_regex(R"(^(\s*at\s+)([a-zA-Z0-9_$]+(?:\.[a-zA-Z0-9_$]+)*)\.([a-zA-Z0-9_$<>]+\b)\(([^)]*)\)(.*)$)");
    // Pattern for "Caused by: package.Class: message" or "package.Class: message"
    static const std::regex ex_regex(R"(^(\s*(?:Caused by:\s+)?)([a-zA-Z0-9_$]+(?:\.[a-zA-Z0-9_$]+)*)(:\s*.*)?$)");

    while (std::getline(trace_stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::smatch at_match;
        if (std::regex_match(line, at_match, at_regex)) {
            std::string prefix = at_match[1].str();
            std::string cls = at_match[2].str();
            std::string meth = at_match[3].str();
            std::string src = at_match[4].str();
            std::string rest = at_match[5].str();

            std::string final_cls = cls;
            std::string final_meth = meth;
            std::string final_src = src;

            if (auto cit = obf_to_orig_class.find(cls); cit != obf_to_orig_class.end()) {
                final_cls = cit->second;

                if (auto mit = class_obf_methods.find(cls); mit != class_obf_methods.end()) {
                    if (auto m_it2 = mit->second.find(meth); m_it2 != mit->second.end()) {
                        final_meth = m_it2->second;
                    }
                }

                // If source is Unknown Source or generic, derive source from original class name
                size_t last_dot = final_cls.find_last_of('.');
                std::string simple_cls = (last_dot != std::string::npos) ? final_cls.substr(last_dot + 1) : final_cls;
                // Strip inner class suffix if any ($1, $Inner)
                size_t dollar = simple_cls.find('$');
                if (dollar != std::string::npos) simple_cls = simple_cls.substr(0, dollar);

                if (src == "Unknown Source" || src.empty()) {
                    final_src = simple_cls + ".java";
                } else if (src.find(':') != std::string::npos) {
                    // Retain line number e.g. "SourceFile:42" -> "MyClass.java:42"
                    std::string line_num = src.substr(src.find(':'));
                    final_src = simple_cls + ".java" + line_num;
                }
            }

            result_stream << prefix << final_cls << "." << final_meth << "(" << final_src << ")" << rest << "\n";
            continue;
        }

        std::smatch ex_match;
        if (std::regex_match(line, ex_match, ex_regex)) {
            std::string prefix = ex_match[1].str();
            std::string cls = ex_match[2].str();
            std::string msg = ex_match[3].str();

            if (auto cit = obf_to_orig_class.find(cls); cit != obf_to_orig_class.end()) {
                result_stream << prefix << cit->second << msg << "\n";
                continue;
            }
        }

        // If no pattern matched, print line as-is
        result_stream << line << "\n";
    }

    return result_stream.str();
}

} // namespace obf::passes
