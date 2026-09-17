#include "jar/jar_archive.hpp"
#include <miniz.h>
#include <sstream>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace obf::jar {

bool JarArchive::load_from_file(const std::filesystem::path& jar_path, std::string& error_msg) {
    m_classes.clear();
    m_resources.clear();

    mz_zip_archive zip_archive;
    std::memset(&zip_archive, 0, sizeof(zip_archive));

    std::string path_str = jar_path.string();
    if (!mz_zip_reader_init_file(&zip_archive, path_str.c_str(), 0)) {
        error_msg = "Failed to open JAR archive: " + path_str + " (" + mz_zip_get_error_string(mz_zip_get_last_error(&zip_archive)) + ")";
        return false;
    }

    mz_uint num_files = mz_zip_reader_get_num_files(&zip_archive);
    for (mz_uint i = 0; i < num_files; ++i) {
        mz_zip_archive_file_stat file_stat;
        if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat)) {
            continue;
        }

        std::string filename = file_stat.m_filename;
        if (file_stat.m_is_directory) {
            continue;
        }

        // Normalize Windows backslashes to forward slashes for zip entries
        std::replace(filename.begin(), filename.end(), '\\', '/');

        size_t uncomp_size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip_archive, i, &uncomp_size, 0);
        if (!p) {
            error_msg = "Failed to extract entry: " + filename;
            mz_zip_reader_end(&zip_archive);
            return false;
        }

        std::vector<bytecode::u1> data(reinterpret_cast<bytecode::u1*>(p), reinterpret_cast<bytecode::u1*>(p) + uncomp_size);
        mz_free(p);

        if (filename.ends_with(".class")) {
            // Remove ".class" suffix to form internal name (e.g. "com/example/Main")
            std::string internal_name = filename.substr(0, filename.size() - 6);
            try {
                bytecode::ClassFile cf = bytecode::ClassFile::from_bytes(data);
                m_classes[internal_name] = std::move(cf);
            } catch (const std::exception& e) {
                // If parsing fails for a non-standard or malformed file, keep it as raw resource
                std::cerr << "[WARN] Failed to parse class " << filename << ": " << e.what() << "\n";
                JarEntry entry;
                entry.name = filename;
                entry.data = std::move(data);
                m_resources[filename] = std::move(entry);
            }
        } else {
            JarEntry entry;
            entry.name = filename;
            entry.data = std::move(data);
            m_resources[filename] = std::move(entry);
        }
    }

    mz_zip_reader_end(&zip_archive);
    return true;
}

bool JarArchive::save_to_file(const std::filesystem::path& jar_path, std::string& error_msg) const {
    std::error_code ec;

    // 1. Ensure all parent directories exist
    auto parent = jar_path.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error_msg = "Cannot create output directory '" + parent.string() + "': " + ec.message();
            return false;
        }
    }

    // 2. Ensure output file is pre-created if it does not exist
    if (!std::filesystem::exists(jar_path, ec)) {
#ifdef _WIN32
        HANDLE hFile = CreateFileW(
            jar_path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hFile == INVALID_HANDLE_VALUE) {
            DWORD win_err = GetLastError();
            if (win_err == ERROR_ACCESS_DENIED) {
                error_msg = "Access Denied: Unable to create output JAR at '" + jar_path.string() + 
                            "'. Protected system directory requires Administrator privileges. Please choose an output location in Documents, Desktop, or Downloads.";
            } else {
                error_msg = "Failed to create output file '" + jar_path.string() + "' (Win32 Error: " + std::to_string(win_err) + ")";
            }
            return false;
        }
        CloseHandle(hFile);
#else
        std::ofstream create_stream(jar_path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!create_stream.is_open()) {
            error_msg = "Permission denied or failed to create output file at: " + jar_path.string();
            return false;
        }
        create_stream.close();
#endif
    } else {
        // If file already exists, ensure it is writable (strip read-only if set)
        std::filesystem::permissions(jar_path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add, ec);
    }

    mz_zip_archive zip_archive;
    std::memset(&zip_archive, 0, sizeof(zip_archive));

    std::string path_str = jar_path.string();
    if (!mz_zip_writer_init_file(&zip_archive, path_str.c_str(), 0)) {
        mz_zip_error last_err = mz_zip_get_last_error(&zip_archive);
        if (last_err == MZ_ZIP_FILE_OPEN_FAILED) {
            error_msg = "Failed to open/write output JAR: " + path_str + " (Access Denied or file locked by another process).";
        } else {
            error_msg = "Failed to create output JAR: " + path_str + " (" + mz_zip_get_error_string(last_err) + ")";
        }
        return false;
    }

    // Write all non-class resources first (including META-INF/MANIFEST.MF)
    for (const auto& [name, entry] : m_resources) {
        if (!mz_zip_writer_add_mem(&zip_archive, name.c_str(), entry.data.data(), entry.data.size(), MZ_DEFAULT_COMPRESSION)) {
            error_msg = "Failed to write resource entry: " + name;
            mz_zip_writer_end(&zip_archive);
            return false;
        }
    }

    // Serialize and write all obfuscated classes
    for (const auto& [name, cf] : m_classes) {
        std::string zip_entry_name = name + ".class";
        std::vector<bytecode::u1> class_bytes = cf.to_bytes();
        if (!mz_zip_writer_add_mem(&zip_archive, zip_entry_name.c_str(), class_bytes.data(), class_bytes.size(), MZ_DEFAULT_COMPRESSION)) {
            error_msg = "Failed to write class entry: " + zip_entry_name;
            mz_zip_writer_end(&zip_archive);
            return false;
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip_archive)) {
        error_msg = "Failed to finalize JAR zip archive";
        mz_zip_writer_end(&zip_archive);
        return false;
    }

    mz_zip_writer_end(&zip_archive);
    return true;
}

