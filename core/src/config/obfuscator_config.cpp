#include "config/obfuscator_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace obf::config {

static constexpr const char* const COMMON_DEPENDENCY_PACKAGES[] = {
    "org/apache/",
    "com/google/",
    "com/fasterxml/",
    "io/netty/",
    "org/slf4j/",
    "ch/qos/logback/",
    "org/apache/logging/",
    "org/bouncycastle/",
    "org/springframework/",
    "kotlin/",
    "kotlinx/",
    "scala/",
    "org/jetbrains/",
    "intellij/",
    "org/yaml/",
    "org/json/",
    "net/bytebuddy/",
    "org/objectweb/asm/",
    "javassist/",
    "cglib/",
    "okhttp3/",
    "okio/",
    "com/zaxxer/",
    "org/hibernate/",
    "org/postgresql/",
    "com/mysql/",
    "org/h2/",
    "org/sqlite/",
    "org/junit/",
    "org/mockito/",
    "org/hamcrest/",
    "org/assertj/",
    "javax/",
    "jakarta/",
    "com/sun/",
    "sun/"
};

ParsedPackagePattern parse_package_pattern(std::string_view pattern_str) {
    ParsedPackagePattern pat;
    pat.raw = std::string(pattern_str);
    
    // Trim whitespace
    while (!pat.raw.empty() && std::isspace(static_cast<unsigned char>(pat.raw.front()))) pat.raw.erase(pat.raw.begin());
    while (!pat.raw.empty() && std::isspace(static_cast<unsigned char>(pat.raw.back()))) pat.raw.pop_back();

    if (pat.raw.empty() || pat.raw == "*") {
        pat.match_prefix = "";
        pat.preserved_prefix = "";
        pat.has_wildcard = true;
        return pat;
    }

    std::string s = pat.raw;
    std::replace(s.begin(), s.end(), '.', '/');

    if (s.ends_with("/*")) {
        pat.has_wildcard = true;
        s.erase(s.size() - 2);
    } else if (s.ends_with(".*")) {
        pat.has_wildcard = true;
        s.erase(s.size() - 2);
    } else if (s.ends_with('*')) {
        pat.has_wildcard = true;
        s.erase(s.size() - 1);
        if (!s.empty() && s.back() == '/') s.pop_back();
    }

    if (!s.empty()) {
        pat.match_prefix = s + "/";
        size_t last_slash = s.find_last_of('/');
        if (last_slash != std::string::npos) {
            pat.preserved_prefix = s.substr(0, last_slash + 1);
        } else {
            pat.preserved_prefix = "";
        }
    } else {
        pat.match_prefix = "";
        pat.preserved_prefix = "";
        pat.has_wildcard = true;
    }

    return pat;
}

bool ObfuscatorConfig::is_class_in_scope(std::string_view class_name) const {
    std::string norm_cls(class_name);
    std::replace(norm_cls.begin(), norm_cls.end(), '.', '/');

    // 1. Explicit class exclusions
    for (const auto& ex_cls : exclude_classes) {
        if (ex_cls.empty()) continue;
        std::string norm_ex(ex_cls);
        std::replace(norm_ex.begin(), norm_ex.end(), '.', '/');
        if (norm_cls == norm_ex) return false;
    }

    // 2. Explicit package exclusions
    for (const auto& ex_pkg : exclude_packages) {
        if (ex_pkg.empty()) continue;
        auto parsed = parse_package_pattern(ex_pkg);
        if (parsed.match_prefix.empty()) {
            return false;
        }
        if (norm_cls.starts_with(parsed.match_prefix) || 
            norm_cls == parsed.match_prefix.substr(0, parsed.match_prefix.size() - 1)) {
            return false;
        }
    }

    // 3. Shaded dependency exclusion
    if (ignore_dependencies) {
        for (const char* dep : COMMON_DEPENDENCY_PACKAGES) {
            if (norm_cls.starts_with(dep)) {
                return false;
            }
        }
        // Also check if any package path element is "shaded" or "shadow" from relocation plugins
        if (norm_cls.starts_with("shaded/") || norm_cls.starts_with("shadow/") ||
            norm_cls.find("/shaded/") != std::string::npos || norm_cls.find("/shadow/") != std::string::npos) {
            return false;
        }
    }

    // 4. Target packages matching
    if (target_packages.empty()) {
        return true;
    }
    for (const auto& pkg : target_packages) {
        if (pkg.empty()) continue;
        auto parsed = parse_package_pattern(pkg);
        if (parsed.match_prefix.empty()) {
            // "*" matches everything
            return true;
        }
        if (norm_cls.starts_with(parsed.match_prefix) || 
            norm_cls == parsed.match_prefix.substr(0, parsed.match_prefix.size() - 1)) {
            return true;
        }
    }
    return false;
}

