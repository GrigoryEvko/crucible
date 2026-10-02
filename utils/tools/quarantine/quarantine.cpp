// The quarantine plugin of GCC.
//
// The rule table (utils/scripts/layer-rules.txt, THE RULE TABLE in
// plugin_core.h) gives the class of each file of the source root.  In a
// quarantined file, each object must have a type that fixy or foundation
// gives, and the code must not name a library entity that the table does not
// admit.  The plugin reads the C++ trees of the translation unit and records
// each place that breaks the rule.  The location rule decides what it checks,
// and the target does not: a use is a finding only when its spelling location
// is in a quarantined file.  A use inside a base file, and a fixy template
// that a crucible type instantiates, is not a finding.
//
// The include rules apply to each #include directive that a base or a
// quarantined file holds.  A base file can include a header of its own layer
// and of each lower layer, and each header outside the root that the allow
// rows give its layer.  A door header can have only its door as includer.
//
// The plugin also applies the contract rule of the tree, from plugin_core.h,
// in each mode: each P2900 contract specifier that no region opts out is an
// error.  Each build of the tree loads this plugin
// (utils/tools/quarantine/Quarantine.cmake).
//
// THE KINDS
//     std_entity            a named reference to a declaration in namespace std.
//                           A typedef or an alias template of namespace std that
//                           the type of a declaration names is one too, such as
//                           std::size_t
//     replace_pending:ENTRY a use that the admit row of ENTRY admits, when the
//                           row has `until FAMILY`, such as std::bit_cast.  Each
//                           such row has a kind of its own, so the ratchet holds
//                           the uses of each entry in each file and a new use of
//                           one entry cannot pass behind a removed use of another
//     std_object            a variable, data member, parameter or return type whose
//                           type holds a class or an enumeration of the standard
//                           library, also through a typedef of fixy, such as
//                           std::memory_order
//     c_library_object      the same, for a C struct, union or enumeration of a
//                           system header
//     raw_pointer_object    a variable, data member or parameter of type T*
//     raw_function_pointer  the same, for a pointer to a function or to a member
//     c_array_object        a variable or data member of type T[N]
//     raw_new_delete        a new-expression or a delete-expression
//     c_library_call        a use of a function or a variable of a system header
//                           in the global namespace, such as memcpy or optind,
//                           and a call of a builtin that has a function of the C
//                           library behind it, such as __builtin_memcpy
//     compiler_builtin      a call of each other builtin of GCC that the source
//                           spells, such as __builtin_trap or __atomic_load_n,
//                           __builtin_bit_cast and va_arg
//     inline_asm            an asm statement
//     assert_expansion      an expansion of the macro assert of a system header.
//                           libcpp reports the expansion, so the finding is the
//                           same with and without NDEBUG.  A token that the
//                           definition of assert spells, such as __assert_fail
//                           or __builtin_FILE, is no finding of its own
//     layer_header          an include of a header outside the root that the
//                           allow rows do not give the layer of the base file
//     door_header           an include of a door header by a file that is not
//                           its door
//     upward_include        an include of a header of a higher layer, or of a
//                           file outside the base, by a base file
//     contract_specifier    a `pre` or a `post` contract specifier (the contract
//                           rule, in each file under the root and in each mode)
//     opted_out             one of the kinds above, inside a region that
//                           CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason") opens
//     region                no finding: each region of a file under the root,
//                           with its reason, for the ledger of the regions
//                           (utils/scripts/check-quarantine-regions.py)
//
// THE HOOKS
//     PLUGIN_PRE_GENERICIZE  the body of each function that is not a template:
//                            local objects, calls, uses of variables, new and
//                            delete.  A template instantiation is skipped, because
//                            its pattern is read instead: a dependent name that
//                            only the instantiation resolves is not a named
//                            reference in the source.  The function of the
//                            dynamic initializers of the unit is read too.
//     PLUGIN_FINISH_UNIT     one walk of every namespace that a system header does
//                            not own: namespace-scope objects, classes and their
//                            data members, bases, aliases, function signatures,
//                            and the bodies of template patterns.  The contract
//                            rule has its own walk.
//     PLUGIN_FINISH          the report, if the unit did not reach its end.
//     PLUGIN_INCLUDE_FILE    each file that the preprocessor enters.  The file
//                            that an #include directive enters is the target
//                            of that directive.
//     the include callback of libcpp
//                            each #include directive, before the preprocessor
//                            looks for the file.  A directive that enters no
//                            file names a file that the unit entered before:
//                            the plugin finds it as libcpp does, beside the
//                            includer and in the search chain of the directive.
//     the macro callback of libcpp
//                            each expansion of a macro: the expansions of assert.
//     plugin_core.h gives the hooks of the contract rule and of the two opt-out
//     pragmas.
//
// THE ARGUMENTS (-fplugin-arg-crucible_quarantine-NAME=VALUE)
//     root=PATH      the source root (necessary)
//     build=PATH     a build directory under the root, whose files are generated
//     rules=PATH     the rule table (necessary)
//     mode=report    each finding is a line of the section of the object, a
//                    line of the report file of out=, or else a note.  A
//                    finding in a file whose enforce mode is error, and that
//                    no region opts out, is an error
//     mode=error     each finding that no region opts out is an error
//     out=DIR        also write the report of the unit to a file in DIR
//     stamps=DIR     the directory of the mode stamps (THE DEPENDENCIES)
//     stamp=TEXT     the head line of the section holds it; a new value makes
//                    the build system compile again
//
// THE DEPENDENCIES
//     utils/scripts/quarantine_stamps.py writes DIR/modes/PATH for each source
//     file of a row of the table, with the enforce mode of that file.  At
//     PLUGIN_FINISH_UNIT, the plugin adds the mode stamp of each file with a
//     finding that a mode can make an error to the dependency file of the
//     unit.  A file with no mode stamp adds DIR/enforce.txt.  A change of the
//     mode of a file then compiles again only the units with such a finding
//     in that file.  An opted-out finding and a finding of the contract rule
//     add nothing, because no mode changes them.
//
// THE SECTION
//     A unit that writes an object puts the report into the section
//     .crucible.quarantine of the object, in each mode, with the head line
//     "# crucible-quarantine 1 stamp=TEXT" and then one line for each finding.
//     So a compiler cache stores the report with the object.  The flag "e"
//     (SHF_EXCLUDE) keeps the section out of each executable and each shared
//     library that the linker writes.  utils/scripts/quarantine_sections.py
//     reads the section.  The plugin writes the directives at
//     PLUGIN_FINISH_UNIT, which runs before the end of the assembler output.
//     A unit that writes no object (-fsyntax-only, -E) has no section.
//
// The plugin cannot see what the front end keeps as no tree, or as a tree with
// no source location: a use in an unevaluated operand, a dependent member of a
// template, a using-declaration and a default argument that no call uses.  The
// front end folds the initializer of a variable while it parses, when a call
// of a constexpr function in it gives a constant.  Such a call is no finding.
// The libstdc++ assertions of the Debug, TSan and UBSan-strict presets stop
// some of these folds, so a call such as std::optional::operator-> in an
// initializer can be a finding in those presets and not in Release.  A
// constructor, a destructor and a conversion function of a library class are
// not findings: the compiler calls them for an object that the plugin reports
// where the code declares it.

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "plugin_core.h"
#include "diagnostic.h"
#include "diagnostics/file-cache.h"
#include "incpath.h"
#include "output.h"
#include "tree-iterator.h"

int plugin_is_GPL_compatible;

// libcpp exports deps_add_dep, and mkdeps.h, which declares it, is not one of
// the plugin headers.
void deps_add_dep(class mkdeps*, const char*);

namespace {

using namespace crucible_plugin;

// ── The kinds of the quarantine rule ────────────────────────────────────

enum class Kind : std::uint8_t {
    std_entity,
    std_object,
    c_library_object,
    raw_pointer_object,
    raw_function_pointer,
    c_array_object,
    raw_new_delete,
    c_library_call,
    compiler_builtin,
    inline_asm,
    assert_expansion,
    layer_header,
    door_header,
    upward_include,
};

const char* kind_name(Kind kind) {
    switch (kind) {
        case Kind::std_entity:
            return "std_entity";
        case Kind::std_object:
            return "std_object";
        case Kind::c_library_object:
            return "c_library_object";
        case Kind::raw_pointer_object:
            return "raw_pointer_object";
        case Kind::raw_function_pointer:
            return "raw_function_pointer";
        case Kind::c_array_object:
            return "c_array_object";
        case Kind::raw_new_delete:
            return "raw_new_delete";
        case Kind::c_library_call:
            return "c_library_call";
        case Kind::compiler_builtin:
            return "compiler_builtin";
        case Kind::inline_asm:
            return "inline_asm";
        case Kind::assert_expansion:
            return "assert_expansion";
        case Kind::layer_header:
            return "layer_header";
        case Kind::door_header:
            return "door_header";
        case Kind::upward_include:
            return "upward_include";
    }
    return "unknown";
}

enum class Library : std::uint8_t {
    none,
    standard,
    c_library
};

enum class Role : std::uint8_t {
    variable,
    member,
    parameter,
    return_value
};

// The use of a library class that the walk reads: the type of a parameter, or
// each other use.  An admit row with the restriction `parameters` admits its
// class only as the type of a parameter.
enum class Use : std::uint8_t {
    other,
    parameter,
};

// ── The state of one translation unit ───────────────────────────────────

// An #include directive of a base or a quarantined file whose target the
// plugin does not know yet.
struct PendingInclude {
    bool is_set = false;
    Place place;  // the directive
    std::string spelled;  // the name inside the delimiters
    bool is_angle = false;
    bool is_next = false;  // an #include_next directive
};

// The library classes and enumerations that a type holds: the first one that
// no admit row admits, and the first one that a row with `until` admits, with
// that row.
struct LibraryClasses {
    tree refused = NULL_TREE;
    tree pending = NULL_TREE;
    const AdmitRow* pending_row = nullptr;

    // Keeps the first refused and the first pending class of the two.
    void add(const LibraryClasses& other) {
        if (refused == NULL_TREE) {
            refused = other.refused;
        }
        if (pending == NULL_TREE) {
            pending = other.pending;
            pending_row = other.pending_row;
        }
    }
    [[nodiscard]] bool is_full() const { return refused != NULL_TREE && pending != NULL_TREE; }
};

using IncludeCallback = void (*)(cpp_reader*, location_t, const unsigned char*, const char*, int, const cpp_token**);
using MacroCallback = void (*)(cpp_reader*, location_t, cpp_hashnode*);

// The state of the quarantine rule.  plugin_core.h holds the state that the
// two rules share: the root, the rule table, the files, the findings and the
// regions.
struct State {
    std::string out_dir;
    std::string stamps_dir;
    std::string stamp;
    bool is_error_mode = false;

