// The part that the two GCC plugins of this directory share: the files of the
// source root, the opt-out regions, and the contract rule of the tree.
//
// contract.cpp builds crucible_contract.so, which applies only the contract
// rule, and a build loads it when CRUCIBLE_QUARANTINE is OFF.  quarantine.cpp
// builds crucible_quarantine.so, which applies the quarantine rule and the
// contract rule, and a build loads it when CRUCIBLE_QUARANTINE is REPORT or
// ERROR.  A change of quarantine.cpp therefore does not change the plugin of a
// build where CRUCIBLE_QUARANTINE is OFF, and its objects do not compile again.
//
// THE CONTRACT RULE
//     A P2900 contract specifier (`pre` or `post` on a function declaration)
//     is not permitted, and CRUCIBLE_PRE and CRUCIBLE_POST of
//     foundation/contracts/ replace it.  GCC 16 does not keep the specifier of
//     a template in a header unit or a precompiled header, and a constant
//     evaluation can ignore the specifier (CLAUDE.md section XII).  The rule
//     applies to each file under the source root, also to include/foundation/,
//     include/fixy/ and the build directory.  Each specifier that no region
//     opts out is an error.
//
//     PLUGIN_FINISH_DECL     each declaration outside a template, also a
//                            local one.
//     PLUGIN_FINISH_PARSE_FUNCTION
//                            each function definition, also a template, a
//                            lambda and a member of a local class.
//     PLUGIN_FINISH_UNIT     one walk of every namespace that a system header
//                            does not own.  It finds a template and a member of
//                            a class template that has no definition.
//
//     The rule cannot see a specifier in a preprocessor arm that the unit does
//     not compile, or on a member function of a local class in a template when
//     the class declares the function and does not define it.
//     utils/scripts/check-contract-form.py reads the parse tree of each
//     tracked file, and it finds these specifiers too.
//
// THE OPT-OUT REGION
//     #pragma crucible I_KNOW_WHAT_IM_DOING("reason") opens a region, and
//     #pragma crucible END_I_KNOW_WHAT_IM_DOING closes it.  A finding inside a
//     region is reported as opted_out and is not an error.
//
// Each plugin includes this header in its one translation unit, after the
// standard headers that the plugin uses, because the headers of GCC rename
// some functions of the C library.

#pragma once

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gcc-plugin.h"
#include "plugin-version.h"
#include "tree.h"
#include "cp/cp-tree.h"
#include "cp/contracts.h"
#include "c-family/c-pragma.h"
#include "diagnostic-core.h"
#include "input.h"
#include "stringpool.h"

namespace crucible_plugin {

// ── The files, the places and the findings ──────────────────────────────

// Where a location is spelled, as the source root sees it.
enum class FileClass : std::uint8_t {
    outside,  // a system header, a generated file, a scratch buffer
    substrate,  // include/foundation/ or include/fixy/
    quarantined,  // every other file under the root
};

struct FileEntry {
    FileClass file_class = FileClass::outside;
    bool is_under_root = false;  // also true for a file of the build directory
    std::string relative;  // empty when the file is not under the root
};

struct Place {
    FileClass file_class = FileClass::outside;
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    location_t spelling = UNKNOWN_LOCATION;
};

// The kind of a finding of the contract rule.  The quarantine plugin names
// the kinds of its own rule.
inline constexpr char kContractKind[] = "contract_specifier";

struct Finding {
    const char* kind = kContractKind;
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    // The spelling location, or for a contract specifier the location of the
    // tree, so that a diagnostic also names the macro expansion.
    location_t spelling = UNKNOWN_LOCATION;
    std::string entity;
};

struct Region {
    const FileEntry* file = nullptr;
    int begin_line = 0;
    int end_line = 0;  // zero while the region is open
    location_t begin = UNKNOWN_LOCATION;
};

// The state of one translation unit that the two rules share.
struct CoreState {
    std::string plugin_name;
    std::string root;  // the real path of the source root, without a final '/'
    std::string build;  // the real path of the build directory, or empty
    bool was_reported = false;

    std::unordered_map<const char*, FileEntry> files;
    std::vector<Finding> findings;
    std::unordered_set<std::string> finding_keys;
    std::vector<Region> regions;

