#pragma once

#include "../config/obfuscator_config.hpp"
#include "../reporter/reporter.hpp"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <random>
#include <memory>
#include <chrono>

namespace obf::jar {
    class JarArchive;
}

namespace obf::passes {

struct ClassHierarchyNode {
    std::string class_name;
    std::string super_class;
    std::vector<std::string> interfaces;
    std::unordered_set<std::string> subclasses;
    bool is_external{false}; // whether it is from JDK/external library
};

class ClassHierarchy {
public:
    void add_class(const std::string& name, const std::string& super_name, const std::vector<std::string>& ifaces);
    [[nodiscard]] bool is_subclass_of(const std::string& sub, const std::string& base) const;
    [[nodiscard]] const ClassHierarchyNode* get_node(const std::string& name) const;
    [[nodiscard]] bool is_method_overridden_in_hierarchy(const std::string& class_name, const std::string& method_name, const std::string& desc) const;

private:
    std::unordered_map<std::string, ClassHierarchyNode> m_nodes;
};

struct ProxyMethodInfo {
    std::string owner_class;
    std::string proxy_method_name;
    std::string target_class;
    std::string target_method;
    std::string target_desc;
    bool is_field{false};
    bool is_constructor{false};
    bool is_class_literal{false};
};

struct PassContext {
    jar::JarArchive& archive;
    const config::ObfuscatorConfig& config;
    reporter::IReporter& reporter;
    reporter::ObfuscationStats& stats;
    ClassHierarchy hierarchy;

    // Global mapping dictionaries
    std::unordered_map<std::string, std::string> class_renames;         // "com/app/MyClass" -> "a/a/a"
    std::unordered_map<std::string, std::string> reverse_class_renames; // "a/a/a" -> "com/app/MyClass"
    std::unordered_map<std::string, std::string> field_renames;         // "com/app/MyClass.secret" -> "a"
    std::unordered_map<std::string, std::string> method_renames;        // "com/app/MyClass.doIt()V" -> "b"
    std::vector<ProxyMethodInfo> proxy_methods;

    std::mt19937_64 rng{1337};

    PassContext(jar::JarArchive& arc, const config::ObfuscatorConfig& cfg,
                reporter::IReporter& rep, reporter::ObfuscationStats& st)
        : archive(arc), config(cfg), reporter(rep), stats(st) {
        if (config.seed.has_value()) {
            rng.seed(*config.seed);
        } else {
            std::random_device rd;
            uint64_t s = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd()) ^
                         static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            rng.seed(s);
        }
    }

    [[nodiscard]] bool is_class_in_scope(std::string_view class_name) const {
        if (config.is_class_in_scope(class_name)) return true;
        auto it = reverse_class_renames.find(std::string(class_name));
        if (it != reverse_class_renames.end()) {
            return config.is_class_in_scope(it->second);
        }
        return false;
    }
};

} // namespace obf::passes