std::string ObfuscatorConfig::to_json() const {
    json j;
    j["profile_name"] = profile_name;
    j["target_packages"] = target_packages;
    j["exclude_packages"] = exclude_packages;
    j["exclude_classes"] = exclude_classes;
    j["keep_rules"] = keep_rules;
    j["ignore_dependencies"] = ignore_dependencies;

    // Strip options
    j["strip"] = {
        {"strip_line_numbers", strip.strip_line_numbers},
        {"strip_local_variables", strip.strip_local_variables},
        {"strip_source_file", strip.strip_source_file},
        {"strip_source_debug", strip.strip_source_debug},
        {"strip_signatures", strip.strip_signatures},
        {"strip_parameters", strip.strip_parameters},
        {"strip_kotlin_metadata", strip.strip_kotlin_metadata}
    };

    // Shrink options
    j["shrink"] = {
        {"enabled", shrink.enabled},
        {"shrink_classes", shrink.shrink_classes},
        {"shrink_methods", shrink.shrink_methods},
        {"shrink_fields", shrink.shrink_fields},
        {"remove_unused_attributes", shrink.remove_unused_attributes}
    };

    // Class encryption options
    j["class_encryption"] = {
        {"enabled", class_encryption.enabled},
        {"launcher_class", class_encryption.launcher_class},
        {"resource_name", class_encryption.resource_name},
        {"main_class", class_encryption.main_class},
        {"memory_scrubbing", class_encryption.memory_scrubbing}
    };

    // Rename options
    j["rename"] = {
        {"enabled", rename.enabled},
        {"rename_classes", rename.rename_classes},
        {"rename_packages", rename.rename_packages},
        {"remove_packages", rename.remove_packages},
        {"rename_methods", rename.rename_methods},
        {"rename_fields", rename.rename_fields},
        {"obfuscate_enums", rename.obfuscate_enums},
        {"keep_main_class", rename.keep_main_class},
        {"scramble_methods", rename.scramble_methods},
        {"remap_resources", rename.remap_resources},
        {"resource_patterns", rename.resource_patterns},
        {"mode", static_cast<int>(rename.mode)},
        {"exclusions", rename.exclusions}
    };

    // String encryption options
    j["string_encryption"] = {
        {"enabled", string_encryption.enabled},
        {"algorithm", static_cast<int>(string_encryption.algorithm)},
        {"key_seed", string_encryption.key_seed},
        {"encrypt_indy_concat", string_encryption.encrypt_indy_concat},
        {"randomize_decryptor_names", string_encryption.randomize_decryptor_names},
        {"strings_per_decryptor_min", string_encryption.strings_per_decryptor_min},
        {"strings_per_decryptor_max", string_encryption.strings_per_decryptor_max},
        {"encrypt_annotation_strings", string_encryption.encrypt_annotation_strings},
        {"split_keys", string_encryption.split_keys},
        {"exclusions", string_encryption.exclusions}
    };

    // Access reducer options
    j["access_reducer"] = {
        {"enabled", access_reducer.enabled}
    };

    // Control flow options
    j["control_flow"] = {
        {"enabled", control_flow.enabled},
        {"insert_opaque_predicates", control_flow.insert_opaque_predicates},
        {"flatten_blocks", control_flow.flatten_blocks},
        {"flattening_intensity", control_flow.flattening_intensity},
        {"decompiler_traps", control_flow.decompiler_traps},
        {"exception_dispatch", control_flow.exception_dispatch}
    };

    // Indy call site options
    j["indy_call_sites"] = {
        {"enabled", indy_call_sites.enabled},
        {"target_packages", indy_call_sites.target_packages}
    };

    // Reflection proxy options
    j["reflection_proxy"] = {
        {"enabled", reflection_proxy.enabled},
        {"proxy_invocations", reflection_proxy.proxy_invocations},
        {"proxy_java_imports", reflection_proxy.proxy_java_imports},
        {"import_packages", reflection_proxy.import_packages},
        {"mh_lookup_helpers_min", reflection_proxy.mh_lookup_helpers_min},
        {"mh_lookup_helpers_max", reflection_proxy.mh_lookup_helpers_max}
    };

    // Number obfuscation options
    j["number_obfuscation"] = {
        {"enabled", number_obfuscation.enabled},
        {"intensity", number_obfuscation.intensity}
    };

    // Condy options
    j["condy"] = {
        {"enabled", condy.enabled},
        {"encrypt_constants", condy.encrypt_constants}
    };

    // Decompiler deterrence options
    j["decompiler_deterrence"] = {
        {"enabled", decompiler_deterrence.enabled},
        {"overload_return_types", decompiler_deterrence.overload_return_types},
        {"synthetic_bridge_tags", decompiler_deterrence.synthetic_bridge_tags},
        {"phantom_inner_classes", decompiler_deterrence.phantom_inner_classes},
        {"custom_attributes", decompiler_deterrence.custom_attributes},
        {"scramble_method_order", decompiler_deterrence.scramble_method_order}
    };

    j["input_jar"] = input_jar.string();
    j["output_jar"] = output_jar.string();
    j["mapping_file"] = mapping_file.string();
    if (seed.has_value()) j["seed"] = *seed;
    j["json_stream_mode"] = json_stream_mode;
    j["verbose"] = verbose;

    return j.dump(4);
}