    // A garbage collection can free a tree and reuse its address, so each
    // collection clears this set.  The rule does not depend on it for
    // correctness: a finding is recorded one time for each place.
    std::unordered_set<tree> contract_walked;
};

inline CoreState core;

// ── Paths and places ────────────────────────────────────────────────────

inline bool has_prefix(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

inline std::string real_path(const char* path) {
    std::string resolved(PATH_MAX + 1, '\0');
    if (::realpath(path, resolved.data()) == nullptr) {
        return {};
    }
    resolved.resize(std::strlen(resolved.c_str()));
    return resolved;
}

inline const FileEntry& classify_file(const char* file) {
    auto found = core.files.find(file);
    if (found != core.files.end()) {
        return found->second;
    }
    FileEntry entry;
    std::string resolved = file != nullptr ? real_path(file) : std::string{};
    if (!resolved.empty() && has_prefix(resolved, core.root + "/")) {
        entry.is_under_root = true;
        entry.relative = resolved.substr(core.root.size() + 1);
        if (core.build.empty() || !has_prefix(resolved, core.build + "/")) {
            bool is_substrate =
                has_prefix(entry.relative, "include/foundation/") || has_prefix(entry.relative, "include/fixy/");
            entry.file_class = is_substrate ? FileClass::substrate : FileClass::quarantined;
        }
    }
    return core.files.emplace(file, std::move(entry)).first->second;
}

// The place of a location is its spelling location.  When the spelling is
// outside the source root, as in the body of a macro of a system header, the
// place is the expansion point of that macro, one level at a time.  A macro of
// fixy therefore owns what it spells, and a quarantined file owns each system
// macro that it uses.
inline Place place_of(location_t location) {
    Place place;
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION) {
        return place;
    }
    location_t current = location;
    for (int depth = 0; depth < 64; ++depth) {
        location_t spelled = linemap_resolve_location(line_table, current, LRK_SPELLING_LOCATION, nullptr);
        expanded_location where = expand_location(spelled);
        if (where.file != nullptr) {
            const FileEntry& entry = classify_file(where.file);
            if (entry.file_class != FileClass::outside) {
                place.file_class = entry.file_class;
                place.file = &entry;
                place.line = where.line;
                place.column = where.column;
                place.spelling = spelled;
                return place;
            }
        }
        if (!linemap_location_from_macro_expansion_p(line_table, current)) {
            return place;
        }
        const line_map* map = linemap_lookup(line_table, current);
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return place;
}

// ── The contract rule ───────────────────────────────────────────────────

// One contract specifier.  The spelling location decides whether the rule
// applies, and one finding stays for each spelling: a template and its
// instantiations share the specifier, and so do the uses of one macro.
inline void record_contract(bool is_precondition, location_t location) {
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION || in_system_header_at(location)) {
        return;
    }
    location_t spelled = linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, nullptr);
    expanded_location where = expand_location(spelled);
    if (where.file == nullptr) {
        return;
    }
    const FileEntry& entry = classify_file(where.file);
    if (!entry.is_under_root) {
        return;
    }
    std::string key = std::string(kContractKind) + ' ' + entry.relative + ':' + std::to_string(where.line) + ':'
                    + std::to_string(where.column);
    if (!core.finding_keys.insert(key).second) {
        return;
    }
    Finding finding;
    finding.kind = kContractKind;
    finding.file = &entry;
    finding.line = where.line;
    finding.column = where.column;
    finding.spelling = location;
    finding.entity = is_precondition ? "pre" : "post";
    core.findings.push_back(std::move(finding));
}

// Each contract specifier of a function.  The front end keeps the specifiers
// of a declaration in a table of its own, and each node of the list holds one
// PRECONDITION_STMT or POSTCONDITION_STMT.
inline void check_contracts(tree decl) {
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    if (decl == NULL_TREE || TREE_CODE(decl) != FUNCTION_DECL) {
        return;
    }
    for (tree specifier = get_fn_contract_specifiers(decl); specifier != NULL_TREE; specifier = TREE_CHAIN(specifier)) {
        if (TREE_CODE(specifier) != TREE_LIST || TREE_VALUE(specifier) == NULL_TREE
            || TREE_CODE(TREE_VALUE(specifier)) != TREE_LIST) {
            continue;
        }
        tree statement = CONTRACT_STATEMENT(specifier);
        if (statement == NULL_TREE || !CONTRACT_CONDITION_P(statement)) {
            continue;
        }
        location_t location = EXPR_LOCATION(statement);
        record_contract(PRECONDITION_P(statement),
                        location != UNKNOWN_LOCATION ? location : DECL_SOURCE_LOCATION(decl));
    }
}