    // The admit row of each name entry, and of each header entry by its name.
    std::unordered_map<std::string, const AdmitRow*> admitted_names;
    std::unordered_map<std::string, const AdmitRow*> admitted_headers;
    // The admit row of each file that an #include of an admitted header
    // entered, by its real path, and the row of the last directive when it
    // names an admitted header.
    std::unordered_map<std::string, const AdmitRow*> admitted_header_paths;
    const AdmitRow* admitted_header_pending = nullptr;

    // The include rules.
    IncludeCallback previous_include = nullptr;
    PendingInclude pending;

    // The expansions of assert.
    MacroCallback previous_used = nullptr;

    // Caches keyed by tree.  A garbage collection can free a tree and reuse
    // its address, so each collection clears them.  Nothing depends on them
    // for correctness: a finding is recorded one time for each place.
    // The first two hold one map for each Use.
    std::unordered_map<tree, LibraryClasses> library_class_of[2];
    std::unordered_map<tree, const AdmitRow*> admitting_row_of[2];
    std::unordered_map<tree, bool> default_argument_of;
    std::unordered_set<tree> walked;
};

State state;

bool is_include_kind(const char* kind);
void finish_pending_include(const FileEntry* entered);

// ── Paths and places ────────────────────────────────────────────────────

bool is_quarantined(location_t location) {
    return place_of(location, Scope::quarantine).file_class == FileClass::quarantined;
}

// A declaration that a macro expansion makes can hold tokens that a
// quarantined file spells, even when the name token is spelled elsewhere.
bool is_worth_a_walk(location_t location) {
    return is_quarantined(location)
        || (location > BUILTINS_LOCATION && !in_system_header_at(location)
            && linemap_location_from_macro_expansion_p(line_table, location));
}

// One finding for each kind and place.  Two names at one place, such as a type
// and its member function in one expression, are two findings, and so are two
// headers that one place includes.  KIND must live as long as the unit: a
// kind_name() text or the pending kind of an admit row.
void add_finding(const char* kind, bool is_name, const Place& place, const std::string& entity) {
    std::string key = std::string(kind) + ' ' + place.file->relative + ':' + std::to_string(place.line) + ':'
                    + std::to_string(place.column) + (is_name ? ' ' + entity : std::string{});
    if (!core.finding_keys.insert(key).second) {
        return;
    }
    Finding finding;
    finding.kind = kind;
    finding.file = place.file;
    finding.line = place.line;
    finding.column = place.column;
    finding.spelling = place.spelling;
    finding.entity = entity;
    core.findings.push_back(std::move(finding));
}

bool is_system_assert(const cpp_hashnode* node) {
    return node != nullptr && cpp_user_macro_p(node) && node->value.macro != nullptr && node->value.macro->syshdr
        && std::strcmp(reinterpret_cast<const char*>(NODE_NAME(node)), "assert") == 0;
}

// True when a macro of a system header spells the token at LOCATION inside an
// expansion of the macro assert of a system header.  The finding
// assert_expansion stands for each such token, such as __assert_fail or
// __builtin_FILE.  A token of the condition is spelled where assert is used,
// so it stays a finding.  Complexity: O(depth of the macro expansion).
bool is_inside_system_assert(location_t location) {
    if (!linemap_location_from_macro_expansion_p(line_table, location)
        || !in_system_header_at(linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, nullptr))) {
        return false;
    }
    location_t current = location;
    for (int depth = 0; depth < 64 && linemap_location_from_macro_expansion_p(line_table, current); ++depth) {
        const line_map* map = linemap_lookup(line_table, current);
        if (is_system_assert(MACRO_MAP_MACRO(linemap_check_macro(map)))) {
            return true;
        }
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return false;
}

bool is_name_kind(Kind kind) {
    return kind == Kind::std_entity || kind == Kind::c_library_call || kind == Kind::compiler_builtin
        || kind == Kind::layer_header || kind == Kind::door_header || kind == Kind::upward_include;
}

void add_finding(Kind kind, const Place& place, const std::string& entity) {
    add_finding(kind_name(kind), is_name_kind(kind), place, entity);
}

// A finding of the quarantine rule, at a place in a quarantined file.
void record_kind(const char* kind, bool is_name, location_t location, const std::string& entity) {
    if (is_inside_system_assert(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class == FileClass::quarantined) {
        add_finding(kind, is_name, place, entity);
    }
}

void record(Kind kind, location_t location, const std::string& entity) {
    record_kind(kind_name(kind), is_name_kind(kind), location, entity);
}

// A use of ENTITY that ROW admits with `until FAMILY`.
void record_pending(const AdmitRow& row, location_t location, const std::string& entity) {
    record_kind(row.pending_kind.c_str(), true, location, entity);
}

// ── Names ───────────────────────────────────────────────────────────────

std::string identifier_text(tree identifier) {
    return identifier != NULL_TREE ? std::string(IDENTIFIER_POINTER(identifier)) : std::string{};
}

std::string simple_name(tree decl) {
    tree name = DECL_NAME(decl);
    if (TREE_CODE(decl) == NAMESPACE_DECL) {
        return name != NULL_TREE ? identifier_text(name) : std::string("(anonymous namespace)");
    }
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE || IDENTIFIER_ANON_P(name)) {
        return "(unnamed)";
    }
    if (IDENTIFIER_LAMBDA_P(name)) {
        return "(lambda)";
    }
    if (IDENTIFIER_CONV_OP_P(name)) {
        return "operator (conversion)";
    }
    return identifier_text(name);
}

// The qualified name of a declaration, without template arguments and
// without inline namespaces: std::__cxx11::basic_string<char> gives
// std::basic_string.
std::string qualified_name(tree decl) {
    std::vector<std::string> parts;
    tree current = decl;
    while (current != NULL_TREE && current != global_namespace && parts.size() < 32) {
        if (TYPE_P(current)) {
            tree main_type = TYPE_MAIN_VARIANT(current);
            tree type_decl = TYPE_NAME(main_type);
            if (type_decl == NULL_TREE || TREE_CODE(type_decl) != TYPE_DECL) {
                parts.emplace_back("(unnamed)");
                break;
            }
            parts.push_back(LAMBDA_TYPE_P(main_type) ? std::string("(lambda)") : simple_name(type_decl));
            current = CP_TYPE_CONTEXT(main_type);
        } else if (TREE_CODE(current) == NAMESPACE_DECL) {
            if (!DECL_NAMESPACE_INLINE_P(current)) {
                parts.push_back(simple_name(current));
            }
            current = CP_DECL_CONTEXT(current);
        } else if (DECL_P(current)) {
            parts.push_back(simple_name(current));
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    std::string joined;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (!joined.empty()) {
            joined += "::";
        }
        joined += *part;
    }
    return joined;
}

std::string type_text(tree type) {
    const char* printed = type_as_string(type, 0);
    return printed != nullptr ? std::string(printed) : std::string("(type)");
}

// ── The library and the admitted list ───────────────────────────────────

// The namespace directly below the global namespace that holds DECL, or the
// global namespace itself.
tree top_namespace(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < 128; ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL) {
            tree context = CP_DECL_CONTEXT(current);
            if (context == global_namespace) {
                return current;
            }
            current = context;
        } else if (TYPE_P(current)) {
            current = CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current));
        } else if (DECL_P(current)) {
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    return global_namespace;
}

// A declaration of namespace std is standard, wherever it is.  Any other
// declaration of a system header is standard when a namespace holds it, as
// __gnu_cxx does, and part of the C library when it is global.
Library library_of(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return Library::none;
    }
    tree top = top_namespace(decl);
    if (top == std_node) {
        return Library::standard;
    }
    if (!in_system_header_at(DECL_SOURCE_LOCATION(decl))) {
        return Library::none;
    }
    return top == global_namespace ? Library::c_library : Library::standard;
}

// The admit row of a qualified name: the row of the name, or of the first
// enclosing name that has a row.  IS_EXACT tells whether the row names the
// name itself.  Returns null when no row admits the name.
const AdmitRow* admit_row_of_name(const std::string& name, bool& is_exact) {
    auto found = state.admitted_names.find(name);
    if (found != state.admitted_names.end()) {
        is_exact = true;
        return found->second;
    }
    is_exact = false;
    for (std::size_t colon = name.find("::"); colon != std::string::npos; colon = name.find("::", colon + 2)) {
        auto enclosing = state.admitted_names.find(name.substr(0, colon));
        if (enclosing != state.admitted_names.end()) {
            return enclosing->second;
        }
    }
    return nullptr;
}

// The parameter count of a function, without `this`, or -1 when DECL is no
// function.
int parameter_count(tree decl) {
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    if (decl == NULL_TREE || TREE_CODE(decl) != FUNCTION_DECL) {
        return -1;
    }
    int count = 0;
    for (tree parameter = TYPE_ARG_TYPES(TREE_TYPE(decl)); parameter != NULL_TREE && parameter != void_list_node;
         parameter = TREE_CHAIN(parameter)) {
        ++count;
    }
    return DECL_IOBJ_MEMBER_FUNCTION_P(decl) ? count - 1 : count;
}

