// The part that the two GCC plugins of this directory share: the files of the
// source root, the rule table, the opt-out regions, and the contract rule of
// the tree.
//
// contract.cpp builds crucible_contract.so, which applies only the contract
// rule, and a build loads it when CRUCIBLE_QUARANTINE is OFF.  quarantine.cpp
// builds crucible_quarantine.so, which applies the quarantine rule, the
// include rules and the contract rule, and a build loads it when
// CRUCIBLE_QUARANTINE is REPORT or ERROR.  A change of quarantine.cpp
// therefore does not change the plugin of a build where CRUCIBLE_QUARANTINE is
// OFF, and its objects do not compile again.
//
// THE RULE TABLE
//     utils/scripts/layer-rules.txt gives the layers of the base, the headers
//     that each layer can include, the door of each system header, the
//     admitted entities of the standard library, the quarantined directories
//     and the enforce mode of each path.  Its head comment gives the format,
//     and utils/scripts/layer_rules.py reads the same format.  The quarantine
//     plugin takes the table as rules=PATH.  The contract plugin takes no
//     table.
//
// THE FILES
//     Each file has one class, from its real path:
//         base          a file that a layer row of the table holds
//         quarantined   a file that a quarantine row of the table holds
//         generated     a file of the build directory, which the build writes
//         unclassified  a file under the source root that no row holds
//         outside       a system header, a file outside the source root, or
//                       a scratch buffer
//     The longest path of a layer row or a quarantine row decides.  The
//     quarantine plugin gives an error for each unclassified file that a unit
//     reads.  The contract plugin reads no table, and it gives each file of
//     the source root outside the build directory the quarantined class.
//     The contract rule and the opt-out regions apply to each file under the
//     source root.  The quarantine rule applies to a quarantined file only.
//     A place of the quarantine rule falls through a generated file to the
//     point where its macro expands, as it falls through a system header.
//     Each plugin takes the build directory as build=PATH.
//
// THE CONTRACT RULE
//     A P2900 contract specifier (`pre` or `post` on a function declaration)
//     is not permitted, and CRUCIBLE_PRE and CRUCIBLE_POST of
//     foundation/contracts/ replace it.  GCC 16 does not keep the specifier of
//     a template in a header unit or a precompiled header, and a constant
//     evaluation can ignore the specifier (CLAUDE.md section XII).  The rule
//     applies to each file under the source root, of each class.  Each
//     specifier that no region opts out is an error.
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
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

// The class of a file, as THE FILES above gives it.
enum class FileClass : std::uint8_t {
    outside,
    generated,
    base,
    quarantined,
    unclassified,
};

struct FileEntry {
    FileClass file_class = FileClass::outside;
    std::string relative;  // empty when the file is outside the root
    std::string real;  // the real path, empty for a scratch buffer
    int layer = -1;  // the index of the layer row of a base file
    bool is_error_mode = false;  // the enforce mode of the file is error
};

// The files that can own a place.
enum class Scope : std::uint8_t {
    quarantine,  // a base or a quarantined file: the place of a finding of the quarantine rule
    tree,  // each file under the root: the place of a pragma of an opt-out region
};

inline bool owns_place(FileClass file_class, Scope scope) {
    switch (file_class) {
        case FileClass::base:
        case FileClass::quarantined:
            return true;
        case FileClass::generated:
        case FileClass::unclassified:
            return scope == Scope::tree;
        case FileClass::outside:
            return false;
    }
    return false;
}

// ── The rule table ──────────────────────────────────────────────────────

struct LayerRow {
    std::string name;
    int rank = 0;
    std::vector<std::string> paths;
};

struct DoorRow {
    std::string header;  // the name inside the angle brackets
    std::string owner;
};

struct EnforceRow {
    std::string path;
    bool is_error = false;
};