// The anonymous namespace of the unit is not a library namespace, even when a
// system header opens it first.
inline bool is_library_namespace(tree ns) {
    return ns == std_node || (DECL_NAME(ns) != NULL_TREE && in_system_header_at(DECL_SOURCE_LOCATION(ns)));
}

inline void walk_contract_class(tree type);

inline void walk_contract_template(tree template_decl) {
    if (!core.contract_walked.insert(template_decl).second) {
        return;
    }
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result == NULL_TREE) {
        return;
    }
    if (TREE_CODE(result) == FUNCTION_DECL) {
        check_contracts(result);
        return;
    }
    if (TREE_CODE(result) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(result) || !CLASS_TYPE_P(TREE_TYPE(result))) {
        return;
    }
    walk_contract_class(TREE_TYPE(result));
    // The list of a class template holds its partial specializations.
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
            walk_contract_template(partial);
        }
    }
}

inline void walk_contract_decl(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)
        || in_system_header_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case FUNCTION_DECL:
            check_contracts(decl);
            break;
        case TEMPLATE_DECL:
            walk_contract_template(decl);
            break;
        case TYPE_DECL:
            if (DECL_IMPLICIT_TYPEDEF_P(decl) && !DECL_SELF_REFERENCE_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                walk_contract_class(TREE_TYPE(decl));
            }
            break;
        default:
            break;
    }
}

// The members of a class, its nested classes and member templates, and the
// friends that a class template declares.  An implicit instantiation is not
// walked: it shares each specifier with its template.
inline void walk_contract_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type) || !core.contract_walked.insert(main_type).second) {
        return;
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == FIELD_DECL) {
            if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                walk_contract_class(TREE_TYPE(member));
            }
            continue;
        }
        walk_contract_decl(member);
    }
    for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        if (TREE_PURPOSE(entry) == NULL_TREE && TREE_VALUE(entry) != NULL_TREE && DECL_P(TREE_VALUE(entry))) {
            walk_contract_decl(TREE_VALUE(entry));
        }
    }
}

// One walk of each namespace that a system header does not own.  The two
// other hooks of the rule see each function definition and each declaration
// outside a template.  This walk finds a function template, a member template
// and a member of a class template that has no definition.
inline void walk_contract_namespace(tree ns) {
    if (!core.contract_walked.insert(ns).second) {
        return;
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        tree member = decl;
        if (TREE_CODE(member) == TREE_LIST) {
            member = TREE_VALUE(member);
            if (member == NULL_TREE || TREE_CODE(member) == TREE_LIST) {
                continue;
            }
        }
        if (TREE_CODE(member) == OVERLOAD) {
            for (ovl_iterator candidate(member, true); candidate; ++candidate) {
                walk_contract_decl(*candidate);
            }
            continue;
        }
        if (TREE_CODE(member) == NAMESPACE_DECL) {
            if (DECL_NAMESPACE_ALIAS(member) == NULL_TREE && !is_library_namespace(member)) {
                walk_contract_namespace(member);
            }
            continue;
        }
        walk_contract_decl(member);
    }
}

// The diagnostic of one contract specifier that no region opts out.
inline void report_contract(const Finding& finding) {
    bool is_precondition = finding.entity == "pre";
    error_at(finding.spelling, "the %qs contract specifier is not permitted in this tree: write %<%s%> of %<%s%> %s",
             finding.entity.c_str(), is_precondition ? "CRUCIBLE_PRE(condition)" : "CRUCIBLE_POST(result, condition)",
             is_precondition ? "foundation/contracts/Pre.h" : "foundation/contracts/Post.h",
             is_precondition ? "as the first statement of the function body" : "before each return statement");
    inform(finding.spelling,
           "GCC 16 does not keep the contract specifier of a template in a header unit or in a precompiled "
           "header, and a constant evaluation can ignore the specifier (CLAUDE.md section XII)");
    inform(finding.spelling, "a test of the specifier itself puts it in a %<#pragma crucible %s(\"reason\")%> region",
           "I_KNOW_WHAT_IM_DOING");
}

inline void on_finish_decl(void* gcc_data, void*) { check_contracts(static_cast<tree>(gcc_data)); }

inline void on_finish_parse_function(void* gcc_data, void*) { check_contracts(static_cast<tree>(gcc_data)); }

// ── The opt-out pragmas ─────────────────────────────────────────────────