// True when TYPE holds a bool or an enumeration: the type itself, an element
// of an array, a vector or a complex type, or a data member or a base of a
// class or a union.  A pointer holds an address, so the walk does not follow
// it, also when its target is dependent.  Each other dependent type and an
// incomplete class count as types that hold them, so the restriction
// plain-result fails closed.  Complexity: O(size of the type tree).
bool holds_bool_or_enumeration(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > 64) {
        return true;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case OFFSET_TYPE:
            return false;
        case BOOLEAN_TYPE:
        case ENUMERAL_TYPE:
            return true;
        case ARRAY_TYPE:
        case VECTOR_TYPE:
        case COMPLEX_TYPE:
            return holds_bool_or_enumeration(TREE_TYPE(type), depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE: {
            tree main_type = TYPE_MAIN_VARIANT(type);
            if (TYPE_PTRMEMFUNC_P(main_type)) {
                return false;
            }
            if (uses_template_parms(main_type) || !COMPLETE_TYPE_P(main_type)) {
                return true;
            }
            for (tree field = TYPE_FIELDS(main_type); field != NULL_TREE; field = DECL_CHAIN(field)) {
                if (TREE_CODE(field) == FIELD_DECL && holds_bool_or_enumeration(TREE_TYPE(field), depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        default:
            return uses_template_parms(type);
    }
}

// The result type of a call of the function template FUNCTION with the
// explicit template ARGUMENTS of a template-id, such as std::bit_cast<To>(x)
// in a template.  It is known when the result type is one template parameter
// of the innermost level and an explicit argument gives that parameter.
// Returns null otherwise.
tree result_of_explicit_arguments(tree function, tree arguments) {
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC || DECL_LANG_SPECIFIC(function) == nullptr
        || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return NULL_TREE;
    }
    tree result = TREE_TYPE(TREE_TYPE(function));
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (result == NULL_TREE || TREE_CODE(result) != TEMPLATE_TYPE_PARM || template_decl == NULL_TREE
        || TREE_CODE(template_decl) != TEMPLATE_DECL
        || TEMPLATE_TYPE_LEVEL(result) != TMPL_PARMS_DEPTH(DECL_TEMPLATE_PARMS(template_decl))
        || TEMPLATE_TYPE_IDX(result) >= TREE_VEC_LENGTH(arguments)) {
        return NULL_TREE;
    }
    tree argument = TREE_VEC_ELT(arguments, TEMPLATE_TYPE_IDX(result));
    return argument != NULL_TREE && TYPE_P(argument) ? argument : NULL_TREE;
}

// A row admits DECL with its restrictions.  The plugin reads no base file, and
// each path after `in` is a base path, so a row with `in` admits nothing in a
// quarantined file.  RESULT is the result type of the call when explicit
// template arguments give it, or null.
bool row_admits(const AdmitRow& row, tree decl, bool is_exact, Use use, tree result) {
    if (!row.in_paths.empty()) {
        return false;
    }
    if (row.arity >= 0 && parameter_count(decl) != row.arity) {
        return false;
    }
    if (row.is_parameters_only && is_exact && use != Use::parameter) {
        return false;
    }
    if (row.is_plain_result) {
        tree function = TREE_CODE(decl) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(decl) : decl;
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
            return false;
        }
        tree type = result != NULL_TREE ? result : TREE_TYPE(TREE_TYPE(function));
        if (holds_bool_or_enumeration(type, 0)) {
            return false;
        }
    }
    return !row.is_concepts_only || concept_definition_p(decl);
}

// The row that admits a dependent qualified name, which names no declaration
// yet, or null.  A restriction that needs the declaration cannot apply, so
// only a row with `in` refuses it.
const AdmitRow* admitting_row_of_name(const std::string& name) {
    bool is_exact = false;
    const AdmitRow* row = admit_row_of_name(name, is_exact);
    return row != nullptr && row->in_paths.empty() ? row : nullptr;
}

const AdmitRow* find_admitting_row(tree decl, Use use, tree result) {
    bool is_exact = false;
    const AdmitRow* row = admit_row_of_name(qualified_name(decl), is_exact);
    if (row != nullptr && row_admits(*row, decl, is_exact, use, result)) {
        return row;
    }
    const char* file = DECL_SOURCE_FILE(decl);
    auto header = file != nullptr ? state.admitted_header_paths.find(classify_file(file).real)
                                  : state.admitted_header_paths.end();
    if (header != state.admitted_header_paths.end() && row_admits(*header->second, decl, false, use, result)) {
        return header->second;
    }
    return nullptr;
}

// The row that admits DECL, or null when no row admits it.  A name entry
// admits the entity that it names and each name nested in it, so
// std::tuple_size admits std::tuple_size<T>::value.  It admits no other entity
// whose name starts with the same text: std::tuple_size_v and
// std::tuple_element_t need their own entries.  A header entry admits each
// entity that the header itself declares: the file that an #include of that
// exact name enters, and not a header of the same last name in a different
// directory, such as experimental/type_traits.  A restriction of the row
// narrows what it admits (row_admits).  RESULT is the result type that the
// explicit template arguments of a call give, or null.
const AdmitRow* admitting_row(tree decl, Use use = Use::other, tree result = NULL_TREE) {
    if (result != NULL_TREE) {
        return find_admitting_row(decl, use, result);
    }
    std::unordered_map<tree, const AdmitRow*>& cache = state.admitting_row_of[static_cast<int>(use)];
    auto cached = cache.find(decl);
    if (cached != cache.end()) {
        return cached->second;
    }
    const AdmitRow* row = find_admitting_row(decl, use, NULL_TREE);
    cache.emplace(decl, row);
    return row;
}

bool is_plainly_admitted(const AdmitRow* row) { return row != nullptr && row->pending_kind.empty(); }

// The finding of a use of the standard library entity DECL at LOCATION, as the
// admitted list decides it: none when a row admits it, the kind of the row
// when a row with `until` admits it, and std_entity when no row admits it.
// Returns true when it made a finding.
bool record_standard_use(tree decl, location_t location, const std::string& entity, Use use = Use::other,
                         tree result = NULL_TREE) {
    const AdmitRow* row = admitting_row(decl, use, result);
    if (row == nullptr) {
        record(Kind::std_entity, location, entity);
        return true;
    }
    if (row->pending_kind.empty()) {
        return false;
    }
    record_pending(*row, location, entity);
    return true;
}

// ── The library class inside a type ─────────────────────────────────────

LibraryClasses library_class_in(tree type, int depth, Use use = Use::other);

// The most general template of a class template specialization, or null.
tree general_template_of(tree class_type) {
    if (!CLASS_TYPE_P(class_type) || CLASSTYPE_TEMPLATE_INFO(class_type) == NULL_TREE) {
        return NULL_TREE;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(class_type);
    return template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
             ? most_general_template(template_decl)
             : NULL_TREE;
}

// A default that depends on an earlier parameter, such as std::allocator<T>,
// matches each specialization of the same template.
bool matches_default(tree argument, tree default_argument) {
    if (TYPE_P(argument) && TYPE_P(default_argument)) {
        if (!uses_template_parms(default_argument)) {
            return same_type_p(argument, default_argument);
        }
        tree argument_template = general_template_of(TYPE_MAIN_VARIANT(argument));
        return argument_template != NULL_TREE
            && argument_template == general_template_of(TYPE_MAIN_VARIANT(default_argument));
    }
    if (TREE_CODE(argument) == TEMPLATE_DECL && TREE_CODE(default_argument) == TEMPLATE_DECL) {
        return most_general_template(argument) == most_general_template(default_argument);
    }
    return false;
}

LibraryClasses library_class_in_argument(tree argument, int depth) {
    LibraryClasses found;
    if (argument == NULL_TREE || argument == error_mark_node) {
        return found;
    }
    if (ARGUMENT_PACK_P(argument)) {
        tree arguments = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(arguments) && !found.is_full(); ++index) {
            found.add(library_class_in_argument(TREE_VEC_ELT(arguments, index), depth + 1));
        }
        return found;
    }
    if (TYPE_P(argument)) {
        return library_class_in(argument, depth + 1);
    }
    tree result = TREE_CODE(argument) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(argument) : NULL_TREE;
    if (result == NULL_TREE || TREE_CODE(result) != TYPE_DECL || library_of(argument) == Library::none) {
        return found;
    }
    const AdmitRow* row = admitting_row(argument);
    if (row == nullptr) {
        found.refused = TREE_TYPE(result);
    } else if (!row->pending_kind.empty()) {
        found.pending = TREE_TYPE(result);
        found.pending_row = row;
    }
    return found;
}

// The template arguments of a class that is not a library class.  An
// argument equal to the default of its parameter is skipped: the declaration
// did not write it, so a fixy default such as the storage of AppendOnly is not
// a finding at each object.
LibraryClasses library_class_in_template_arguments(tree class_type, int depth) {
    LibraryClasses found;
    tree info = CLASSTYPE_TEMPLATE_INFO(class_type);
    if (TI_ARGS(info) == NULL_TREE) {
        return found;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(info));
    tree parameters = NULL_TREE;
    tree template_decl = TI_TEMPLATE(info);
    if (template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL) {
        tree general = most_general_template(template_decl);
        if (general != NULL_TREE && TREE_CODE(general) == TEMPLATE_DECL) {
            parameters = INNERMOST_TEMPLATE_PARMS(DECL_TEMPLATE_PARMS(general));
        }
    }
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC) {
        return found;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(arguments) && !found.is_full(); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (parameters != NULL_TREE && index < TREE_VEC_LENGTH(parameters)) {
            tree parameter = TREE_VEC_ELT(parameters, index);
            if (parameter != NULL_TREE && parameter != error_mark_node && TREE_PURPOSE(parameter) != NULL_TREE
                && matches_default(argument, TREE_PURPOSE(parameter))) {
                continue;
            }
        }
        found.add(library_class_in_argument(argument, depth));
    }
    return found;
}

// The first class or enumeration of the standard library or of a C header
// that TYPE holds, and that no admit row admits, and the first one that a row
// with `until` admits.  The walk looks through typedefs, pointers, references,
// arrays, template arguments and the enclosing class, and stops at a library
// class or enumeration.  So a typedef of fixy that names std::memory_order
// gives an object of a library type.  A function type is not read: a callback
// that takes a std::string is not an object of that type.  USE applies to the
// class at the outer level, through pointers and references.  A template
// argument and an enclosing class are other uses.
LibraryClasses library_class_in(tree type, int depth, Use use) {
    LibraryClasses found;
    if (type == NULL_TREE || type == error_mark_node || depth > 64) {
        return found;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
            if (FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type))) {
                return found;
            }
            return library_class_in(TREE_TYPE(type), depth + 1, use);
        case ARRAY_TYPE:
            return library_class_in(TREE_TYPE(type), depth + 1, use);
        case TYPENAME_TYPE:
            return library_class_in(TYPE_CONTEXT(type), depth + 1);
        case TYPE_PACK_EXPANSION:
            return library_class_in(PACK_EXPANSION_PATTERN(type), depth + 1, use);
        case RECORD_TYPE:
        case UNION_TYPE:
        case ENUMERAL_TYPE:
            break;
        default:
            return found;
    }
    if (TYPE_PTRMEMFUNC_P(type)) {
        return found;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    std::unordered_map<tree, LibraryClasses>& cache = state.library_class_of[static_cast<int>(use)];
    auto cached = cache.find(main_type);
    if (cached != cache.end()) {
        return cached->second;
    }
    tree decl = TYPE_MAIN_DECL(main_type);
    if (decl != NULL_TREE && !LAMBDA_TYPE_P(main_type)) {
        if (library_of(decl) != Library::none) {
            const AdmitRow* row = admitting_row(decl, use);
            if (row == nullptr) {
                found.refused = main_type;
            } else if (!row->pending_kind.empty()) {
                found.pending = main_type;
                found.pending_row = row;
            }
        } else {
            if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
                found = library_class_in_template_arguments(main_type, depth);
            }
            tree context = CP_TYPE_CONTEXT(main_type);
            if (!found.is_full() && context != NULL_TREE && TYPE_P(context)) {
                found.add(library_class_in(context, depth + 1));
            }
        }
    }
    cache.emplace(main_type, found);
    return found;
}

