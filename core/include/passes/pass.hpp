#pragma once

#include "pass_context.hpp"
#include "../bytecode/class_file.hpp"
#include <string_view>

namespace obf::passes {

class IPass {
public:
    virtual ~IPass() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view description() const noexcept = 0;

    // Run pass over the whole archive (default iterates over all classes)
    virtual bool run_on_archive(PassContext& ctx);

    // Run pass on a single class file
    virtual bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx);
};

void synthesize_global_cache_class(PassContext& ctx);

// Shared MethodHandle lookup helpers living in the global cache class.
// Virtual/static shapes share one descriptor; the pool member itself is
// bound to findVirtual or findStatic. Proxies call a random pool member.
inline constexpr const char* kMhLookupMethodDesc =
    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;";
inline constexpr const char* kMhLookupGetterDesc =
    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;";

} // namespace obf::passes