inline constexpr char kBeginPragma[] = "I_KNOW_WHAT_IM_DOING";
inline constexpr char kEndPragma[] = "END_I_KNOW_WHAT_IM_DOING";

inline void handle_begin_pragma(cpp_reader*) {
    tree value = NULL_TREE;
    location_t open_location = UNKNOWN_LOCATION;
    location_t token_location = UNKNOWN_LOCATION;
    bool has_reason = false;
    if (pragma_lex(&value, &open_location) == CPP_OPEN_PAREN) {
        cpp_ttype token = pragma_lex(&value, &token_location);
        if (token == CPP_STRING && value != NULL_TREE && TREE_CODE(value) == STRING_CST
            && TREE_STRING_LENGTH(value) > 1) {
            has_reason = pragma_lex(&value, &token_location) == CPP_CLOSE_PAREN
                      && pragma_lex(&value, &token_location) == CPP_EOF;
        }
    }
    location_t location = open_location != UNKNOWN_LOCATION ? open_location : input_location;
    if (!has_reason) {
        error_at(location,
                 "%<#pragma crucible %s%> takes one string that gives the reason: "
                 "write %<#pragma crucible %s(\"reason\")%>",
                 kBeginPragma, kBeginPragma);
        return;
    }
    // A pragma of a file outside the source root opens no region.
    Place place = place_of(location);
    if (place.file == nullptr) {
        return;
    }
    if (!core.regions.empty() && core.regions.back().end_line == 0) {
        error_at(location,
                 "a %<#pragma crucible %s%> region is open at line %d; close it with %<#pragma crucible %s%> "
                 "before you open a new region",
                 kBeginPragma, core.regions.back().begin_line, kEndPragma);
        return;
    }
    Region region;
    region.file = place.file;
    region.begin_line = place.line;
    region.begin = location;
    core.regions.push_back(region);
}

inline void handle_end_pragma(cpp_reader*) {
    tree value = NULL_TREE;
    location_t location = UNKNOWN_LOCATION;
    cpp_ttype token = pragma_lex(&value, &location);
    if (location == UNKNOWN_LOCATION) {
        location = input_location;
    }
    if (token != CPP_EOF) {
        error_at(location, "%<#pragma crucible %s%> takes no argument", kEndPragma);
        return;
    }
    // A pragma of a file outside the source root closes nothing, as the begin
    // pragma of such a file opens nothing.
    Place place = place_of(location);
    if (place.file == nullptr) {
        return;
    }
    if (core.regions.empty() || core.regions.back().end_line != 0) {
        error_at(location,
                 "%<#pragma crucible %s%> has no open region; open one with "
                 "%<#pragma crucible %s(\"reason\")%>",
                 kEndPragma, kBeginPragma);
        return;
    }
    Region& region = core.regions.back();
    if (place.file != region.file) {
        error_at(location,
                 "%<#pragma crucible %s%> closes a region that another file opened; "
                 "close each region in the file that opens it",
                 kEndPragma);
        return;
    }
    region.end_line = place.line;
}

inline void register_pragmas(void*, void*) {
    c_register_pragma("crucible", kBeginPragma, handle_begin_pragma);
    c_register_pragma("crucible", kEndPragma, handle_end_pragma);
}

inline bool is_opted_out(const Finding& finding) {
    for (const Region& region : core.regions) {
        if (region.file == finding.file && region.begin_line <= finding.line
            && (region.end_line == 0 || finding.line <= region.end_line)) {
            return true;
        }
    }
    return false;
}

// An error for each region that the unit does not close.
inline void report_open_regions() {
    for (const Region& region : core.regions) {
        if (region.end_line == 0) {
            error_at(region.begin, "this %<#pragma crucible %s%> region has no %<#pragma crucible %s%>", kBeginPragma,
                     kEndPragma);
        }
    }
}

// ── The arguments that the two plugins share ────────────────────────────

// Records the source root.  Returns false, after an error, when the path does
// not exist.
inline bool set_root(const std::string& root_argument) {
    core.root = root_argument.empty() ? std::string{} : real_path(root_argument.c_str());
    if (core.root.empty()) {
        error("quarantine: give the source root with %<-fplugin-arg-%s-root=PATH%>; the path must exist",
              core.plugin_name.c_str());
        return false;
    }
    return true;
}

// Registers the hooks of the contract rule and of the opt-out pragmas.
inline void register_contract_rule(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRAGMAS, register_pragmas, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function, nullptr);
}

}  // namespace crucible_plugin