// The rows of the rule table, in the order of the file.  A header keeps the
// name inside its angle brackets.
struct RuleTable {
    bool is_loaded = false;
    std::vector<LayerRow> layers;
    std::vector<std::pair<int, std::string>> allows;  // the layer index and the header
    std::vector<DoorRow> doors;
    std::vector<std::string> admitted_names;
    std::vector<std::string> admitted_headers;  // "type_traits": the name inside the angle brackets
    std::vector<std::string> quarantines;
    std::vector<EnforceRow> enforces;
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
    RuleTable table;

    // One entry for each real path.  The empty path holds the entry of each
    // scratch buffer.  A node of an unordered map keeps its address, so the
    // pointers of `files` and `unclassified` stay valid.
    std::unordered_map<std::string, FileEntry> entries;
    std::unordered_map<const char*, const FileEntry*> files;  // by the file name of a line map
    std::vector<const FileEntry*> unclassified;
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

// A path of a row holds a file: a directory holds each file under it, and a
// file path holds that file only.
inline bool row_holds(const std::string& path, const std::string& relative) {
    return !path.empty() && path.back() == '/' ? has_prefix(relative, path) : relative == path;
}

// The class, the layer and the enforce mode of a file of the root that is not
// generated.  The longest path decides.  Complexity: O(rows).
inline void classify_by_table(FileEntry& entry) {
    std::size_t best_length = 0;
    bool is_found = false;
    for (std::size_t index = 0; index < core.table.layers.size(); ++index) {
        for (const std::string& path : core.table.layers[index].paths) {
            if (row_holds(path, entry.relative) && (!is_found || path.size() > best_length)) {
                entry.file_class = FileClass::base;
                entry.layer = static_cast<int>(index);
                best_length = path.size();
                is_found = true;
            }
        }
    }
    for (const std::string& path : core.table.quarantines) {
        if (row_holds(path, entry.relative) && (!is_found || path.size() > best_length)) {
            entry.file_class = FileClass::quarantined;
            entry.layer = -1;
            best_length = path.size();
            is_found = true;
        }
    }
    if (!is_found) {
        entry.file_class = FileClass::unclassified;
    }
    std::size_t mode_length = 0;
    bool has_mode = false;
    for (const EnforceRow& row : core.table.enforces) {
        if (row_holds(row.path, entry.relative) && (!has_mode || row.path.size() > mode_length)) {
            entry.is_error_mode = row.is_error;
            mode_length = row.path.size();
            has_mode = true;
        }
    }
}

// The entry of one real path, made and kept on the first request.
inline const FileEntry& entry_of_real_path(const std::string& resolved) {
    auto found = core.entries.find(resolved);
    if (found != core.entries.end()) {
        return found->second;
    }
    FileEntry entry;
    entry.real = resolved;
    if (!resolved.empty() && has_prefix(resolved, core.root + "/")) {
        entry.relative = resolved.substr(core.root.size() + 1);
        if (!core.build.empty() && has_prefix(resolved, core.build + "/")) {
            entry.file_class = FileClass::generated;
        } else if (core.table.is_loaded) {
            classify_by_table(entry);
        } else {
            entry.file_class = FileClass::quarantined;
        }
    }
    const FileEntry& stored = core.entries.emplace(resolved, std::move(entry)).first->second;
    if (stored.file_class == FileClass::unclassified) {
        core.unclassified.push_back(&stored);
    }
    return stored;
}

inline const FileEntry& classify_file(const char* file) {
    auto found = core.files.find(file);
    if (found != core.files.end()) {
        return *found->second;
    }
    std::string resolved = file != nullptr ? real_path(file) : std::string{};
    const FileEntry* entry = &entry_of_real_path(resolved);
    core.files.emplace(file, entry);
    return *entry;
}

// An error for each file of the root that the unit read and that no row of
// the rule table holds.
inline void report_unclassified() {
    for (const FileEntry* entry : core.unclassified) {
        error("quarantine: no layer row and no quarantine row of the rule table holds %qs; add a row for its "
              "directory to the table",
              entry->relative.c_str());
    }
}

// The place of a location is its spelling location.  When the file that
// spells it owns no place in SCOPE, as a system header owns none, the place is
// the expansion point of that macro, one level at a time.  A macro of fixy
// therefore owns what it spells, and a quarantined file owns each system macro
// that it uses.  A generated file owns a place only in the scope of the tree.
inline Place place_of(location_t location, Scope scope) {
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
            if (owns_place(entry.file_class, scope)) {
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
    if (entry.file_class == FileClass::outside) {
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
    // A pragma of a file outside the source root opens no region.  A pragma
    // of a generated file opens one, because the contract rule applies there.
    Place place = place_of(location, Scope::tree);
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
    Place place = place_of(location, Scope::tree);
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

// ── The reader of the rule table ────────────────────────────────────────

inline std::string trimmed(const std::string& text) {
    std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

inline std::vector<std::string> split_words(const std::string& text) {
    std::vector<std::string> words;
    std::size_t start = text.find_first_not_of(" \t");
    while (start != std::string::npos) {
        std::size_t end = text.find_first_of(" \t", start);
        words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        start = end == std::string::npos ? end : text.find_first_not_of(" \t", end);
    }
    return words;
}

inline bool is_plain_path(const std::string& path) {
    if (path.empty() || path.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    std::size_t length = path.back() == '/' ? path.size() - 1 : path.size();
    while (start <= length) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos || end > length) {
            end = length;
        }
        std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

inline bool is_header(const std::string& text) { return text.size() >= 3 && text.front() == '<' && text.back() == '>'; }

inline bool is_digits(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

// Reads the rule table at PATH into core.table.  The format and each check
// are those of utils/scripts/layer_rules.py, and check_plugin.py gives the two
// readers the same malformed tables.  Returns false after an error.
inline bool load_rule_table(const std::string& path) {
    FILE* stream = fopen(path.c_str(), "r");
    if (stream == nullptr) {
        error("quarantine: cannot read the rule table %s: %m", path.c_str());
        return false;
    }
    std::string text;
    char chunk[4096];
    for (std::size_t count = 0; (count = fread(chunk, 1, sizeof chunk, stream)) > 0;) {
        text.append(chunk, count);
    }
    fclose(stream);
    RuleTable table;
    std::vector<std::pair<std::string, std::string>> class_paths;  // a path of a layer or quarantine row, and its row
    std::vector<std::pair<std::string, int>> allow_layers;  // the layer name of each allow row, and its line
    std::vector<std::string> allow_headers;
    bool is_valid = true;
    int line_number = 0;
    auto refuse = [&](const char* message) {
        error("quarantine: %s:%d: %s", path.c_str(), line_number, message);
        is_valid = false;
    };
    auto add_class_path = [&](const std::string& class_path, const std::string& row) {
        for (const auto& [known, known_row] : class_paths) {
            if (known == class_path) {
                error("quarantine: %s:%d: the path %s is also in %s", path.c_str(), line_number, class_path.c_str(),
                      known_row.c_str());
                is_valid = false;
                return;
            }
        }
        class_paths.emplace_back(class_path, row);
    };
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? text.size() + 1 : end + 1;
        ++line_number;
        std::string content = trimmed(line);
        if (content.empty() || content[0] == '#') {
            continue;
        }
        std::size_t bar = content.find('|');
        std::vector<std::string> words = split_words(content.substr(0, bar));
        std::string reason = bar == std::string::npos ? std::string{} : trimmed(content.substr(bar + 1));
        if (words.empty()) {
            refuse("the row has a reason and no kind");
            continue;
        }
        const std::string& kind = words[0];
        std::vector<std::string> args(words.begin() + 1, words.end());
        if (kind == "layer") {
            if (args.size() < 3 || !is_digits(args[1])) {
                refuse("a layer row is 'layer NAME RANK PATH...', with a RANK of digits");
                continue;
            }
            LayerRow layer;
            layer.name = args[0];
            for (char digit : args[1]) {
                layer.rank = layer.rank * 10 + (digit - '0');
            }
            for (const LayerRow& known : table.layers) {
                if (known.name == layer.name) {
                    refuse("the layer has a second row");
                } else if (known.rank == layer.rank) {
                    refuse("the rank belongs to a second layer");
                }
            }
            for (std::size_t index = 2; index < args.size(); ++index) {
                if (!is_plain_path(args[index])) {
                    refuse("a path must be relative to the source root, with no '.', '..' or empty component");
                    continue;
                }
                add_class_path(args[index], "the layer " + layer.name);
                layer.paths.push_back(args[index]);
            }
            table.layers.push_back(std::move(layer));
        } else if (kind == "allow") {
            if (args.size() != 2 || !is_header(args[1])) {
                refuse("an allow row is 'allow LAYER HEADER', with a HEADER in angle brackets");
                continue;
            }
            allow_layers.emplace_back(args[0], line_number);
            allow_headers.push_back(args[1].substr(1, args[1].size() - 2));
        } else if (kind == "door") {
            if (args.size() != 2 || !is_header(args[0]) || !is_plain_path(args[1]) || args[1].back() == '/') {
                refuse("a door row is 'door HEADER OWNER', with a HEADER in angle brackets and one OWNER file");
                continue;
            }
            DoorRow door{args[0].substr(1, args[0].size() - 2), args[1]};
            for (const DoorRow& known : table.doors) {
                if (known.header == door.header) {
                    refuse("the header has a second door");
                }
            }
            table.doors.push_back(std::move(door));
        } else if (kind == "admit") {
            // The word after `until` names the family that replaces the entry.
            // It changes nothing in the plugin.
            if ((args.size() != 1 && args.size() != 3) || (args.size() == 3 && args[1] != "until") || reason.empty()) {
                refuse("an admit row is 'admit ENTRY | REASON' or 'admit ENTRY until FAMILY | REASON', and the reason "
                       "is necessary");
                continue;
            }
            if (is_header(args[0])) {
                table.admitted_headers.push_back(args[0].substr(1, args[0].size() - 2));
            } else {
                table.admitted_names.push_back(args[0]);
            }
        } else if (kind == "quarantine") {
            if (args.size() != 1 || !is_plain_path(args[0])) {
                refuse("a quarantine row is 'quarantine PATH', with a PATH relative to the source root");
                continue;
            }
            add_class_path(args[0], "a quarantine row");
            table.quarantines.push_back(args[0]);
        } else if (kind == "enforce") {
            if (args.size() != 2 || (args[1] != "report" && args[1] != "error") || !is_plain_path(args[0])) {
                refuse("an enforce row is 'enforce PATH MODE', and MODE is report or error");
                continue;
            }
            for (const EnforceRow& known : table.enforces) {
                if (known.path == args[0]) {
                    refuse("the path has a second enforce row");
                }
            }
            table.enforces.push_back(EnforceRow{args[0], args[1] == "error"});
        } else {
            refuse("the row kind is unknown; the kinds are layer, allow, door, admit, quarantine and enforce");
        }
    }
    for (std::size_t index = 0; index < allow_layers.size(); ++index) {
        int layer = -1;
        for (std::size_t known = 0; known < table.layers.size(); ++known) {
            if (table.layers[known].name == allow_layers[index].first) {
                layer = static_cast<int>(known);
            }
        }
        if (layer < 0) {
            error("quarantine: %s:%d: the allow row names the layer %s, and no layer row gives it", path.c_str(),
                  allow_layers[index].second, allow_layers[index].first.c_str());
            is_valid = false;
            continue;
        }
        table.allows.emplace_back(layer, allow_headers[index]);
    }
    if (!is_valid) {
        return false;
    }
    table.is_loaded = true;
    core.table = std::move(table);
    return true;
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

// Records the build directory, whose files are generated.  A plugin with no
// build directory gives no file the generated class.  Returns false, after an
// error, when the path does not exist.
inline bool set_build(const std::string& build_argument) {
    core.build = real_path(build_argument.c_str());
    if (core.build.empty()) {
        error("quarantine: the build directory %qs of %<-fplugin-arg-%s-build=PATH%> does not exist",
              build_argument.c_str(), core.plugin_name.c_str());
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
