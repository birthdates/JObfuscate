#include "passes/strip_debug_pass.hpp"

namespace obf::passes {

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
