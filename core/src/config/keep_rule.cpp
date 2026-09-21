#include "config/keep_rule.hpp"
#include <cctype>
#include <fstream>
#include <sstream>

namespace obf::config {

namespace {

std::string trim(std::string_view s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string to_dotted(std::string_view name) {
    std::string s(name);
    for (char& c : s) {
        if (c == '/') c = '.';
    }
    return s;
}

// Glob `*`/`?` match on the dotted name (`*` crosses dots).
bool glob_match(std::string_view pat, std::string_view s) {
    size_t px = 0, sx = 0, star = std::string_view::npos, ss = 0;
    while (sx < s.size()) {
        if (px < pat.size() && (pat[px] == '?' || pat[px] == s[sx])) {
            ++px;
            ++sx;
        } else if (px < pat.size() && pat[px] == '*') {
            star = px++;
            ss = sx;
        } else if (star != std::string_view::npos) {
            px = star + 1;
            sx = ++ss;
        } else {
            return false;
        }
    }
    while (px < pat.size() && pat[px] == '*') ++px;
    return px == pat.size();
}

// Map a Java type name to its JVM descriptor. Returns "" when not mappable
// ("*" and "***" map to "*" = wildcard marker).
std::string java_type_to_jvm(std::string_view t) {
    std::string s = trim(t);
    if (s.empty() || s == "*" || s == "***") return "*";
    // Array suffixes.
    int dims = 0;
    while (s.size() > 2 && s.substr(s.size() - 2) == "[]") {
        s.erase(s.size() - 2);
        ++dims;
    }
    std::string base;
    if (s == "void") base = "V";
    else if (s == "boolean") base = "Z";
    else if (s == "byte") base = "B";
    else if (s == "char") base = "C";
    else if (s == "short") base = "S";
    else if (s == "int") base = "I";
    else if (s == "float") base = "F";
    else if (s == "long") base = "J";
    else if (s == "double") base = "D";
    else {
        // Dotted (or slash-separated) class name. Inner classes must use the
        // binary `$` form (e.g. com.foo.Outer$Inner).
        for (char& c : s) {
            if (c == '.') c = '/';
        }
        base = "L" + s + ";";
    }
    return std::string(dims, '[') + base;
}

bool spec_type_matches_jvm(std::string_view spec_type, std::string_view jvm_desc) {
    std::string t = trim(spec_type);
    if (t.empty() || t == "*" || t == "***") return true;
    return java_type_to_jvm(t) == jvm_desc;
}

std::vector<std::string> split_top_level(std::string_view s, char delim) {
    std::vector<std::string> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(' || s[i] == '<') ++depth;
        else if (s[i] == ')' || s[i] == '>') --depth;
        else if (s[i] == delim && depth == 0) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

std::vector<std::string> split_args(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    int angle = 0;
    for (char c : s) {
        if (c == '<') ++angle;
        if (c == '>') --angle;
        if (c == ',' && angle == 0) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
}

bool parse_member_item(const std::string& item, KeepMemberSpec& spec, std::string& error) {
    if (item == "*") {
        spec.kind = KeepMemberKind::All;
        return true;
    }
    if (item == "<methods>") {
        spec.kind = KeepMemberKind::AllMethods;
        return true;
    }
    if (item == "<fields>") {
        spec.kind = KeepMemberKind::AllFields;
        return true;
    }
    // [rettype ]name[(args)]
    std::string head = item;
    std::string arglist;
    bool has_parens = false;
    auto paren = item.find('(');
    if (paren != std::string::npos) {
        if (item.back() != ')') {
            error = "unbalanced parentheses in member spec '" + item + "'";
            return false;
        }
        head = trim(item.substr(0, paren));
        arglist = trim(item.substr(paren + 1, item.size() - paren - 2));
        has_parens = true;
    }
    // Split head into optional type + name at the last space outside <>.
    std::string type;
    std::string name = head;
    {
        int angle = 0;
        size_t split = std::string::npos;
        for (size_t i = 0; i < head.size(); ++i) {
            if (head[i] == '<') ++angle;
            else if (head[i] == '>') --angle;
            else if (head[i] == ' ' && angle == 0) split = i;
        }
        if (split != std::string::npos) {
            type = trim(head.substr(0, split));
            name = trim(head.substr(split + 1));
        }
    }
    if (name.empty() || name.find(' ') != std::string::npos) {
        error = "bad member name in spec '" + item + "'";
        return false;
    }
    if (name == "<init>" || has_parens) {
        spec.kind = KeepMemberKind::Method;
        spec.name = name;
        spec.return_type = type;
        if (!has_parens) {
            spec.any_params = true;
        } else if (arglist.empty()) {
            spec.any_params = false;
        } else {
            spec.any_params = false;
            auto args = split_args(arglist);
            if (args.size() == 1 && trim(args[0]) == "...") {
                spec.any_params = true;
            } else {
                // Allow a trailing "..." (e.g. "int, ...").
                for (auto& a : args) {
                    a = trim(a);
                    if (a == "...") {
                        spec.any_params = true;
                    } else if (!a.empty()) {
                        spec.param_types.push_back(a);
                    }
                }
                if (spec.any_params && !spec.param_types.empty()) {
                    // prefix match handled at match time via any_params flag
                    // combined with leading fixed params
                }
            }
        }
        if (name == "<init>" && !type.empty() && type != "*" && type != "***") {
            error = "constructors take no return type in spec '" + item + "'";
            return false;
        }
        return true;
    }
    if (type.empty()) {
        // Bare `name;` matches any method overload or field of that name.
        spec.kind = KeepMemberKind::NamedAny;
        spec.name = name;
        return true;
    }
    spec.kind = KeepMemberKind::Field;
    spec.name = name;
    spec.field_type = type;
    return true;
}

} // anonymous namespace

std::optional<KeepRule> parse_keep_rule(std::string_view line, std::string& error) {
    std::string s = trim(line);
    if (!s.empty() && s.front() == '-') s = trim(s.substr(1));
    KeepRule rule;
    if (s.starts_with("keepclassmembers")) {
        rule.directive = KeepDirective::KeepClassMembers;
        s = trim(s.substr(std::string("keepclassmembers").size()));
    } else if (s.starts_with("keep")) {
        rule.directive = KeepDirective::Keep;
        s = trim(s.substr(std::string("keep").size()));
        // Guard against typo'd directives ("keepclassmemberz ..."): the
        // remainder must start with a class keyword or be a bare pattern
        // without whitespace.
        auto is_class_kw = [](std::string_view h) {
            for (const char* kw : {"@interface", "interface", "enum", "class"}) {
                std::string k(kw);
                if (h == k || h.starts_with(k + " ") || h.starts_with(k + "\t")) return true;
            }
            return false;
        };
        std::string head = s.substr(0, s.find('{'));
        head = trim(head);
        if (!is_class_kw(head) && head.find_first_of(" \t") != std::string::npos) {
            error = "unknown directive (want -keep or -keepclassmembers): '" + std::string(line) + "'";
            return std::nullopt;
        }
    } else if (s.starts_with("class") || s.starts_with("interface") || s.starts_with("enum") || s.starts_with("@interface")) {
        // Bare `class ...` (e.g. via --keep 'class Foo'): defaults to -keep.
        rule.directive = KeepDirective::Keep;
    } else {
        error = "rule must start with -keep or -keepclassmembers: '" + std::string(line) + "'";
        return std::nullopt;
    }
    // Optional class-kind keyword (accepted, no distinct semantics in v1).
    for (const char* kw : {"@interface", "interface", "enum", "class"}) {
        std::string k(kw);
        if (s == k || s.starts_with(k + " ") || s.starts_with(k + "\t")) {
            s = trim(s.substr(k.size()));
            break;
        }
    }
    // Split "pattern { members }" — braces optional (bare pattern = all members).
    std::string pattern = s;
    std::string body;
    auto open = s.find('{');
    if (open != std::string::npos) {
        if (s.back() != '}') {
            error = "missing closing '}' in rule: '" + std::string(line) + "'";
            return std::nullopt;
        }
        pattern = trim(s.substr(0, open));
        body = trim(s.substr(open + 1, s.size() - open - 2));
    }
    if (pattern.empty()) {
        error = "missing class pattern in rule: '" + std::string(line) + "'";
        return std::nullopt;
    }
    if (pattern.find('{') != std::string::npos || pattern.find('}') != std::string::npos ||
        pattern.find(';') != std::string::npos) {
        error = "bad class pattern '" + pattern + "'";
        return std::nullopt;
    }
    rule.class_pattern = pattern;
    if (!body.empty()) {
        for (const auto& item : split_top_level(body, ';')) {
            if (item.empty()) continue;
            KeepMemberSpec spec;
            if (!parse_member_item(item, spec, error)) return std::nullopt;
            rule.members.push_back(std::move(spec));
        }
    }
    return rule;
}

bool read_keep_file(const std::string& path, std::vector<std::string>& out_rules, std::string& error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "could not open keep file: " + path;
        return false;
    }
    std::string logical;
    int depth = 0;
    std::string line;
    auto flush = [&]() {
        std::string t = trim(logical);
        if (!t.empty()) out_rules.push_back(t);
        logical.clear();
    };
    while (std::getline(file, line)) {
        // Strip `#` comments.
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        for (char c : line) {
            if (c == '{') ++depth;
            else if (c == '}') --depth;
        }
        if (depth < 0) {
            error = "unbalanced '}' in keep file: " + path;
            return false;
        }
        if (!logical.empty()) logical += ' ';
        logical += trim(line);
        if (depth == 0) flush();
    }
    if (depth != 0) {
        error = "unbalanced '{' in keep file: " + path;
        return false;
    }
    flush();
    return true;
}

bool keep_rule_matches_class(const KeepRule& rule, std::string_view internal_name) {
    std::string dotted = to_dotted(internal_name);
    const std::string& pat = rule.class_pattern;
    if (pat == "*") return true;
    if (pat.size() > 3 && pat.substr(pat.size() - 3) == ".**") {
        std::string prefix = pat.substr(0, pat.size() - 2); // keep trailing dot
        return dotted.starts_with(prefix);
    }
    if (pat.size() > 2 && pat.substr(pat.size() - 2) == ".*") {
        std::string prefix = pat.substr(0, pat.size() - 1); // keep trailing dot
        if (!dotted.starts_with(prefix)) return false;
        return dotted.find('.', prefix.size()) == std::string::npos;
    }
    if (pat.find('*') != std::string::npos || pat.find('?') != std::string::npos) {
        return glob_match(pat, dotted);
    }
    return dotted == to_dotted(pat);
}

bool split_jvm_method_desc(std::string_view desc,
                           std::vector<std::string>& out_params,
                           std::string& out_ret) {
    out_params.clear();
    out_ret.clear();
    if (desc.empty() || desc.front() != '(') return false;
    size_t i = 1;
    while (i < desc.size() && desc[i] != ')') {
        size_t start = i;
        while (i < desc.size() && desc[i] == '[') ++i;
        if (i >= desc.size()) return false;
        if (desc[i] == 'L') {
            size_t semi = desc.find(';', i);
            if (semi == std::string_view::npos) return false;
            i = semi + 1;
        } else {
            ++i;
        }
        out_params.emplace_back(desc.substr(start, i - start));
    }
    if (i >= desc.size() || desc[i] != ')') return false;
    out_ret = std::string(desc.substr(i + 1));
    return !out_ret.empty();
}

namespace {

bool member_name_matches(std::string_view spec_name, std::string_view actual) {
    if (spec_name == "*") return true;
    if (spec_name.find('*') != std::string::npos || spec_name.find('?') != std::string::npos) {
        return glob_match(spec_name, actual);
    }
    return spec_name == actual;
}

bool spec_matches_method(const KeepMemberSpec& spec,
                         std::string_view method_name,
                         std::string_view method_desc) {
    switch (spec.kind) {
        case KeepMemberKind::All:
        case KeepMemberKind::AllMethods:
            return true;
        case KeepMemberKind::AllFields:
            return false;
        case KeepMemberKind::Constructors:
            if (method_name != "<init>") return false;
            break;
        case KeepMemberKind::NamedAny:
            return member_name_matches(spec.name, method_name);
        case KeepMemberKind::Method:
            if (!member_name_matches(spec.name, method_name)) return false;
            break;
        case KeepMemberKind::Field:
            return false;
    }
    // Descriptor check (constructors + named methods).
    if (spec.any_params && spec.return_type.empty()) return true;
    std::vector<std::string> params;
    std::string ret;
    if (!split_jvm_method_desc(method_desc, params, ret)) return false;
    if (!spec.return_type.empty() && !spec_type_matches_jvm(spec.return_type, ret)) return false;
    if (spec.any_params) {
        // Leading fixed params (from "T, ..." form) must match; rest free.
        if (params.size() < spec.param_types.size()) return false;
        for (size_t i = 0; i < spec.param_types.size(); ++i) {
            if (!spec_type_matches_jvm(spec.param_types[i], params[i])) return false;
        }
        return true;
    }
    if (params.size() != spec.param_types.size()) return false;
    for (size_t i = 0; i < params.size(); ++i) {
        if (!spec_type_matches_jvm(spec.param_types[i], params[i])) return false;
    }
    return true;
}

bool spec_matches_field(const KeepMemberSpec& spec,
                        std::string_view field_name,
                        std::string_view field_desc) {
    switch (spec.kind) {
        case KeepMemberKind::All:
        case KeepMemberKind::AllFields:
            return true;
        case KeepMemberKind::AllMethods:
        case KeepMemberKind::Constructors:
        case KeepMemberKind::Method:
            return false;
        case KeepMemberKind::NamedAny:
            return member_name_matches(spec.name, field_name);
        case KeepMemberKind::Field:
            break;
    }
    if (!member_name_matches(spec.name, field_name)) return false;
    if (!spec.field_type.empty() && !field_desc.empty()) {
        return spec_type_matches_jvm(spec.field_type, field_desc);
    }
    return true;
}

} // anonymous namespace

bool keep_rule_matches_method(const KeepRule& rule,
                              std::string_view method_name,
                              std::string_view method_desc) {
    if (rule.members.empty()) return true;
    for (const auto& spec : rule.members) {
        if (spec_matches_method(spec, method_name, method_desc)) return true;
    }
    return false;
}

bool keep_rule_matches_field(const KeepRule& rule,
                             std::string_view field_name,
                             std::string_view field_desc) {
    if (rule.members.empty()) return true;
    for (const auto& spec : rule.members) {
        if (spec_matches_field(spec, field_name, field_desc)) return true;
    }
    return false;
}

} // namespace obf::config
