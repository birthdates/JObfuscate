#include "passes/strip_debug_pass.hpp"

namespace obf::passes {

namespace {

// Byte lengths of annotation structs (JVMS 4.7.16). Return 0 on malformed input.
size_t element_value_length(const std::vector<bytecode::u1>& d, size_t off);
size_t annotation_length(const std::vector<bytecode::u1>& d, size_t off) {
    if (off + 4 > d.size()) return 0;
    uint16_t num_pairs = (static_cast<uint16_t>(d[off + 2]) << 8) | d[off + 3];
    size_t cur = off + 4;
    for (uint16_t p = 0; p < num_pairs; ++p) {
        if (cur + 2 > d.size()) return 0;
        cur += 2; // element_name_index
        size_t ev = element_value_length(d, cur);
        if (ev == 0) return 0;
        cur += ev;
    }
    return cur - off;
}

size_t element_value_length(const std::vector<bytecode::u1>& d, size_t off) {
    if (off >= d.size()) return 0;
    char tag = static_cast<char>(d[off]);
    switch (tag) {
        case 'B': case 'C': case 'D': case 'F':
        case 'I': case 'J': case 'S': case 'Z':
        case 's': case 'c':
            return (off + 3 <= d.size()) ? 3 : 0;
        case 'e':
            return (off + 5 <= d.size()) ? 5 : 0;
        case '@': {
            size_t inner = annotation_length(d, off + 1);
            return inner == 0 ? 0 : 1 + inner;
        }
        case '[': {
            if (off + 3 > d.size()) return 0;
            uint16_t n = (static_cast<uint16_t>(d[off + 1]) << 8) | d[off + 2];
            size_t cur = off + 3;
            for (uint16_t i = 0; i < n; ++i) {
                size_t ev = element_value_length(d, cur);
                if (ev == 0) return 0;
                cur += ev;
                if (cur > d.size()) return 0;
            }
            return cur - off;
        }
        default:
            return 0;
    }
}

// Remove annotations of the given type from annotation-array attributes.
// Returns entries removed; leaves the attribute untouched on malformed data.
size_t strip_annotation_by_type(std::vector<bytecode::Attribute>& attrs,
                                bytecode::ConstantPool& cp,
                                std::string_view target_type) {
    size_t removed = 0;
    for (auto& attr : attrs) {
        std::string_view aname = attr.name(cp);
        if (aname != "RuntimeVisibleAnnotations" && aname != "RuntimeInvisibleAnnotations") {
            continue;
        }
        auto* raw = attr.get_mut_if<bytecode::AttrRaw>();
        if (!raw || raw->data.size() < 2) continue;
        const auto& bytes = raw->data;
        uint16_t num = (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1];
        size_t off = 2;
        std::vector<bytecode::u1> kept;
        uint16_t kept_count = 0;
        bool ok = true;
        for (uint16_t i = 0; i < num; ++i) {
            size_t len = annotation_length(bytes, off);
            if (len == 0 || off + len > bytes.size()) {
                ok = false;
                break;
            }
            uint16_t type_idx = (static_cast<uint16_t>(bytes[off]) << 8) | bytes[off + 1];
            const auto* utf = cp.get_if<bytecode::CpUtf8>(type_idx);
            std::string_view type = utf ? std::string_view(utf->value) : std::string_view{};
            if (type == target_type) {
                ++removed;
            } else {
                kept.insert(kept.end(), bytes.begin() + off, bytes.begin() + off + len);
                ++kept_count;
            }
            off += len;
        }
        if (!ok || off != bytes.size()) continue; // never corrupt: skip malformed
        if (kept_count != num) {
            std::vector<bytecode::u1> rebuilt;
            rebuilt.push_back(static_cast<bytecode::u1>((kept_count >> 8) & 0xFF));
            rebuilt.push_back(static_cast<bytecode::u1>(kept_count & 0xFF));
            rebuilt.insert(rebuilt.end(), kept.begin(), kept.end());
            raw->data = std::move(rebuilt);
        }
    }
    return removed;
}

} // anonymous namespace

bool StripDebugPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.is_class_in_scope(cls.name())) return true;
    const auto& strip = ctx.config.strip;

    // Strip Class-level attributes
    if (strip.strip_source_file) {
        if (const auto* sf_attr = cls.find_attribute("SourceFile")) {
            if (const auto* sf = sf_attr->get_if<bytecode::AttrSourceFile>()) {
                if (auto* u = cls.constant_pool().get_mut_if<bytecode::CpUtf8>(sf->sourcefile_index)) {
                    u->value.clear();
                }
            }
        }
        cls.remove_attribute("SourceFile");
        ctx.stats.debug_attributes_stripped++;
    }
    if (strip.strip_source_debug) {
        cls.remove_attribute("SourceDebugExtension");
        ctx.stats.debug_attributes_stripped++;
    }
    if (strip.strip_signatures) {
        cls.remove_attribute("Signature");
        for (auto& f : cls.fields()) {
            f.remove_attribute("Signature", cls.constant_pool());
        }
        for (auto& m : cls.methods()) {
            m.remove_attribute("Signature", cls.constant_pool());
        }
        ctx.stats.debug_attributes_stripped++;
    }
    if (strip.strip_kotlin_metadata) {
        // kotlin.Metadata data1/data2 name tables leak original identifiers and
        // go stale under renaming: drop the annotation everywhere it appears.
        size_t n = strip_annotation_by_type(cls.attributes(), cls.constant_pool(), "Lkotlin/Metadata;");
        for (auto& f : cls.fields()) {
            n += strip_annotation_by_type(f.attributes, cls.constant_pool(), "Lkotlin/Metadata;");
        }
        for (auto& m : cls.methods()) {
            n += strip_annotation_by_type(m.attributes, cls.constant_pool(), "Lkotlin/Metadata;");
        }
        if (n > 0) {
            // Purge orphaned name-table strings (d1/d2 contents) from the pool.
            cls.sanitize_unused_constants();
        }
        ctx.stats.debug_attributes_stripped += n;
    }

    // Strip Method Code-level debug attributes & parameters
    for (auto& method : cls.methods()) {
        if (strip.strip_parameters) {
            method.remove_attribute("MethodParameters", cls.constant_pool());
        }
        if (auto* code = method.code_mut()) {
            std::erase_if(code->attributes, [&](const bytecode::Attribute& attr) {
                std::string_view name = attr.name(cls.constant_pool());
                if (strip.strip_line_numbers && name == "LineNumberTable") {
                    ctx.stats.debug_attributes_stripped++;
                    return true;
                }
                if (strip.strip_local_variables && (name == "LocalVariableTable" || name == "LocalVariableTypeTable")) {
                    ctx.stats.debug_attributes_stripped++;
                    return true;
                }
                return false;
            });
        }
    }

    return true;
}

} // namespace obf::passes