// The library name that TYPE spells at its outer level, when the type is not
// a class or an enumeration: a typedef or an alias template of namespace std,
// such as std::size_t or std::tuple_element_t.  The walk looks through
// pointers, references and arrays, and it stops at a typedef that is not of
// the library: a project alias of std::size_t names no library entity where
// the code uses it, because the type itself is a fundamental type.  Returns
// the declaration of the alias template or of the typedef, or null when a row
// without `until` admits it.  A template argument loses its typedef, so a
// typedef inside a template argument is no finding.  library_class_in finds an
// enumeration.
tree library_type_name_in(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > 64) {
        return NULL_TREE;
    }
    if (TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == REFERENCE_TYPE) {
        return FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type)) ? NULL_TREE : library_type_name_in(TREE_TYPE(type), depth + 1);
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return library_type_name_in(TREE_TYPE(type), depth + 1);
    }
    tree name = TYPE_NAME(type);
    if (name != NULL_TREE && TREE_CODE(name) == TYPE_DECL && DECL_ORIGINAL_TYPE(name) != NULL_TREE) {
        tree decl = name;
        if (tree info = TYPE_ALIAS_TEMPLATE_INFO(type); info != NULL_TREE && TI_TEMPLATE(info) != NULL_TREE) {
            decl = TI_TEMPLATE(info);
        }
        return library_of(decl) == Library::standard && !is_plainly_admitted(admitting_row(decl)) ? decl : NULL_TREE;
    }
    return NULL_TREE;
}

bool is_standard_class(tree type) {
    return type != NULL_TREE && library_of(TYPE_MAIN_DECL(type)) == Library::standard;
}

// The finding of a class that CLASSES gives as pending, at LOCATION.  SUFFIX
// follows the name of the class in the entity, such as the member of a
// qualified name.
void record_pending_class(const LibraryClasses& classes, location_t location, const std::string& suffix = {}) {
    if (classes.pending != NULL_TREE && classes.pending_row != nullptr) {
        record_pending(*classes.pending_row, location, qualified_name(classes.pending) + suffix);
    }
}

// The library entities that TYPE names, where the source spells TYPE: a class
// of namespace std inside it, or else the library name at its outer level,
// and each class that a row with `until` admits.
void record_named_entities(tree type, location_t location) {
    LibraryClasses classes = library_class_in(type, 0);
    if (classes.refused != NULL_TREE) {
        if (is_standard_class(classes.refused)) {
            record(Kind::std_entity, location, qualified_name(TYPE_MAIN_DECL(classes.refused)));
        }
    } else if (tree named = library_type_name_in(type, 0)) {
        record_standard_use(named, location, qualified_name(named));
    }
    record_pending_class(classes, location);
}

// ── The checks of a declaration ─────────────────────────────────────────

void check_object(tree decl, Role role) {
    tree type = role == Role::return_value ? TREE_TYPE(TREE_TYPE(decl)) : TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    if (!is_quarantined(location)) {
        return;
    }
    if (role != Role::return_value) {
        if (TREE_CODE(type) == POINTER_TYPE) {
            bool is_function = FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type));
            record(is_function ? Kind::raw_function_pointer : Kind::raw_pointer_object, location, type_text(type));
        } else if (TYPE_PTRMEM_P(type)) {
            record(Kind::raw_function_pointer, location, type_text(type));
        }
        if (role != Role::parameter && TREE_CODE(type) == ARRAY_TYPE) {
            record(Kind::c_array_object, location, type_text(type));
        }
    }
    LibraryClasses classes = library_class_in(type, 0, role == Role::parameter ? Use::parameter : Use::other);
    if (tree found = classes.refused) {
        Kind kind = library_of(TYPE_MAIN_DECL(found)) == Library::c_library ? Kind::c_library_object : Kind::std_object;
        record(kind, location, qualified_name(found) + " (" + type_text(type) + ")");
    } else if (tree named = library_type_name_in(type, 0)) {
        record_standard_use(named, location, qualified_name(named));
    }
    record_pending_class(classes, location);
}

void check_alias(tree decl) {
    tree aliased = DECL_ORIGINAL_TYPE(decl);
    if (aliased == NULL_TREE || !is_quarantined(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    record_named_entities(aliased, DECL_SOURCE_LOCATION(decl));
}

bool has_lang_template_info(tree decl) {
    return DECL_LANG_SPECIFIC(decl) != nullptr && DECL_TEMPLATE_INFO(decl) != NULL_TREE;
}

// True when DECL, or a function or class that holds it, comes from a template
// instantiation.
bool is_in_instantiation(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < 128; ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL) {
            return false;
        }
        if (TYPE_P(current)) {
            tree main_type = TYPE_MAIN_VARIANT(current);
            if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INSTANTIATION(main_type)) {
                return true;
            }
            current = CP_TYPE_CONTEXT(main_type);
            continue;
        }
        if (!DECL_P(current)) {
            return false;
        }
        if (TREE_CODE(current) == FUNCTION_DECL && DECL_LANG_SPECIFIC(current) != nullptr
            && DECL_TEMPLOID_INSTANTIATION(current)) {
            return true;
        }
        current = CP_DECL_CONTEXT(current);
    }
    return false;
}

// ── The walk of a body ──────────────────────────────────────────────────

// The walk of one body visits each node one time, so it stays linear when
// subtrees are shared.  A declaration is one shared node for all of its uses,
// so each expression reads the declarations that are its operands: each use
// then counts, at the location of the expression that holds it.
struct BodyWalk {
    location_t last = UNKNOWN_LOCATION;
    bool is_pattern = false;
    hash_set<tree>* visited = nullptr;
};

void walk_body(tree function, bool is_pattern);
void walk_class(tree type, bool is_pattern);
void walk_expression(tree* expression, BodyWalk& walk);

bool is_placement_new(tree function) {
    tree parameters = TYPE_ARG_TYPES(TREE_TYPE(function));
    if (parameters == NULL_TREE || TREE_CHAIN(parameters) == NULL_TREE) {
        return false;
    }
    tree second = TREE_VALUE(TREE_CHAIN(parameters));
    tree after = TREE_CHAIN(TREE_CHAIN(parameters));
    return DECL_NAMESPACE_SCOPE_P(function) && TREE_CODE(second) == POINTER_TYPE && VOID_TYPE_P(TREE_TYPE(second))
        && (after == NULL_TREE || after == void_list_node);
}

bool is_identifier_byte(char byte) {
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_';
}

// The identifier that the source spells at the start of the range of
// LOCATION, after each space, '(' and ':' there, or "" when none.  The
// spelling location decides, so a macro of a system header gives the
// identifier of its definition.  Complexity: O(length of the line).
std::string identifier_spelled_at(location_t location) {
    location_t start = linemap_resolve_location(line_table, get_start(location), LRK_SPELLING_LOCATION, nullptr);
    expanded_location from = expand_location(start);
    if (from.file == nullptr || from.line <= 0 || from.column <= 0) {
        return {};
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(from.file, from.line);
    if (!line) {
        return {};
    }
    std::string text(line.get_buffer(), line.length());
    std::size_t at = static_cast<std::size_t>(from.column - 1);
    while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '(' || text[at] == ':')) {
        ++at;
    }
    std::size_t end = at;
    while (end < text.size() && is_identifier_byte(text[end])) {
        ++end;
    }
    return text.substr(at, end - at);
}

// A builtin of GCC that the code calls.  The front end makes some calls of a
// builtin itself, such as the atomic load of the guard of a static local, and
// gives them the location of the declaration that needs them.  So the call
// counts only when the source spells the builtin, or a name with a prefix that
// GCC keeps for its builtins, at the start of the call: __atomic_load_n
// resolves to __atomic_load_4.  A builtin with a function of the C library
// behind it, such as __builtin_memcpy, has that function as its assembler
// name.  Returns true when it made a finding.
bool check_builtin_use(tree function, location_t location) {
    tree name = DECL_NAME(function);
    if (name == NULL_TREE || !is_quarantined(location)) {
        return false;
    }
    std::string spelled = identifier_spelled_at(location);
    bool is_spelled = spelled == IDENTIFIER_POINTER(name) || spelled.rfind("__builtin_", 0) == 0
                   || spelled.rfind("__atomic_", 0) == 0 || spelled.rfind("__sync_", 0) == 0;
    if (!is_spelled) {
        return false;
    }
    tree library = DECL_ASSEMBLER_NAME_SET_P(function) ? DECL_ASSEMBLER_NAME_RAW(function) : NULL_TREE;
    if (DECL_BUILT_IN_CLASS(function) == BUILT_IN_NORMAL && library != NULL_TREE && library != name) {
        record(Kind::c_library_call, location, identifier_text(library) + " (" + spelled + ")");
    } else {
        record(Kind::compiler_builtin, location, spelled);
    }
    return true;
}

// One function that the code names.  EXPLICIT_ARGUMENTS are the explicit
// template arguments of a template-id that names it, or null.  Returns true
// when it made a finding.
bool check_function_use(tree function, location_t location, tree explicit_arguments = NULL_TREE) {
    if (TREE_CODE(function) == TEMPLATE_DECL) {
        function = DECL_TEMPLATE_RESULT(function);
    }
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    if (DECL_IS_UNDECLARED_BUILTIN(function)) {
        return fndecl_built_in_p(function) && check_builtin_use(function, location);
    }
    if (DECL_IS_OPERATOR_NEW_P(function)) {
        record(Kind::raw_new_delete, location, is_placement_new(function) ? "placement new" : "new");
        return true;
    }
    if (DECL_IS_OPERATOR_DELETE_P(function)) {
        record(Kind::raw_new_delete, location, "delete");
        return true;
    }
    // A constructor, a destructor and a conversion function run for an object
    // that the plugin reports where the code declares it, and the source
    // rarely names them: the compiler calls them implicitly.
    if (DECL_ARTIFICIAL(function) || DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function)
        || DECL_CONV_FN_P(function)) {
        return false;
    }
    switch (library_of(function)) {
        case Library::standard:
            return record_standard_use(function, location, qualified_name(function), Use::other,
                                       result_of_explicit_arguments(function, explicit_arguments));
        case Library::c_library:
            record(Kind::c_library_call, location, qualified_name(function));
            return true;
        case Library::none:
            return false;
    }
    return false;
}

// True when a call with ARGUMENTS arguments can call FUNCTION: the count is
// at least the parameters with no default and at most all parameters, or the
// function takes a pack or an ellipsis.
bool takes_argument_count(tree function, int arguments) {
    if (TREE_CODE(function) == TEMPLATE_DECL) {
        function = DECL_TEMPLATE_RESULT(function);
    }
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return true;
    }
    tree parameter = TYPE_ARG_TYPES(TREE_TYPE(function));
    if (DECL_IOBJ_MEMBER_FUNCTION_P(function) && parameter != NULL_TREE) {
        parameter = TREE_CHAIN(parameter);
    }
    int needed = 0;
    int total = 0;
    for (; parameter != NULL_TREE && parameter != void_list_node; parameter = TREE_CHAIN(parameter)) {
        if (PACK_EXPANSION_P(TREE_VALUE(parameter))) {
            return arguments >= needed;
        }
        ++total;
        needed += TREE_PURPOSE(parameter) == NULL_TREE ? 1 : 0;
    }
    return arguments >= needed && (parameter == NULL_TREE || arguments <= total);
}