std::optional<ObfuscatorConfig> ObfuscatorConfig::from_json(std::string_view json_str, std::string& error_msg) {
    try {
        auto j = json::parse(json_str);
        ObfuscatorConfig cfg;

        if (j.contains("profile_name")) cfg.profile_name = j["profile_name"];
        if (j.contains("seed") && !j["seed"].is_null()) {
            if (j["seed"].is_number()) {
                cfg.seed = j["seed"].get<uint64_t>();
            } else if (j["seed"].is_string()) {
                try { cfg.seed = std::stoull(j["seed"].get<std::string>()); } catch (...) {}
            }
        }
        if (j.contains("target_packages")) cfg.target_packages = j["target_packages"].get<std::vector<std::string>>();
        if (j.contains("exclude_packages")) cfg.exclude_packages = j["exclude_packages"].get<std::vector<std::string>>();
        if (j.contains("exclude_classes")) cfg.exclude_classes = j["exclude_classes"].get<std::vector<std::string>>();
        if (j.contains("keep_rules")) cfg.keep_rules = j["keep_rules"].get<std::vector<std::string>>();
        if (j.contains("ignore_dependencies")) cfg.ignore_dependencies = j["ignore_dependencies"].get<bool>();

        if (j.contains("strip")) {
            const auto& s = j["strip"];
            if (s.contains("strip_line_numbers")) cfg.strip.strip_line_numbers = s["strip_line_numbers"];
            if (s.contains("strip_local_variables")) cfg.strip.strip_local_variables = s["strip_local_variables"];
            if (s.contains("strip_source_file")) cfg.strip.strip_source_file = s["strip_source_file"];
            if (s.contains("strip_source_debug")) cfg.strip.strip_source_debug = s["strip_source_debug"];
            if (s.contains("strip_signatures")) cfg.strip.strip_signatures = s["strip_signatures"];
            if (s.contains("strip_parameters")) cfg.strip.strip_parameters = s["strip_parameters"];
            if (s.contains("strip_kotlin_metadata")) cfg.strip.strip_kotlin_metadata = s["strip_kotlin_metadata"];
        }

        if (j.contains("shrink")) {
            const auto& sh = j["shrink"];
            if (sh.contains("enabled")) cfg.shrink.enabled = sh["enabled"];
            if (sh.contains("shrink_classes")) cfg.shrink.shrink_classes = sh["shrink_classes"];
            if (sh.contains("shrink_methods")) cfg.shrink.shrink_methods = sh["shrink_methods"];
            if (sh.contains("shrink_fields")) cfg.shrink.shrink_fields = sh["shrink_fields"];
            if (sh.contains("remove_unused_attributes")) cfg.shrink.remove_unused_attributes = sh["remove_unused_attributes"];
        }

        if (j.contains("class_encryption")) {
            const auto& ce = j["class_encryption"];
            if (ce.contains("enabled")) cfg.class_encryption.enabled = ce["enabled"];
            if (ce.contains("launcher_class")) cfg.class_encryption.launcher_class = ce["launcher_class"].get<std::string>();
            if (ce.contains("resource_name")) cfg.class_encryption.resource_name = ce["resource_name"].get<std::string>();
            if (ce.contains("main_class")) cfg.class_encryption.main_class = ce["main_class"].get<std::string>();
            if (ce.contains("memory_scrubbing")) cfg.class_encryption.memory_scrubbing = ce["memory_scrubbing"];
        }

        if (j.contains("rename")) {
            const auto& r = j["rename"];
            if (r.contains("enabled")) cfg.rename.enabled = r["enabled"];
            if (r.contains("rename_classes")) cfg.rename.rename_classes = r["rename_classes"];
            if (r.contains("rename_packages")) cfg.rename.rename_packages = r["rename_packages"];
            if (r.contains("remove_packages")) cfg.rename.remove_packages = r["remove_packages"];
            if (r.contains("rename_methods")) cfg.rename.rename_methods = r["rename_methods"];
            if (r.contains("rename_fields")) cfg.rename.rename_fields = r["rename_fields"];
            if (r.contains("obfuscate_enums")) cfg.rename.obfuscate_enums = r["obfuscate_enums"];
            if (r.contains("keep_main_class")) cfg.rename.keep_main_class = r["keep_main_class"];
            if (r.contains("scramble_methods")) cfg.rename.scramble_methods = r["scramble_methods"];
            if (r.contains("remap_resources")) cfg.rename.remap_resources = r["remap_resources"];
            if (r.contains("resource_patterns")) cfg.rename.resource_patterns = r["resource_patterns"].get<std::vector<std::string>>();
            if (r.contains("mode")) {
                if (r["mode"].is_number()) {
                    cfg.rename.mode = static_cast<RenameMode>(r["mode"].get<int>());
                } else if (r["mode"].is_string()) {
                    std::string m = r["mode"].get<std::string>();
                    if (m == "short") cfg.rename.mode = RenameMode::ShortAlpha;
                    else if (m == "confusing") cfg.rename.mode = RenameMode::ConfusingChars;
                    else if (m == "hex") cfg.rename.mode = RenameMode::RandomHex;
                    else cfg.rename.mode = RenameMode::ConfusingLong;
                }
            }
            if (r.contains("exclusions")) cfg.rename.exclusions = r["exclusions"].get<std::vector<std::string>>();
        }

        if (j.contains("access_reducer")) {
            const auto& ar = j["access_reducer"];
            if (ar.contains("enabled")) cfg.access_reducer.enabled = ar["enabled"];
        }

        if (j.contains("string_encryption")) {
            const auto& se = j["string_encryption"];
            if (se.contains("enabled")) cfg.string_encryption.enabled = se["enabled"];
            if (se.contains("algorithm")) {
                if (se["algorithm"].is_number()) {
                    cfg.string_encryption.algorithm = static_cast<StringEncryptionAlgorithm>(se["algorithm"].get<int>());
                } else if (se["algorithm"].is_string()) {
                    std::string a = se["algorithm"].get<std::string>();
                    if (a == "XorRollingKey" || a == "XorShift" || a == "xor") cfg.string_encryption.algorithm = StringEncryptionAlgorithm::XorRollingKey;
                    else cfg.string_encryption.algorithm = StringEncryptionAlgorithm::MultiLayerCrypto;
                }
            }
            if (se.contains("key_seed")) {
                if (se["key_seed"].is_number()) {
                    cfg.string_encryption.key_seed = static_cast<uint32_t>(se["key_seed"].get<uint64_t>());
                } else if (se["key_seed"].is_string()) {
                    try { cfg.string_encryption.key_seed = static_cast<uint32_t>(std::stoull(se["key_seed"].get<std::string>())); } catch (...) {}
                }
            }
            if (se.contains("encrypt_indy_concat")) cfg.string_encryption.encrypt_indy_concat = se["encrypt_indy_concat"];
            if (se.contains("randomize_decryptor_names")) cfg.string_encryption.randomize_decryptor_names = se["randomize_decryptor_names"];
            if (se.contains("strings_per_decryptor_min")) {
                if (se["strings_per_decryptor_min"].is_number()) {
                    cfg.string_encryption.strings_per_decryptor_min = se["strings_per_decryptor_min"].get<size_t>();
                } else if (se["strings_per_decryptor_min"].is_string()) {
                    try { cfg.string_encryption.strings_per_decryptor_min = std::stoull(se["strings_per_decryptor_min"].get<std::string>()); } catch (...) {}
                }
            }
            if (se.contains("strings_per_decryptor_max")) {
                if (se["strings_per_decryptor_max"].is_number()) {
                    cfg.string_encryption.strings_per_decryptor_max = se["strings_per_decryptor_max"].get<size_t>();
                } else if (se["strings_per_decryptor_max"].is_string()) {
                    try { cfg.string_encryption.strings_per_decryptor_max = std::stoull(se["strings_per_decryptor_max"].get<std::string>()); } catch (...) {}
                }
            }
            if (se.contains("encrypt_annotation_strings")) cfg.string_encryption.encrypt_annotation_strings = se["encrypt_annotation_strings"];
            if (se.contains("split_keys")) cfg.string_encryption.split_keys = se["split_keys"];
            if (se.contains("exclusions")) cfg.string_encryption.exclusions = se["exclusions"].get<std::vector<std::string>>();
        }

        if (j.contains("control_flow")) {
            const auto& cf = j["control_flow"];
            if (cf.contains("enabled")) cfg.control_flow.enabled = cf["enabled"];
            if (cf.contains("insert_opaque_predicates")) cfg.control_flow.insert_opaque_predicates = cf["insert_opaque_predicates"];
            if (cf.contains("flatten_blocks")) cfg.control_flow.flatten_blocks = cf["flatten_blocks"];
            if (cf.contains("decompiler_traps")) cfg.control_flow.decompiler_traps = cf["decompiler_traps"];
            if (cf.contains("exception_dispatch")) cfg.control_flow.exception_dispatch = cf["exception_dispatch"];
            if (cf.contains("flattening_intensity")) {
                if (cf["flattening_intensity"].is_number()) {
                    cfg.control_flow.flattening_intensity = cf["flattening_intensity"].get<int>();
                } else if (cf["flattening_intensity"].is_string()) {
                    try { cfg.control_flow.flattening_intensity = std::stoi(cf["flattening_intensity"].get<std::string>()); } catch (...) {}
                }
            }
        }

        if (j.contains("indy_call_sites")) {
            const auto& ics = j["indy_call_sites"];
            if (ics.contains("enabled")) cfg.indy_call_sites.enabled = ics["enabled"];
            if (ics.contains("target_packages")) cfg.indy_call_sites.target_packages = ics["target_packages"].get<std::vector<std::string>>();
        }

        if (j.contains("reflection_proxy")) {
            const auto& rp = j["reflection_proxy"];
            if (rp.contains("enabled")) cfg.reflection_proxy.enabled = rp["enabled"];
            if (rp.contains("proxy_invocations")) cfg.reflection_proxy.proxy_invocations = rp["proxy_invocations"];
            if (rp.contains("proxy_java_imports")) cfg.reflection_proxy.proxy_java_imports = rp["proxy_java_imports"];
            if (rp.contains("import_packages")) cfg.reflection_proxy.import_packages = rp["import_packages"].get<std::vector<std::string>>();
            else if (rp.contains("target_packages")) cfg.reflection_proxy.import_packages = rp["target_packages"].get<std::vector<std::string>>();
            if (rp.contains("mh_lookup_helpers_min")) {
                if (rp["mh_lookup_helpers_min"].is_number()) {
                    cfg.reflection_proxy.mh_lookup_helpers_min = rp["mh_lookup_helpers_min"].get<size_t>();
                } else if (rp["mh_lookup_helpers_min"].is_string()) {
                    try { cfg.reflection_proxy.mh_lookup_helpers_min = std::stoull(rp["mh_lookup_helpers_min"].get<std::string>()); } catch (...) {}
                }
            }
            if (rp.contains("mh_lookup_helpers_max")) {
                if (rp["mh_lookup_helpers_max"].is_number()) {
                    cfg.reflection_proxy.mh_lookup_helpers_max = rp["mh_lookup_helpers_max"].get<size_t>();
                } else if (rp["mh_lookup_helpers_max"].is_string()) {
                    try { cfg.reflection_proxy.mh_lookup_helpers_max = std::stoull(rp["mh_lookup_helpers_max"].get<std::string>()); } catch (...) {}
                }
            }
        }

        if (j.contains("number_obfuscation")) {
            const auto& no = j["number_obfuscation"];
            if (no.contains("enabled")) cfg.number_obfuscation.enabled = no["enabled"];
            if (no.contains("intensity")) {
                if (no["intensity"].is_number()) {
                    cfg.number_obfuscation.intensity = no["intensity"].get<int>();
                } else if (no["intensity"].is_string()) {
                    try { cfg.number_obfuscation.intensity = std::stoi(no["intensity"].get<std::string>()); } catch (...) {}
                }
            }
        }

        if (j.contains("condy")) {
            const auto& c = j["condy"];
            if (c.contains("enabled")) cfg.condy.enabled = c["enabled"];
            if (c.contains("encrypt_constants")) cfg.condy.encrypt_constants = c["encrypt_constants"];
        }

        if (j.contains("decompiler_deterrence")) {
            const auto& dd = j["decompiler_deterrence"];
            if (dd.contains("enabled")) cfg.decompiler_deterrence.enabled = dd["enabled"];
            if (dd.contains("overload_return_types")) cfg.decompiler_deterrence.overload_return_types = dd["overload_return_types"];
            if (dd.contains("synthetic_bridge_tags")) cfg.decompiler_deterrence.synthetic_bridge_tags = dd["synthetic_bridge_tags"];
            if (dd.contains("phantom_inner_classes")) cfg.decompiler_deterrence.phantom_inner_classes = dd["phantom_inner_classes"];
            if (dd.contains("custom_attributes")) cfg.decompiler_deterrence.custom_attributes = dd["custom_attributes"];
            if (dd.contains("scramble_method_order")) cfg.decompiler_deterrence.scramble_method_order = dd["scramble_method_order"];
        }

        if (j.contains("input_jar")) cfg.input_jar = j["input_jar"].get<std::string>();
        if (j.contains("output_jar")) cfg.output_jar = j["output_jar"].get<std::string>();
        if (j.contains("mapping_file")) cfg.mapping_file = j["mapping_file"].get<std::string>();
        if (j.contains("json_stream_mode")) cfg.json_stream_mode = j["json_stream_mode"];
        if (j.contains("verbose")) cfg.verbose = j["verbose"];

        return cfg;
    } catch (const std::exception& e) {
        error_msg = e.what();
        return std::nullopt;
    }
}

std::optional<ObfuscatorConfig> ObfuscatorConfig::from_file(const std::filesystem::path& path, std::string& error_msg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error_msg = "Could not open config file: " + path.string();
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_json(buffer.str(), error_msg);
}

bool ObfuscatorConfig::save_to_file(const std::filesystem::path& path, std::string& error_msg) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        error_msg = "Could not open config file for writing: " + path.string();
        return false;
    }
    file << to_json();
    return true;
}

} // namespace obf::config
