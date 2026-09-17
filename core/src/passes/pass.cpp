#include "passes/pass.hpp"
#include "jar/jar_archive.hpp"

namespace obf::passes {

void ClassHierarchy::add_class(const std::string& name, const std::string& super_name, const std::vector<std::string>& ifaces) {
    auto& node = m_nodes[name];
    node.class_name = name;
    node.super_class = super_name;
    node.interfaces = ifaces;

    if (!super_name.empty()) {
        m_nodes[super_name].subclasses.insert(name);
    }
    for (const auto& iface : ifaces) {
        m_nodes[iface].subclasses.insert(name);
    }
}

bool ClassHierarchy::is_subclass_of(const std::string& sub, const std::string& base) const {
    if (sub == base) return true;
    auto it = m_nodes.find(sub);
    if (it == m_nodes.end()) return false;

    if (!it->second.super_class.empty() && is_subclass_of(it->second.super_class, base)) {
        return true;
    }
    for (const auto& iface : it->second.interfaces) {
        if (is_subclass_of(iface, base)) return true;
    }
    return false;
}

const ClassHierarchyNode* ClassHierarchy::get_node(const std::string& name) const {
    auto it = m_nodes.find(name);
    return it != m_nodes.end() ? &it->second : nullptr;
}

bool ClassHierarchy::is_method_overridden_in_hierarchy(const std::string& class_name, const std::string& /*method_name*/, const std::string& /*desc*/) const {
    // If method is in base class or interface, consider it an override
    const auto* node = get_node(class_name);
    if (!node) return false;

    // Check super class
    if (!node->super_class.empty() && node->super_class != "java/lang/Object") {
        return true;
    }
    if (!node->interfaces.empty()) {
        return true;
    }
    return !node->subclasses.empty();
}

bool IPass::run_on_archive(PassContext& ctx) {
    bool ok = true;
    size_t count = 0;
    size_t total = ctx.archive.classes().size();

    for (auto& [name, cf] : ctx.archive.classes()) {
        ctx.reporter.progress(this->name(), count++, total, name);
        if (!run_on_class(cf, ctx)) {
            ok = false;
        }
    }
    return ok;
}

bool IPass::run_on_class(bytecode::ClassFile& /*cls*/, PassContext& /*ctx*/) {
    return true;
}

} // namespace obf::passes