// An overload set that the code names.  A template keeps the set of a
// dependent call, and ARGUMENTS gives the argument count of that call, or -1.
// Only a candidate that can take the count counts: std::move(value) then names
// the cast of one argument and not the algorithm of three.
void check_overload_use(tree overload, location_t location, int arguments = -1, tree explicit_arguments = NULL_TREE) {
    for (ovl_iterator candidate(overload, true); candidate; ++candidate) {
        tree function = *candidate;
        if (TREE_CODE(function) == OVERLOAD) {
            check_overload_use(function, location, arguments, explicit_arguments);
            return;
        }
        if (arguments >= 0 && !takes_argument_count(function, arguments)) {
            continue;
        }
        if (check_function_use(function, location, explicit_arguments)) {
            return;
        }
    }
}

// A variable that the code names.  Only a variable of a namespace or a
// static data member can be a library entity.  A constexpr variable of an
// enumeration or an arithmetic type, such as std::memory_order_acquire, is a
// named constant: it makes no object, as an enumerator makes none.  A global
// variable of the C library, such as optind or stderr, is a use of the C
// library.
void check_variable_use(tree variable, location_t location) {
    if (DECL_ARTIFICIAL(variable) || !(DECL_NAMESPACE_SCOPE_P(variable) || DECL_CLASS_SCOPE_P(variable))) {
        return;
    }
    tree type = TREE_TYPE(variable);
    if (DECL_DECLARED_CONSTEXPR_P(variable) && type != NULL_TREE
        && (TREE_CODE(type) == ENUMERAL_TYPE || ARITHMETIC_TYPE_P(type))) {
        return;
    }
    switch (library_of(variable)) {
        case Library::standard:
            record_standard_use(variable, location, qualified_name(variable));
            break;
        case Library::c_library:
            record(Kind::c_library_call, location, qualified_name(variable));
            break;
        case Library::none:
            break;
    }
}

void check_template_use(tree template_decl, location_t location, tree explicit_arguments = NULL_TREE) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL) {
        check_function_use(template_decl, location, explicit_arguments);
        return;
    }
    if (library_of(template_decl) == Library::standard) {
        record_standard_use(template_decl, location, qualified_name(template_decl));
    }
}

std::string member_name_text(tree name) {
    if (name == NULL_TREE) {
        return "(member)";
    }
    switch (TREE_CODE(name)) {
        case IDENTIFIER_NODE:
            return IDENTIFIER_CONV_OP_P(name) ? std::string("operator (conversion)") : identifier_text(name);
        case TEMPLATE_ID_EXPR:
            return member_name_text(TREE_OPERAND(name, 0));
        case BIT_NOT_EXPR:
            return "~" + member_name_text(TREE_OPERAND(name, 0));
        case OVERLOAD:
            return simple_name(OVL_FIRST(name));
        default:
            return DECL_P(name) ? simple_name(name) : std::string("(member)");
    }
}

// A qualified name in a template, such as std::sort before the arguments
// are known, or std::numeric_limits<T>::max.
void check_scope_use(tree scope_ref, location_t location) {
    tree scope = TREE_OPERAND(scope_ref, 0);
    std::string member = member_name_text(TREE_OPERAND(scope_ref, 1));
    if (scope == NULL_TREE) {
        return;
    }
    if (TREE_CODE(scope) == NAMESPACE_DECL) {
        if (top_namespace(scope) == std_node || scope == std_node) {
            std::string name = qualified_name(scope) + "::" + member;
            const AdmitRow* row = admitting_row_of_name(name);
            if (row == nullptr) {
                record(Kind::std_entity, location, name);
            } else if (!row->pending_kind.empty()) {
                record_pending(*row, location, name);
            }
        }
        return;
    }
    if (TYPE_P(scope)) {
        LibraryClasses classes = library_class_in(scope, 0);
        if (is_standard_class(classes.refused)) {
            record(Kind::std_entity, location, qualified_name(classes.refused) + "::" + member);
        }
        record_pending_class(classes, location, "::" + member);
    }
}

// A type that a template names: the type of a cast, an explicit template
// argument, a compound literal.
void check_type_use(tree type, location_t location) { record_named_entities(type, location); }

void check_new_expression(tree expression, location_t location) {
    bool is_placement = TREE_OPERAND(expression, 0) != NULL_TREE;
    tree type = TREE_OPERAND(expression, 1);
    std::string what = is_placement ? "placement new" : "new";
    if (type != NULL_TREE && TYPE_P(type)) {
        what += " " + type_text(type);
    }
    record(Kind::raw_new_delete, location, what);
}

void check_local_decl(tree decl, BodyWalk& walk) {
    if (decl == NULL_TREE || state.walked.count(decl) != 0) {
        return;
    }
    state.walked.insert(decl);
    if (VAR_P(decl)) {
        bool is_binding = DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl);
        bool is_proxy = DECL_HAS_VALUE_EXPR_P(decl) && !DECL_DECOMPOSITION_P(decl);
        bool is_artificial = DECL_ARTIFICIAL(decl) && !DECL_DECOMPOSITION_P(decl);
        if (!is_binding && !is_proxy && !is_artificial) {
            check_object(decl, Role::variable);
        }
        // A template keeps the initializer of a local in DECL_INITIAL, and
        // cp_walk_tree does not reach it outside template processing.
        if (walk.is_pattern && DECL_INITIAL(decl) != NULL_TREE) {
            location_t saved = walk.last;
            walk.last = DECL_SOURCE_LOCATION(decl);
            walk_expression(&DECL_INITIAL(decl), walk);
            walk.last = saved;
        }
        return;
    }
    if (TREE_CODE(decl) == TYPE_DECL && !DECL_ARTIFICIAL(decl)) {
        check_alias(decl);
        return;
    }
    if (TREE_CODE(decl) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
        walk_class(TREE_TYPE(decl), walk.is_pattern);
    }
}

void check_lambda(tree lambda, BodyWalk& walk) {
    tree closure = LAMBDA_EXPR_CLOSURE(lambda);
    if (closure == NULL_TREE || !LAMBDA_TYPE_P(closure)) {
        return;
    }
    tree call_operator = lambda_function(closure);
    if (call_operator == NULL_TREE) {
        return;
    }
    // The call operator of a lambda that is not generic and not in a template
    // has its own PLUGIN_PRE_GENERICIZE.
    if (walk.is_pattern || generic_lambda_fn_p(call_operator)) {
        walk_body(call_operator, true);
    }
}

tree find_immediate_call(tree* node_pointer, int*, void*) {
    tree node = *node_pointer;
    if (TREE_CODE(node) == CALL_EXPR || TREE_CODE(node) == AGGR_INIT_EXPR) {
        tree callee = cp_get_callee_fndecl_nofold(node);
        if (callee != NULL_TREE && DECL_LANG_SPECIFIC(callee) != nullptr && DECL_IMMEDIATE_FUNCTION_P(callee)) {
            return node;
        }
    }
    return NULL_TREE;
}

bool holds_immediate_call(tree expression) {
    return expression != NULL_TREE
        && cp_walk_tree_without_duplicates(&expression, find_immediate_call, nullptr) != NULL_TREE;
}

// True when a parameter of FUNCTION has a default argument.  A call site asks
// for each call, so the answer is cached for each function.
bool has_default_argument(tree function) {
    auto cached = state.default_argument_of.find(function);
    if (cached != state.default_argument_of.end()) {
        return cached->second;
    }
    bool is_found = false;
    for (tree parameter = TYPE_ARG_TYPES(TREE_TYPE(function)); parameter != NULL_TREE && !is_found;
         parameter = TREE_CHAIN(parameter)) {
        is_found = TREE_PURPOSE(parameter) != NULL_TREE;
    }
    state.default_argument_of.emplace(function, is_found);
    return is_found;
}

// DR 2631: the compiler copies a default argument that holds an immediate
// invocation, such as std::source_location::current(), into each call, with
// the location of the call.  The declaration spells that default, and the call
// does not.  So the walk skips the argument of a parameter with a default when
// the argument holds an immediate invocation.  The default of an instantiated
// template can stay in its template form, so the test reads the argument, not
// the default.  Returns true when the callee has a default argument; the
// function then walks the operands of the call itself.
bool walk_call_without_immediate_defaults(tree call, BodyWalk& walk) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || !has_default_argument(callee)) {
        return false;
    }
    bool is_aggregate = TREE_CODE(call) == AGGR_INIT_EXPR;
    walk_expression(is_aggregate ? &AGGR_INIT_EXPR_FN(call) : &CALL_EXPR_FN(call), walk);
    walk_expression(is_aggregate ? &AGGR_INIT_EXPR_SLOT(call) : &CALL_EXPR_STATIC_CHAIN(call), walk);
    int count = is_aggregate ? aggr_init_expr_nargs(call) : call_expr_nargs(call);
    tree parameter = TYPE_ARG_TYPES(TREE_TYPE(callee));
    for (int index = 0; index < count; ++index) {
        tree* argument = is_aggregate ? &AGGR_INIT_EXPR_ARG(call, index) : &CALL_EXPR_ARG(call, index);
        bool is_copied_default =
            parameter != NULL_TREE && TREE_PURPOSE(parameter) != NULL_TREE && holds_immediate_call(*argument);
        if (!is_copied_default) {
            walk_expression(argument, walk);
        }
        if (parameter != NULL_TREE) {
            parameter = TREE_CHAIN(parameter);
        }
    }
    return true;
}

// One declaration that the code names, at the location of the expression that
// holds it.
// ARGUMENTS is the argument count when NAMED is the callee of a call, or -1.
// EXPLICIT_ARGUMENTS are the explicit template arguments when a template-id
// names NAMED, or null.
void check_named(tree named, location_t location, int arguments = -1, tree explicit_arguments = NULL_TREE) {
    switch (TREE_CODE(named)) {
        case FUNCTION_DECL:
            check_function_use(named, location, explicit_arguments);
            break;
        case VAR_DECL:
            check_variable_use(named, location);
            break;
        case TEMPLATE_DECL:
            check_template_use(named, location, explicit_arguments);
            break;
        case OVERLOAD:
            check_overload_use(named, location, arguments, explicit_arguments);
            break;
        case BASELINK:
            check_overload_use(BASELINK_FUNCTIONS(named), location, arguments, explicit_arguments);
            break;
        default:
            break;
    }
}

