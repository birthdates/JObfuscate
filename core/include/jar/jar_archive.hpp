#pragma once

#include "../bytecode/types.hpp"
#include "../bytecode/class_file.hpp"
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <span>

namespace obf::jar {

struct JarEntry {
    std::string name;
    std::vector<bytecode::u1> data;
    bool is_directory{false};
};

class JarArchive {
public:
    JarArchive() = default;

    // Load from disk
    [[nodiscard]] bool load_from_file(const std::filesystem::path& jar_path, std::string& error_msg);

    // Save to disk
    [[nodiscard]] bool save_to_file(const std::filesystem::path& jar_path, std::string& error_msg) const;

    // Class file manipulation
    [[nodiscard]] size_t class_count() const noexcept { return m_classes.size(); }
    [[nodiscard]] size_t resource_count() const noexcept { return m_resources.size(); }

    [[nodiscard]] const std::unordered_map<std::string, bytecode::ClassFile>& classes() const noexcept {
        return m_classes;
    }
    [[nodiscard]] std::unordered_map<std::string, bytecode::ClassFile>& classes() noexcept {
        return m_classes;
    }

    [[nodiscard]] const std::unordered_map<std::string, JarEntry>& resources() const noexcept {
        return m_resources;
    }
    [[nodiscard]] std::unordered_map<std::string, JarEntry>& resources() noexcept {
        return m_resources;
    }

    [[nodiscard]] bytecode::ClassFile* find_class(std::string_view class_name);
    [[nodiscard]] const bytecode::ClassFile* find_class(std::string_view class_name) const;

    void add_or_update_class(std::string_view class_name, bytecode::ClassFile class_file);
    void add_resource(std::string_view resource_name, std::vector<bytecode::u1> data);

    // Manifest helpers
    [[nodiscard]] std::optional<std::string> get_manifest() const;
    void set_manifest(std::string_view manifest_content);
    [[nodiscard]] std::optional<std::string> get_main_class() const;
    void set_main_class(std::string_view main_class);

private:
    // Internal class storage: key is internal class name without .class (e.g. "com/example/Main")
    std::unordered_map<std::string, bytecode::ClassFile> m_classes;
    // Internal non-class resources: key is full zip path (e.g. "META-INF/MANIFEST.MF")
    std::unordered_map<std::string, JarEntry> m_resources;
};

} // namespace obf::jar