bytecode::ClassFile* JarArchive::find_class(std::string_view class_name) {
    std::string key(class_name);
    auto it = m_classes.find(key);
    return it != m_classes.end() ? &it->second : nullptr;
}

const bytecode::ClassFile* JarArchive::find_class(std::string_view class_name) const {
    std::string key(class_name);
    auto it = m_classes.find(key);
    return it != m_classes.end() ? &it->second : nullptr;
}

void JarArchive::add_or_update_class(std::string_view class_name, bytecode::ClassFile class_file) {
    m_classes[std::string(class_name)] = std::move(class_file);
}

void JarArchive::add_resource(std::string_view resource_name, std::vector<bytecode::u1> data) {
    JarEntry entry;
    entry.name = std::string(resource_name);
    entry.data = std::move(data);
    m_resources[std::string(resource_name)] = std::move(entry);
}

std::optional<std::string> JarArchive::get_manifest() const {
    auto it = m_resources.find("META-INF/MANIFEST.MF");
    if (it == m_resources.end()) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(it->second.data.data()), it->second.data.size());
}

void JarArchive::set_manifest(std::string_view manifest_content) {
    JarEntry entry;
    entry.name = "META-INF/MANIFEST.MF";
    entry.data.assign(
        reinterpret_cast<const bytecode::u1*>(manifest_content.data()),
        reinterpret_cast<const bytecode::u1*>(manifest_content.data() + manifest_content.size())
    );
    m_resources["META-INF/MANIFEST.MF"] = std::move(entry);
}

std::optional<std::string> JarArchive::get_main_class() const {
    auto manifest = get_manifest();
    if (!manifest) return std::nullopt;

    std::istringstream stream(*manifest);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Main-Class:", 0) == 0) {
            std::string main_cls = line.substr(11);
            // Trim whitespace
            size_t first = main_cls.find_first_not_of(" \t");
            if (first == std::string::npos) return std::nullopt;
            size_t last = main_cls.find_last_not_of(" \t");
            return main_cls.substr(first, (last - first + 1));
        }
    }
    return std::nullopt;
}

void JarArchive::set_main_class(std::string_view main_class) {
    auto current_manifest = get_manifest().value_or("Manifest-Version: 1.0\r\n");
    std::istringstream stream(current_manifest);
    std::ostringstream out;
    std::string line;
    bool replaced = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Main-Class:", 0) == 0) {
            out << "Main-Class: " << main_class << "\r\n";
            replaced = true;
        } else {
            out << line << "\r\n";
        }
    }

    if (!replaced) {
        out << "Main-Class: " << main_class << "\r\n";
    }

    set_manifest(out.str());
}

} // namespace obf::jar