// Each declaration among the operands of NODE.  A list and a braced
// initializer hold operands too.
void check_named_operands(tree node, location_t location) {
    // A type is one shared node too.  The source names it in a cast, in a
    // compound literal and in an explicit template argument.
    switch (TREE_CODE(node)) {
        case CAST_EXPR:
        case STATIC_CAST_EXPR:
        case REINTERPRET_CAST_EXPR:
        case CONST_CAST_EXPR:
        case DYNAMIC_CAST_EXPR:
        case BIT_CAST_EXPR:
            check_type_use(TREE_TYPE(node), location);
            break;
        case CONSTRUCTOR:
            if (COMPOUND_LITERAL_P(node)) {
                check_type_use(TREE_TYPE(node), location);
            }
            break;
        case TEMPLATE_ID_EXPR:
            if (tree arguments = TREE_OPERAND(node, 1); arguments != NULL_TREE && TREE_CODE(arguments) == TREE_VEC) {
                for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
                    if (tree argument = TREE_VEC_ELT(arguments, index); argument != NULL_TREE && TYPE_P(argument)) {
                        check_type_use(argument, location);
                    }
                }
            }
            break;
        default:
            break;
    }
    // A builtin that a call names counts at the location of the call: the
    // front end can give the address of the callee the location of the next
    // token.
    if (TREE_CODE(node) == CALL_EXPR) {
        tree called = cp_get_callee_fndecl_nofold(node);
        if (called != NULL_TREE && DECL_IS_UNDECLARED_BUILTIN(called) && fndecl_built_in_p(called)) {
            check_builtin_use(called, location);
        }
    }
    if (EXPR_P(node)) {
        tree callee = TREE_CODE(node) == CALL_EXPR ? CALL_EXPR_FN(node) : NULL_TREE;
        int arguments = callee != NULL_TREE ? call_expr_nargs(node) : -1;
        tree explicit_arguments = TREE_CODE(node) == TEMPLATE_ID_EXPR ? TREE_OPERAND(node, 1) : NULL_TREE;
        for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
            if (tree operand = TREE_OPERAND(node, index)) {
                check_named(operand, location, operand == callee ? arguments : -1,
                            index == 0 ? explicit_arguments : NULL_TREE);
            }
        }
    } else if (TREE_CODE(node) == TREE_LIST) {
        if (TREE_VALUE(node) != NULL_TREE) {
            check_named(TREE_VALUE(node), location);
        }
    } else if (TREE_CODE(node) == CONSTRUCTOR) {
        vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(node);
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            if (tree value = (*elements)[index].value) {
                check_named(value, location);
            }
        }
    }
}

tree visit_body_node(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    BodyWalk& walk = *static_cast<BodyWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        // An unevaluated operand makes no object and calls nothing.
        case SIZEOF_EXPR:
        case ALIGNOF_EXPR:
        case NOEXCEPT_EXPR:
        case REQUIRES_EXPR:
        case TRAIT_EXPR:
        case REFLECT_EXPR:
        case STATIC_ASSERT:
        case DECLTYPE_TYPE:
            *walk_subtrees = 0;
            return NULL_TREE;
        // The type of an implicit conversion is not a name in the source.
        case IMPLICIT_CONV_EXPR:
            check_named_operands(node, walk.last);
            walk_expression(&TREE_OPERAND(node, 0), walk);
            *walk_subtrees = 0;
            return NULL_TREE;
        // A declaration is read where an expression names it as an operand.
        case FUNCTION_DECL:
        case VAR_DECL:
        case TEMPLATE_DECL:
        case OVERLOAD:
        case BASELINK:
            *walk_subtrees = 0;
            return NULL_TREE;
        default:
            break;
    }
    check_named_operands(node, walk.last);
    switch (TREE_CODE(node)) {
        case DECL_EXPR:
            check_local_decl(DECL_EXPR_DECL(node), walk);
            return NULL_TREE;
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                check_local_decl(variable, walk);
            }
            return NULL_TREE;
        case LAMBDA_EXPR:
            check_lambda(node, walk);
            return NULL_TREE;
        case CALL_EXPR:
        case AGGR_INIT_EXPR:
            if (!walk.is_pattern && walk_call_without_immediate_defaults(node, walk)) {
                *walk_subtrees = 0;
            }
            return NULL_TREE;
        case NEW_EXPR:
        case VEC_NEW_EXPR:
            check_new_expression(node, walk.last);
            return NULL_TREE;
        case DELETE_EXPR:
        case VEC_DELETE_EXPR:
            record(Kind::raw_new_delete, walk.last, "delete");
            return NULL_TREE;
        // Only the keyword __builtin_bit_cast, the macro va_arg and an asm
        // statement make these trees.
        case BIT_CAST_EXPR:
            record(Kind::compiler_builtin, walk.last, "__builtin_bit_cast");
            return NULL_TREE;
        case VA_ARG_EXPR:
            record(Kind::compiler_builtin, walk.last, "__builtin_va_arg");
            return NULL_TREE;
        case ASM_EXPR:
            record(Kind::inline_asm, walk.last, "asm");
            return NULL_TREE;
        case SCOPE_REF:
            check_scope_use(node, walk.last);
            *walk_subtrees = 0;
            return NULL_TREE;
        default:
            break;
    }
    if (TYPE_P(node)) {
        check_type_use(node, walk.last);
        *walk_subtrees = 0;
    }
    return NULL_TREE;
}

void walk_expression(tree* expression, BodyWalk& walk) {
    if (expression != nullptr && *expression != NULL_TREE) {
        check_named(*expression, walk.last);
        cp_walk_tree(expression, visit_body_node, &walk, walk.visited);
    }
}

// One walk of a body or an initializer, with its own set of visited nodes.
void walk_root(tree* root, location_t location, bool is_pattern) {
    hash_set<tree> visited;
    BodyWalk walk;
    walk.last = location;
    walk.is_pattern = is_pattern;
    walk.visited = &visited;
    walk_expression(root, walk);
}

void check_signature(tree function) {
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function)) {
        check_object(function, Role::return_value);
    }
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        if (!DECL_ARTIFICIAL(parameter)) {
            check_object(parameter, Role::parameter);
        }
    }
}

// The function that the front end makes for the dynamic initializers of the
// variables of a namespace, of the static data members and of each
// thread_local.  Its body holds each initializer with the location that the
// source gives it.
bool is_static_initializer(tree function) {
    tree name = DECL_NAME(function);
    if (!DECL_ARTIFICIAL(function) || name == NULL_TREE) {
        return false;
    }
    std::string text = identifier_text(name);
    return text.rfind("__static_initialization_and_destruction_", 0) == 0 || text == "__tls_init";
}

// The signature, and the body when IS_PATTERN or when the plugin reads it
// from PLUGIN_PRE_GENERICIZE.  The body of a static initializer has places in
// each file, so it gets no location gate.
void walk_body(tree function, bool is_pattern) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || state.walked.count(function) != 0) {
        return;
    }
    state.walked.insert(function);
    if (is_static_initializer(function)) {
        if (DECL_SAVED_TREE(function) != NULL_TREE) {
            walk_root(&DECL_SAVED_TREE(function), DECL_SOURCE_LOCATION(function), false);
        }
        return;
    }
    if (!is_worth_a_walk(DECL_SOURCE_LOCATION(function))) {
        return;
    }
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    check_signature(function);
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function)) {
        return;
    }
    tree body = DECL_SAVED_TREE(function);
    if (body != NULL_TREE) {
        walk_root(&body, DECL_SOURCE_LOCATION(function), is_pattern);
    }
}

// ── The walk of declarations ────────────────────────────────────────────

void walk_template(tree template_decl);

void walk_initializer(tree decl, bool is_pattern) {
    tree initial = DECL_INITIAL(decl);
    if (initial == NULL_TREE || initial == error_mark_node || !is_worth_a_walk(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    walk_root(&DECL_INITIAL(decl), DECL_SOURCE_LOCATION(decl), is_pattern);
}

void check_member_function(tree function, bool is_pattern) {
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    if (is_pattern) {
        walk_body(function, true);
    } else if (is_worth_a_walk(DECL_SOURCE_LOCATION(function))) {
        check_signature(function);
    }
}

void walk_class(tree type, bool is_pattern) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || state.walked.count(main_type) != 0) {
        return;
    }
    state.walked.insert(main_type);
    tree decl = TYPE_MAIN_DECL(main_type);
    if (decl == NULL_TREE || LAMBDA_TYPE_P(main_type) || !is_worth_a_walk(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    location_t class_location = DECL_SOURCE_LOCATION(decl);
    if (tree binfo = TYPE_BINFO(main_type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            tree base = BINFO_TYPE(BINFO_BASE_BINFO(binfo, index));
            LibraryClasses classes = library_class_in(base, 0);
            if (is_standard_class(classes.refused)) {
                record(Kind::std_entity, class_location, qualified_name(classes.refused));
            }
            record_pending_class(classes, class_location);
        }
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        switch (TREE_CODE(member)) {
            case FIELD_DECL:
                if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                    walk_class(TREE_TYPE(member), is_pattern);
                } else if (!DECL_ARTIFICIAL(member)) {
                    check_object(member, Role::member);
                    walk_initializer(member, is_pattern);
                }
                break;
            case VAR_DECL:
                check_object(member, Role::variable);
                walk_initializer(member, is_pattern);
                break;
            case FUNCTION_DECL:
                check_member_function(member, is_pattern);
                break;
            case TYPE_DECL:
                if (DECL_SELF_REFERENCE_P(member)) {
                    break;
                }
                if (DECL_IMPLICIT_TYPEDEF_P(member) && CLASS_TYPE_P(TREE_TYPE(member))) {
                    walk_class(TREE_TYPE(member), is_pattern);
                } else if (!DECL_ARTIFICIAL(member)) {
                    check_alias(member);
                }
                break;
            case TEMPLATE_DECL:
                walk_template(member);
                break;
            default:
                break;
        }
    }
    // A friend that a class template defines is not a member.
    if (is_pattern) {
        for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
            tree befriended = TREE_VALUE(entry);
            if (TREE_PURPOSE(entry) != NULL_TREE || befriended == NULL_TREE) {
                continue;
            }
            if (TREE_CODE(befriended) == TEMPLATE_DECL) {
                befriended = DECL_TEMPLATE_RESULT(befriended);
            }
            if (befriended != NULL_TREE && TREE_CODE(befriended) == FUNCTION_DECL) {
                walk_body(befriended, true);
            }
        }
    }
}

