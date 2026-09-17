#pragma once

#include <string>
#include <string_view>
#include <cstddef>
#include <cstdint>

namespace obf::reporter {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

struct ObfuscationStats {
    size_t classes_processed{0};
    size_t strings_encrypted{0};
    size_t methods_flattened{0};
    size_t fields_renamed{0};
    size_t methods_renamed{0};
    size_t classes_renamed{0};
    size_t debug_attributes_stripped{0};
    size_t reflection_proxies_created{0};
    size_t numbers_obfuscated{0};
    size_t members_privatized{0};
    size_t original_size_bytes{0};
    size_t output_size_bytes{0};
};

class IReporter {
public:
    virtual ~IReporter() = default;

    virtual void log(LogLevel level, std::string_view message) = 0;
    virtual void progress(std::string_view stage, size_t current, size_t total, std::string_view current_item) = 0;
    virtual void complete(bool success, uint64_t duration_ms, const ObfuscationStats& stats) = 0;
    virtual void error(std::string_view error_message) = 0;
    virtual void pass_start(std::string_view /*pass*/, std::string_view /*description*/) {}
    virtual void pass_complete(std::string_view /*pass*/, uint64_t /*duration_ms*/) {}
};

} // namespace obf::reporter
