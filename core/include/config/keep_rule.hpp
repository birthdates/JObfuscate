#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace obf::config {

// ProGuard-style keep rules (supported subset):
//   -keep class com.foo.Bar
//   -keep class com.foo.* { *; }
//   -keep class com.foo.** { void onEnable(); int getCount(java.lang.String); }
//   -keepclassmembers class com.foo.Bar { private int secret; <init>(); }
// Class patterns: exact, `pkg.*` (direct package), `pkg.**` (subtree), `*`,
// or `*`/`?` wildcards in the dotted name. Member specs: `*;`, `<methods>;`,
// `<fields>;`, `<init>[ (args) ];`, `[type ]name[ (args) ]` with Java types
// (void, primitives, `*`/`***`, dotted classes, `[]` arrays) and `...` args.
enum class KeepDirective {
    Keep,             // keep class (+members) as entry points: no shrink, no rename
    KeepClassMembers  // keep matching members if the class survives (names kept)
};

enum class KeepMemberKind {
    All,          // `*;`
    AllMethods,   // `<methods>;`
    AllFields,    // `<fields>;`
    Constructors, // `<init>` with optional exact args
    Method,       // named, optional return type + args
    Field,        // named, optional type
    NamedAny      // bare `name;` — matches any method overload or field
};

struct KeepMemberSpec {
    KeepMemberKind kind{KeepMemberKind::All};
    std::string name;            // method/field name, "*" for any (Method/Field/NamedAny kinds)
    std::string return_type;     // Java type, "", "*" or "***" = any (Method kind)
    std::vector<std::string> param_types; // Java types (Method kind)
    bool any_params{true};       // true when no explicit (args) list was given
    std::string field_type;      // Java type, ""/"*"/"***" = any (Field kind)
};

struct KeepRule {
    KeepDirective directive{KeepDirective::Keep};
    std::string class_pattern; // dotted pattern as written
    std::vector<KeepMemberSpec> members; // empty = all members
};

// Parse one rule line ("-keep class ..." with or without the leading dash).
// Returns nullopt and sets error on syntax failure.
[[nodiscard]] std::optional<KeepRule> parse_keep_rule(std::string_view line, std::string& error);

// Read a ProGuard-style keep file: `#` comments, blank lines skipped, rules
// may span lines until braces balance. Appends parsed rules to out.
bool read_keep_file(const std::string& path, std::vector<std::string>& out_rules, std::string& error);

// Match helpers. Class names are internal (`com/foo/Bar`); patterns dotted.
[[nodiscard]] bool keep_rule_matches_class(const KeepRule& rule, std::string_view internal_name);
[[nodiscard]] bool keep_rule_matches_method(const KeepRule& rule,
                                            std::string_view method_name,
                                            std::string_view method_desc);
[[nodiscard]] bool keep_rule_matches_field(const KeepRule& rule,
                                           std::string_view field_name,
                                           std::string_view field_desc);

// Split a JVM method descriptor into param descriptors + return descriptor.
bool split_jvm_method_desc(std::string_view desc,
                           std::vector<std::string>& out_params,
                           std::string& out_ret);

} // namespace obf::config