// A default template argument is spelled where the template is declared.
void check_template_defaults(tree template_decl) {
    tree levels = DECL_TEMPLATE_PARMS(template_decl);
    if (levels == NULL_TREE) {
        return;
    }
    tree parameters = INNERMOST_TEMPLATE_PARMS(levels);
    if (parameters == NULL_TREE || TREE_CODE(parameters) != TREE_VEC) {
        return;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(parameters); ++index) {
        tree parameter = TREE_VEC_ELT(parameters, index);
        if (parameter == NULL_TREE || parameter == error_mark_node || TREE_PURPOSE(parameter) == NULL_TREE
            || TREE_VALUE(parameter) == NULL_TREE || !DECL_P(TREE_VALUE(parameter))) {
            continue;
        }
        tree default_argument = TREE_PURPOSE(parameter);
        location_t location = DECL_SOURCE_LOCATION(TREE_VALUE(parameter));
        LibraryClasses classes = library_class_in_argument(default_argument, 0);
        if (classes.refused != NULL_TREE) {
            if (is_standard_class(classes.refused)) {
                record(Kind::std_entity, location, qualified_name(classes.refused));
            }
        } else if (tree named = TYPE_P(default_argument) ? library_type_name_in(default_argument, 0) : NULL_TREE) {
            record_standard_use(named, location, qualified_name(named));
        }
        record_pending_class(classes, location);
    }
}

// An explicit or partial specialization is not a member of its namespace:
// the template records it.  A quarantined file can specialize a template of
// fixy, so the walk reads the specializations of every template it meets.
void walk_specializations(tree template_decl) {
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
            walk_template(partial);
        }
    }
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree specialization = TREE_VALUE(entry);
        if (specialization != NULL_TREE && CLASS_TYPE_P(specialization)
            && CLASSTYPE_TEMPLATE_SPECIALIZATION(specialization) && !uses_template_parms(specialization)) {
            walk_class(specialization, false);
        }
    }
}

void walk_template(tree template_decl) {
    if (state.walked.count(template_decl) != 0) {
        return;
    }
    state.walked.insert(template_decl);
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result == NULL_TREE) {
        return;
    }
    bool is_class_template =
        TREE_CODE(result) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(result) && CLASS_TYPE_P(TREE_TYPE(result));
    if (is_class_template) {
        walk_specializations(template_decl);
    }
    if (!is_worth_a_walk(DECL_SOURCE_LOCATION(template_decl))) {
        return;
    }
    check_template_defaults(template_decl);
    switch (TREE_CODE(result)) {
        case TYPE_DECL:
            if (is_class_template) {
                walk_class(TREE_TYPE(result), true);
            } else {
                check_alias(result);
            }
            break;
        case FUNCTION_DECL:
            walk_body(result, true);
            break;
        case VAR_DECL:
            check_object(result, Role::variable);
            walk_initializer(result, true);
            break;
        default:
            break;
    }
}

void walk_namespace(tree ns);

void walk_namespace_member(tree decl) {
    switch (TREE_CODE(decl)) {
        case NAMESPACE_DECL:
            if (DECL_NAMESPACE_ALIAS(decl) != NULL_TREE) {
                tree target = ORIGINAL_NAMESPACE(decl);
                if (target != NULL_TREE && top_namespace(target) == std_node) {
                    record(Kind::std_entity, DECL_SOURCE_LOCATION(decl), qualified_name(target));
                }
            } else if (!is_library_namespace(decl)) {
                walk_namespace(decl);
            }
            return;
        case TREE_LIST:
            if (TREE_VALUE(decl) != NULL_TREE && TREE_CODE(TREE_VALUE(decl)) != TREE_LIST) {
                walk_namespace_member(TREE_VALUE(decl));
            }
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(decl, true); candidate; ++candidate) {
                if (TREE_CODE(*candidate) != OVERLOAD) {
                    walk_namespace_member(*candidate);
                }
            }
            return;
        default:
            break;
    }
    if (!DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl) || in_system_header_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case TEMPLATE_DECL:
            walk_template(decl);
            break;
        case TYPE_DECL:
            if (DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                walk_class(TREE_TYPE(decl), false);
            } else if (!DECL_ARTIFICIAL(decl)) {
                check_alias(decl);
            }
            break;
        case FUNCTION_DECL:
            // A friend that a class template injects is an instantiation.
            if (!has_lang_template_info(decl) || DECL_TEMPLATE_SPECIALIZATION(decl)) {
                check_member_function(decl, false);
            }
            break;
        case VAR_DECL:
            if (!has_lang_template_info(decl) && !DECL_ARTIFICIAL(decl)) {
                check_object(decl, Role::variable);
                walk_initializer(decl, false);
            }
            break;
        default:
            break;
    }
}

void walk_namespace(tree ns) {
    if (state.walked.count(ns) != 0) {
        return;
    }
    state.walked.insert(ns);
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        walk_namespace_member(decl);
    }
}

// ── The report ──────────────────────────────────────────────────────────

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char byte : text) {
        hash = (hash ^ byte) * 0x100000001b3ULL;
    }
    return hash;
}

std::string report_path() {
    std::string key = std::string(main_input_filename != nullptr ? main_input_filename : "") + '\n'
                    + (dump_dir_name != nullptr ? dump_dir_name : "") + '\n'
                    + (dump_base_name != nullptr ? dump_base_name : "");
    std::string base = main_input_filename != nullptr ? std::string(main_input_filename) : std::string("unit");
    std::size_t slash = base.rfind('/');
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    std::uint64_t hash = fnv1a(key);
    std::string hash_text(16, '0');
    for (int digit = 15; digit >= 0; --digit, hash >>= 4) {
        hash_text[static_cast<std::size_t>(digit)] = "0123456789abcdef"[hash & 0xF];
    }
    return state.out_dir + "/" + base + "." + hash_text + ".quarantine";
}

// The report replaces the last report of the same object in one rename, so a
// reader never sees half of it.  system.h of GCC renames the stdio functions,
// so the calls are not qualified.
void write_report_file(const std::vector<std::string>& lines) {
    ::mkdir(state.out_dir.c_str(), 0775);
    std::string path = report_path();
    std::string temporary = path + ".tmp." + std::to_string(::getpid());
    FILE* stream = fopen(temporary.c_str(), "w");
    if (stream == nullptr) {
        warning(0, "quarantine: cannot write %s: %m", temporary.c_str());
        return;
    }
    std::string text = std::string("# unit ") + (main_input_filename != nullptr ? main_input_filename : "") + "\n";
    for (const std::string& line : lines) {
        text += line;
        text += '\n';
    }
    bool was_written = fwrite(text.data(), 1, text.size(), stream) == text.size();
    was_written = fclose(stream) == 0 && was_written;
    if (!was_written || rename(temporary.c_str(), path.c_str()) != 0) {
        warning(0, "quarantine: cannot write %s: %m", path.c_str());
        remove(temporary.c_str());
    }
}

// True while the assembler output of an object is open: at PLUGIN_FINISH_UNIT
// of a unit that is not -fsyntax-only.  At PLUGIN_FINISH the output is closed.
bool writes_section() { return asm_out_file != nullptr && !flag_syntax_only; }

// Writes the report into the section of the object (THE SECTION above).  An
// .ascii string escapes '"', '\' and each byte that is not printable ASCII.
void write_section(const std::vector<std::string>& lines) {
    std::string text = "\t.pushsection\t.crucible.quarantine,\"e\",%progbits\n";
    auto append_line = [&text](const std::string& line) {
        text += "\t.ascii\t\"";
        for (unsigned char byte : line) {
            if (byte == '"' || byte == '\\') {
                text += '\\';
                text += static_cast<char>(byte);
            } else if (byte < 0x20 || byte >= 0x7f) {
                char escaped[8];
                snprintf(escaped, sizeof escaped, "\\%03o", static_cast<unsigned>(byte));
                text += escaped;
            } else {
                text += static_cast<char>(byte);
            }
        }
        text += "\\n\"\n";
    };
    append_line("# crucible-quarantine 1 stamp=" + state.stamp);
    for (const std::string& line : lines) {
        append_line(line);
    }
    text += "\t.popsection\n";
    fwrite(text.data(), 1, text.size(), asm_out_file);
}

// The mode stamp of each file with a finding that a mode can make an error,
// in the dependency file of the unit (THE DEPENDENCIES).  Complexity: O(findings).
void add_mode_dependencies() {
    class mkdeps* dependencies = cpp_get_deps(parse_in);
    if (dependencies == nullptr || state.stamps_dir.empty()) {
        return;
    }
    std::unordered_set<const FileEntry*> files;
    bool needs_enforce = false;
    for (const Finding& finding : core.findings) {
        if (finding.kind == kContractKind || finding.file == nullptr || is_opted_out(finding)
            || !files.insert(finding.file).second) {
            continue;
        }
        std::string stamp = state.stamps_dir + "/modes/" + finding.file->relative;
        struct stat status;
        if (::stat(stamp.c_str(), &status) == 0 && S_ISREG(status.st_mode)) {
            deps_add_dep(dependencies, stamp.c_str());
        } else {
            needs_enforce = true;
        }
    }
    if (needs_enforce) {
        deps_add_dep(dependencies, (state.stamps_dir + "/enforce.txt").c_str());
    }
}

// The report of the unit.  CAN_ADD_DEPENDENCIES is true at PLUGIN_FINISH_UNIT,
// before GCC writes the dependency file.
void report(bool can_add_dependencies) {
    if (core.was_reported) {
        return;
    }
    core.was_reported = true;
    finish_pending_include(nullptr);
    report_open_regions();
    report_unclassified();
    if (can_add_dependencies) {
        add_mode_dependencies();
    }
    bool has_section = writes_section();
    // An admit row with `unless MACRO` refuses each unit that defines MACRO.
    for (const AdmitRow& admit : core.table.admits) {
        const std::string& macro = admit.unless_macro;
        if (!macro.empty()
            && cpp_defined(parse_in, reinterpret_cast<const unsigned char*>(macro.c_str()),
                           static_cast<int>(macro.size()))) {
            error("quarantine: the unit defines %s, and the rule table admits %s%s%s only in a unit that does not "
                  "define it; remove the definition",
                  macro.c_str(), admit.is_header ? "<" : "", admit.entry.c_str(), admit.is_header ? ">" : "");
        }
    }
    std::vector<std::string> lines;
    for (const Finding& finding : core.findings) {
        bool is_out = is_opted_out(finding);
        std::string kind = is_out ? std::string("opted_out") : std::string(finding.kind);
        std::string entity = is_out ? std::string(finding.kind) + " " + finding.entity : finding.entity;
        lines.push_back("quarantine: " + kind + " " + finding.file->relative + ":" + std::to_string(finding.line) + ":"
                        + std::to_string(finding.column) + " " + entity);
        bool is_error = state.is_error_mode || finding.file->is_error_mode;
        // The contract rule gives an error in each mode.
        if (finding.kind == kContractKind && !is_out) {
            report_contract(finding);
        } else if (is_error && !is_out && is_include_kind(finding.kind)) {
            error_at(finding.spelling,
                     "quarantine: %s %s; the rule table does not let this file include the header, so include a "
                     "header that its layer can include",
                     kind.c_str(), entity.c_str());
        } else if (is_error && !is_out) {
            error_at(finding.spelling,
                     "quarantine: %s %s; use a type or an entity of fixy or foundation.  A region of %qs is "
                     "only for a reason class of CLAUDE.md section XXII",
                     kind.c_str(), entity.c_str(), kBeginMacro);
        } else if (!is_error && state.out_dir.empty() && !has_section) {
            inform(finding.spelling, "quarantine: %s %s", kind.c_str(), entity.c_str());
        }
    }
    // Each region, for the ledger of the regions.  A region is no finding.
    for (const Region& region : core.regions) {
        std::string place = region.file->relative + ":" + std::to_string(region.begin_line) + ":1";
        lines.push_back("quarantine: region " + place + " " + region.reason);
    }
    if (has_section) {
        write_section(lines);
    }
    if (!state.out_dir.empty()) {
        write_report_file(lines);
    }
}

