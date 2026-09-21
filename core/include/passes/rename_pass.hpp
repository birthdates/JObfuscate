#pragma once

#include "pass.hpp"
#include "symbol_mapping.hpp"

namespace obf::passes {

class RenamePass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Scrambles class, field, and method names while preserving hierarchy and public APIs";
    }

    bool run_on_archive(PassContext& ctx) override;

private:
    void generate_mappings(PassContext& ctx);
    void apply_mappings_to_class(bytecode::ClassFile& cls, PassContext& ctx);
    void update_manifest(PassContext& ctx);
    void remap_resources(PassContext& ctx);
    void export_mapping(PassContext& ctx);

    std::unordered_map<std::string, std::string> m_annotation_element_renames;
    std::vector<ClassMappingRecord> m_mapping_records;

public:
    [[nodiscard]] static std::string generate_obfuscated_name(size_t index, config::RenameMode mode, uint64_t salt = 0);
};

} // namespace obf::passes