// ── The include rules ───────────────────────────────────────────────────

bool is_include_kind(const char* kind) {
    return kind == kind_name(Kind::layer_header) || kind == kind_name(Kind::door_header)
        || kind == kind_name(Kind::upward_include);
}

const std::string& layer_name(int layer) { return core.table.layers[static_cast<std::size_t>(layer)].name; }

int layer_rank(int layer) { return core.table.layers[static_cast<std::size_t>(layer)].rank; }

// The target of a directive that entered no file.  An include guard or
// #pragma once stopped it, so the unit entered the target before.  The plugin
// looks for the file as libcpp does: beside the includer for a quoted name,
// and then in each directory of the search chain of the directive, in order.
// An #include_next starts after the directory of the chain that holds the
// includer.  Returns null when no directory holds the name, which the rules
// treat as a header outside the root.  Complexity: O(directories of the chain).
const FileEntry* resolve_skipped_include(const PendingInclude& pending) {
    const std::string& includer = pending.place.file->real;
    std::string includer_directory = includer.substr(0, includer.rfind('/'));
    if (!pending.is_angle && !pending.is_next && !includer.empty()) {
        std::string beside = real_path((includer_directory + "/" + pending.spelled).c_str());
        if (!beside.empty()) {
            return &entry_of_real_path(beside);
        }
    }
    cpp_dir* first = get_added_cpp_dirs(pending.is_angle ? INC_BRACKET : INC_QUOTE);
    if (pending.is_next && !includer.empty()) {
        for (cpp_dir* directory = first; directory != nullptr; directory = directory->next) {
            if (real_path(directory->name) == includer_directory) {
                first = directory->next;
                break;
            }
        }
    }
    for (cpp_dir* directory = first; directory != nullptr; directory = directory->next) {
        std::string candidate = real_path((std::string(directory->name) + "/" + pending.spelled).c_str());
        if (!candidate.empty()) {
            return &entry_of_real_path(candidate);
        }
    }
    return nullptr;
}

// The include rules for one directive and its target.
void check_include(const PendingInclude& pending, const FileEntry* target) {
    const FileEntry& includer = *pending.place.file;
    if (target != nullptr && target->file_class != FileClass::outside) {
        if (includer.file_class != FileClass::base
            || (target->file_class == FileClass::base && layer_rank(target->layer) <= layer_rank(includer.layer))) {
            return;
        }
        std::string what =
            target->file_class == FileClass::base ? "the layer " + layer_name(target->layer) : std::string("no layer");
        add_finding(Kind::upward_include, pending.place,
                    target->relative + " (" + what + ") from the layer " + layer_name(includer.layer));
        return;
    }
    for (const DoorRow& door : core.table.doors) {
        if (door.header == pending.spelled) {
            if (includer.relative != door.owner) {
                add_finding(Kind::door_header, pending.place, "<" + pending.spelled + "> has the door " + door.owner);
            }
            return;
        }
    }
    if (includer.file_class != FileClass::base) {
        return;
    }
    for (const auto& [layer, header] : core.table.allows) {
        if (header == pending.spelled && layer_rank(layer) <= layer_rank(includer.layer)) {
            return;
        }
    }
    add_finding(Kind::layer_header, pending.place,
                "<" + pending.spelled + "> in the layer " + layer_name(includer.layer));
}

void finish_pending_include(const FileEntry* entered) {
    if (!state.pending.is_set) {
        return;
    }
    PendingInclude pending = std::move(state.pending);
    state.pending = PendingInclude{};
    check_include(pending, entered != nullptr ? entered : resolve_skipped_include(pending));
}

// The include callback of libcpp: one #include directive, before the
// preprocessor looks for the file.
void on_include(cpp_reader* reader, location_t location, const unsigned char* directive, const char* name, int is_angle,
                const cpp_token** comments) {
    if (state.previous_include != nullptr) {
        state.previous_include(reader, location, directive, name, is_angle, comments);
    }
    finish_pending_include(nullptr);
    auto header = is_angle != 0 && name != nullptr ? state.admitted_headers.find(name) : state.admitted_headers.end();
    state.admitted_header_pending = header != state.admitted_headers.end() ? header->second : nullptr;
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr || name == nullptr) {
        return;
    }
    state.pending.is_set = true;
    state.pending.place = place;
    state.pending.spelled = name;
    state.pending.is_angle = is_angle != 0;
    state.pending.is_next =
        directive != nullptr && std::strcmp(reinterpret_cast<const char*>(directive), "include_next") == 0;
}

// The macro callback of libcpp: one expansion of a macro, after libcpp read
// its arguments.  libcpp also calls it for a test of a macro in a directive,
// with the location of the directive line, where the source spells no
// identifier at the start.
void on_macro_used(cpp_reader* reader, location_t location, cpp_hashnode* node) {
    if (state.previous_used != nullptr) {
        state.previous_used(reader, location, node);
    }
    if (is_system_assert(node) && identifier_spelled_at(location) == "assert") {
        record(Kind::assert_expansion, location, "assert");
    }
}

// Each file that the preprocessor enters.  The file that the last directive
// names enters right after the include callback, with that directive as the
// place it comes from.
void on_include_file(void* gcc_data, void*) {
    const char* name = static_cast<const char*>(gcc_data);
    if (name == nullptr) {
        return;
    }
    const FileEntry& entry = classify_file(name);
    const line_map_ordinary* map = LINEMAPS_LAST_ORDINARY_MAP(line_table);
    if (map == nullptr || map->reason != LC_ENTER) {
        return;
    }
    if (state.admitted_header_pending != nullptr) {
        state.admitted_header_paths.emplace(entry.real, state.admitted_header_pending);
        state.admitted_header_pending = nullptr;
    }
    if (!state.pending.is_set) {
        return;
    }
    expanded_location from = expand_location(linemap_included_from(map));
    if (from.file != nullptr && &classify_file(from.file) == state.pending.place.file) {
        finish_pending_include(&entry);
    }
}

// ── The callbacks ───────────────────────────────────────────────────────

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || is_in_instantiation(function)) {
        return;
    }
    walk_body(function, false);
}

void walk_declarations() {
    walk_contract_namespace(global_namespace);
    walk_namespace(global_namespace);
}

void on_finish_unit(void*, void*) {
    walk_declarations();
    report(true);
}

// A unit that stopped on an error, or that -fsyntax-only compiled, does not
// reach PLUGIN_FINISH_UNIT.  Under -fsyntax-only the trees are complete, so
// the walk of declarations runs here.  GCC wrote the dependency file before
// PLUGIN_FINISH, so the report adds no dependency.
void on_finish(void*, void*) {
    if (!core.was_reported && flag_syntax_only && !seen_error()) {
        walk_declarations();
    }
    report(false);
}

void on_collection(void*, void*) {
    for (int use = 0; use < 2; ++use) {
        state.library_class_of[use].clear();
        state.admitting_row_of[use].clear();
    }
    state.default_argument_of.clear();
    state.walked.clear();
    core.contract_walked.clear();
}

}  // namespace

int plugin_init(plugin_name_args* plugin_info, plugin_gcc_version* version) {
    if (!plugin_default_version_check(version, &gcc_version)) {
        error("quarantine: the plugin was built for GCC %s, and this compiler is a different version",
              gcc_version.basever);
        return 1;
    }
    // A preprocessing run builds no tree, and its empty report must not
    // replace the report of the unit.
    if (flag_preprocess_only) {
        return 0;
    }
    core.plugin_name = plugin_info->base_name;
    std::string root_argument;
    std::string rules_argument;
    for (int index = 0; index < plugin_info->argc; ++index) {
        std::string key = plugin_info->argv[index].key;
        std::string value = plugin_info->argv[index].value != nullptr ? plugin_info->argv[index].value : "";
        if (key == "root") {
            root_argument = value;
        } else if (key == "build") {
            if (!set_build(value)) {
                return 1;
            }
        } else if (key == "rules") {
            rules_argument = value;
        } else if (key == "out") {
            state.out_dir = value;
        } else if (key == "stamps") {
            state.stamps_dir = value;
        } else if (key == "mode") {
            if (value != "report" && value != "error") {
                error("quarantine: mode is %qs; the modes are report and error", value.c_str());
                return 1;
            }
            state.is_error_mode = value == "error";
        } else if (key == "stamp") {
            state.stamp = value;
        } else {
            error("quarantine: unknown argument %qs; the arguments are root, build, rules, mode, out, stamps and "
                  "stamp",
                  key.c_str());
            return 1;
        }
    }
    if (!set_root(root_argument)) {
        return 1;
    }
    if (rules_argument.empty()) {
        error("quarantine: give the rule table with %<-fplugin-arg-%s-rules=PATH%>", core.plugin_name.c_str());
        return 1;
    }
    if (!load_rule_table(rules_argument)) {
        return 1;
    }
    for (const AdmitRow& admit : core.table.admits) {
        (admit.is_header ? state.admitted_headers : state.admitted_names).emplace(admit.entry, &admit);
    }
    cpp_callbacks* callbacks = cpp_get_callbacks(parse_in);
    state.previous_include = callbacks->include;
    callbacks->include = on_include;
    state.previous_used = callbacks->used;
    callbacks->used = on_macro_used;
    register_callback(plugin_info->base_name, PLUGIN_INCLUDE_FILE, on_include_file, nullptr);
    register_contract_rule(plugin_info->base_name);
    register_callback(plugin_info->base_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT, on_finish_unit, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH, on_finish, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_START, on_collection, nullptr);
    return 0;
}
