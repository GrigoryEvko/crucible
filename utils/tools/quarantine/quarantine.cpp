// The quarantine plugin of GCC.
//
// Every file under the source root outside the base and the build directory is
// quarantined (THE FILES in plugin_core.h).  The base is include/foundation/,
// include/fixy/, src/foundation/ and src/fixy/.  In a quarantined file, each
// object must have a type that fixy or foundation gives, and the code must not
// name a library entity that is not admitted.  The plugin reads the C++ trees
// of the translation unit and records each place that breaks the rule.  The
// location rule decides what it checks, and the target does not: a use is a
// finding only when its spelling location is in a quarantined file.  A use
// inside a base file, and a fixy template that a crucible type instantiates,
// is not a finding.
//
// The plugin also applies the contract rule of the tree, from plugin_core.h,
// in each mode: each P2900 contract specifier that no region opts out is an
// error.  CMake loads this plugin only when CRUCIBLE_QUARANTINE is REPORT or
// ERROR.  Each other build loads the contract plugin of contract.cpp, which
// applies only the contract rule, so a change of this file does not compile
// the objects of that build again.
//
// THE KINDS
//     std_entity            a named reference to a declaration in namespace std
//     std_object            a variable, data member, parameter or return type whose
//                           type holds a class of the standard library
//     c_library_object      the same, for a C struct or union of a system header
//     raw_pointer_object    a variable, data member or parameter of type T*
//     raw_function_pointer  the same, for a pointer to a function or to a member
//     c_array_object        a variable or data member of type T[N]
//     raw_new_delete        a new-expression or a delete-expression
//     c_library_call        a use of a function of a system header in the global
//                           namespace
//     contract_specifier    a `pre` or a `post` contract specifier (the contract
//                           rule, in each file under the root and in each mode)
//     opted_out             one of the kinds above, inside a region that
//                           #pragma crucible I_KNOW_WHAT_IM_DOING("reason") opens
//
// THE HOOKS
//     PLUGIN_PRE_GENERICIZE  the body of each function that is not a template:
//                            local objects, calls, uses of variables, new and
//                            delete.  A template instantiation is skipped, because
//                            its pattern is read instead: a dependent name that
//                            only the instantiation resolves is not a named
//                            reference in the source.
//     PLUGIN_FINISH_UNIT     one walk of every namespace that a system header does
//                            not own: namespace-scope objects, classes and their
//                            data members, bases, aliases, function signatures,
//                            and the bodies of template patterns.  The contract
//                            rule has its own walk.
//     PLUGIN_FINISH          the report, if the unit did not reach its end.
//     plugin_core.h gives the hooks of the contract rule and of the two opt-out
//     pragmas.
//
// THE ARGUMENTS (-fplugin-arg-crucible_quarantine-NAME=VALUE)
//     root=PATH      the source root (necessary)
//     build=PATH     a build directory under the root, whose files are generated
//     admitted=PATH  the list of admitted standard library entities
//     mode=report    each finding is a note, or a line of the report file
//     mode=error     each finding that no region opts out is an error
//     out=DIR        write the report of the unit to a file in DIR, not as notes
//     stamp=TEXT     ignored; a new value makes the build system compile again
//
// The plugin cannot see what the front end keeps as no tree, or as a tree with
// no source location: a use in an unevaluated operand, a dependent member of a
// template, a using-declaration, a default argument that no call uses, and the
// dynamic initializer of a namespace-scope variable.  A constructor, a
// destructor and a conversion function of a library class are not findings:
// the compiler calls them for an object that the plugin reports where the code
// declares it.

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
#include "tree-iterator.h"

int plugin_is_GPL_compatible;

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

// ── The state of one translation unit ───────────────────────────────────

// The state of the quarantine rule.  plugin_core.h holds the state that the
// two rules share: the root, the files, the findings and the regions.
struct State {
    std::string out_dir;
    bool is_error_mode = false;

    std::unordered_set<std::string> admitted_names;
    std::vector<std::string> admitted_headers;  // "/type_traits", matched as a path suffix

    // Caches keyed by tree.  A garbage collection can free a tree and reuse
    // its address, so each collection clears them.  Nothing depends on them
    // for correctness: a finding is recorded one time for each place.
    std::unordered_map<tree, tree> library_class_of;
    std::unordered_map<tree, bool> admitted_of;
    std::unordered_map<tree, bool> default_argument_of;
    std::unordered_set<tree> walked;
};

State state;

// ── Paths and places ────────────────────────────────────────────────────

bool has_suffix(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

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

void record(Kind kind, location_t location, const std::string& entity) {
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class != FileClass::quarantined) {
        return;
    }
    // One finding for each kind and place.  Two names at one place, such as a
    // type and its member function in one expression, are two findings.
    bool is_name = kind == Kind::std_entity || kind == Kind::c_library_call;
    std::string key = std::string(kind_name(kind)) + ' ' + place.file->relative + ':' + std::to_string(place.line) + ':'
                    + std::to_string(place.column) + (is_name ? ' ' + entity : std::string{});
    if (!core.finding_keys.insert(key).second) {
        return;
    }
    Finding finding;
    finding.kind = kind_name(kind);
    finding.file = place.file;
    finding.line = place.line;
    finding.column = place.column;
    finding.spelling = place.spelling;
    finding.entity = entity;
    core.findings.push_back(std::move(finding));
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

bool is_admitted_name(const std::string& name) {
    if (state.admitted_names.count(name) != 0) {
        return true;
    }
    for (std::size_t colon = name.find("::"); colon != std::string::npos; colon = name.find("::", colon + 2)) {
        if (state.admitted_names.count(name.substr(0, colon)) != 0) {
            return true;
        }
    }
    return false;
}

bool is_admitted(tree decl) {
    auto cached = state.admitted_of.find(decl);
    if (cached != state.admitted_of.end()) {
        return cached->second;
    }
    bool is_listed = is_admitted_name(qualified_name(decl));
    if (!is_listed) {
        const char* file = DECL_SOURCE_FILE(decl);
        if (file != nullptr) {
            std::string path(file);
            is_listed = std::any_of(state.admitted_headers.begin(), state.admitted_headers.end(),
                                    [&path](const std::string& header) { return has_suffix(path, header); });
        }
    }
    state.admitted_of.emplace(decl, is_listed);
    return is_listed;
}

// ── The library class inside a type ─────────────────────────────────────

tree library_class_in(tree type, int depth);

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

tree library_class_in_argument(tree argument, int depth) {
    if (argument == NULL_TREE || argument == error_mark_node) {
        return NULL_TREE;
    }
    if (ARGUMENT_PACK_P(argument)) {
        tree arguments = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
            if (tree found = library_class_in_argument(TREE_VEC_ELT(arguments, index), depth + 1)) {
                return found;
            }
        }
        return NULL_TREE;
    }
    if (TYPE_P(argument)) {
        return library_class_in(argument, depth + 1);
    }
    if (TREE_CODE(argument) == TEMPLATE_DECL && library_of(argument) != Library::none && !is_admitted(argument)) {
        tree result = DECL_TEMPLATE_RESULT(argument);
        if (result != NULL_TREE && TREE_CODE(result) == TYPE_DECL) {
            return TREE_TYPE(result);
        }
    }
    return NULL_TREE;
}

// The template arguments of a class that is not a library class.  An
// argument equal to the default of its parameter is skipped: the declaration
// did not write it, so a fixy default such as the storage of AppendOnly is not
// a finding at each object.
tree library_class_in_template_arguments(tree class_type, int depth) {
    tree info = CLASSTYPE_TEMPLATE_INFO(class_type);
    if (TI_ARGS(info) == NULL_TREE) {
        return NULL_TREE;
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
        return NULL_TREE;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (parameters != NULL_TREE && index < TREE_VEC_LENGTH(parameters)) {
            tree parameter = TREE_VEC_ELT(parameters, index);
            if (parameter != NULL_TREE && parameter != error_mark_node && TREE_PURPOSE(parameter) != NULL_TREE
                && matches_default(argument, TREE_PURPOSE(parameter))) {
                continue;
            }
        }
        if (tree found = library_class_in_argument(argument, depth)) {
            return found;
        }
    }
    return NULL_TREE;
}

// The first class of the standard library or of a C header that TYPE holds.
// The walk looks through pointers, references, arrays, template arguments and
// the enclosing class, and stops at an admitted class.  A function type is not
// read: a callback that takes a std::string is not an object of that type.
tree library_class_in(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > 64) {
        return NULL_TREE;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
            if (FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type))) {
                return NULL_TREE;
            }
            return library_class_in(TREE_TYPE(type), depth + 1);
        case ARRAY_TYPE:
            return library_class_in(TREE_TYPE(type), depth + 1);
        case TYPENAME_TYPE:
            return library_class_in(TYPE_CONTEXT(type), depth + 1);
        case TYPE_PACK_EXPANSION:
            return library_class_in(PACK_EXPANSION_PATTERN(type), depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE:
            break;
        default:
            return NULL_TREE;
    }
    if (TYPE_PTRMEMFUNC_P(type)) {
        return NULL_TREE;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    auto cached = state.library_class_of.find(main_type);
    if (cached != state.library_class_of.end()) {
        return cached->second;
    }
    tree found = NULL_TREE;
    tree decl = TYPE_MAIN_DECL(main_type);
    if (decl != NULL_TREE && !LAMBDA_TYPE_P(main_type)) {
        if (library_of(decl) != Library::none) {
            found = is_admitted(decl) ? NULL_TREE : main_type;
        } else {
            if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
                found = library_class_in_template_arguments(main_type, depth);
            }
            tree context = CP_TYPE_CONTEXT(main_type);
            if (found == NULL_TREE && context != NULL_TREE && TYPE_P(context)) {
                found = library_class_in(context, depth + 1);
            }
        }
    }
    state.library_class_of.emplace(main_type, found);
    return found;
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
    if (tree found = library_class_in(type, 0)) {
        Kind kind = library_of(TYPE_MAIN_DECL(found)) == Library::c_library ? Kind::c_library_object : Kind::std_object;
        record(kind, location, qualified_name(found) + " (" + type_text(type) + ")");
    }
}

void check_alias(tree decl) {
    tree aliased = DECL_ORIGINAL_TYPE(decl);
    if (aliased == NULL_TREE || !is_quarantined(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    tree found = library_class_in(aliased, 0);
    if (found != NULL_TREE && library_of(TYPE_MAIN_DECL(found)) == Library::standard) {
        record(Kind::std_entity, DECL_SOURCE_LOCATION(decl), qualified_name(found));
    }
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

// One function that the code names.  Returns true when it made a finding.
bool check_function_use(tree function, location_t location) {
    if (TREE_CODE(function) == TEMPLATE_DECL) {
        function = DECL_TEMPLATE_RESULT(function);
    }
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_IS_UNDECLARED_BUILTIN(function)) {
        return false;
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
            if (is_admitted(function)) {
                return false;
            }
            record(Kind::std_entity, location, qualified_name(function));
            return true;
        case Library::c_library:
            record(Kind::c_library_call, location, qualified_name(function));
            return true;
        case Library::none:
            return false;
    }
    return false;
}

void check_overload_use(tree overload, location_t location) {
    for (ovl_iterator candidate(overload, true); candidate; ++candidate) {
        tree function = *candidate;
        if (TREE_CODE(function) == OVERLOAD) {
            check_overload_use(function, location);
            return;
        }
        if (check_function_use(function, location)) {
            return;
        }
    }
}

// A variable that the code names.  Only a variable of a namespace or a
// static data member can be a library entity.  A constexpr variable of an
// enumeration or an arithmetic type, such as std::memory_order_acquire, is a
// named constant: it makes no object, as an enumerator makes none.
void check_variable_use(tree variable, location_t location) {
    if (DECL_ARTIFICIAL(variable) || !(DECL_NAMESPACE_SCOPE_P(variable) || DECL_CLASS_SCOPE_P(variable))) {
        return;
    }
    tree type = TREE_TYPE(variable);
    if (DECL_DECLARED_CONSTEXPR_P(variable) && type != NULL_TREE
        && (TREE_CODE(type) == ENUMERAL_TYPE || ARITHMETIC_TYPE_P(type))) {
        return;
    }
    if (library_of(variable) == Library::standard && !is_admitted(variable)) {
        record(Kind::std_entity, location, qualified_name(variable));
    }
}

void check_template_use(tree template_decl, location_t location) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL) {
        check_function_use(template_decl, location);
        return;
    }
    if (library_of(template_decl) == Library::standard && !is_admitted(template_decl)) {
        record(Kind::std_entity, location, qualified_name(template_decl));
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
            if (!is_admitted_name(name)) {
                record(Kind::std_entity, location, name);
            }
        }
        return;
    }
    if (TYPE_P(scope)) {
        tree found = library_class_in(scope, 0);
        if (found != NULL_TREE && library_of(TYPE_MAIN_DECL(found)) == Library::standard) {
            record(Kind::std_entity, location, qualified_name(found) + "::" + member);
        }
    }
}

// A type that a template names: the type of a cast, an explicit template
// argument, a compound literal.
void check_type_use(tree type, location_t location) {
    tree found = library_class_in(type, 0);
    if (found != NULL_TREE && library_of(TYPE_MAIN_DECL(found)) == Library::standard) {
        record(Kind::std_entity, location, qualified_name(found));
    }
}

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
void check_named(tree named, location_t location) {
    switch (TREE_CODE(named)) {
        case FUNCTION_DECL:
            check_function_use(named, location);
            break;
        case VAR_DECL:
            check_variable_use(named, location);
            break;
        case TEMPLATE_DECL:
            check_template_use(named, location);
            break;
        case OVERLOAD:
            check_overload_use(named, location);
            break;
        case BASELINK:
            check_overload_use(BASELINK_FUNCTIONS(named), location);
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
    if (EXPR_P(node)) {
        for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
            if (tree operand = TREE_OPERAND(node, index)) {
                check_named(operand, location);
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

// The signature, and the body when IS_PATTERN or when the plugin reads it
// from PLUGIN_PRE_GENERICIZE.
void walk_body(tree function, bool is_pattern) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || state.walked.count(function) != 0) {
        return;
    }
    state.walked.insert(function);
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
            tree found = library_class_in(base, 0);
            if (found != NULL_TREE && library_of(TYPE_MAIN_DECL(found)) == Library::standard) {
                record(Kind::std_entity, class_location, qualified_name(found));
            }
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
        tree found = library_class_in_argument(TREE_PURPOSE(parameter), 0);
        if (found != NULL_TREE && library_of(TYPE_MAIN_DECL(found)) == Library::standard) {
            record(Kind::std_entity, DECL_SOURCE_LOCATION(TREE_VALUE(parameter)), qualified_name(found));
        }
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

void report() {
    if (core.was_reported) {
        return;
    }
    core.was_reported = true;
    report_open_regions();
    std::vector<std::string> lines;
    for (const Finding& finding : core.findings) {
        bool is_out = is_opted_out(finding);
        std::string kind = is_out ? std::string("opted_out") : std::string(finding.kind);
        std::string entity = is_out ? std::string(finding.kind) + " " + finding.entity : finding.entity;
        lines.push_back("quarantine: " + kind + " " + finding.file->relative + ":" + std::to_string(finding.line) + ":"
                        + std::to_string(finding.column) + " " + entity);
        // The contract rule gives an error in each mode.
        if (finding.kind == kContractKind && !is_out) {
            report_contract(finding);
        } else if (state.is_error_mode && !is_out) {
            error_at(finding.spelling,
                     "quarantine: %s %s; use a type or an entity of fixy or foundation, or put the code in a "
                     "%<#pragma crucible %s(\"reason\")%> region",
                     kind.c_str(), entity.c_str(), kBeginPragma);
        } else if (!state.is_error_mode && state.out_dir.empty()) {
            inform(finding.spelling, "quarantine: %s %s", kind.c_str(), entity.c_str());
        }
    }
    if (!state.out_dir.empty()) {
        write_report_file(lines);
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
    report();
}

// A unit that stopped on an error, or that -fsyntax-only compiled, does not
// reach PLUGIN_FINISH_UNIT.  Under -fsyntax-only the trees are complete, so
// the walk of declarations runs here.
void on_finish(void*, void*) {
    if (!core.was_reported && flag_syntax_only && !seen_error()) {
        walk_declarations();
    }
    report();
}

void on_collection(void*, void*) {
    state.library_class_of.clear();
    state.admitted_of.clear();
    state.default_argument_of.clear();
    state.walked.clear();
    core.contract_walked.clear();
}

// ── The arguments and the admitted list ─────────────────────────────────

std::string trimmed(const std::string& text) {
    std::size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    std::size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// One entry on each line: a qualified name or a header in angle brackets,
// then '|' and the reason.  A line that starts with '#' is a comment.
bool load_admitted(const std::string& path) {
    FILE* stream = fopen(path.c_str(), "r");
    if (stream == nullptr) {
        error("quarantine: cannot read the admitted list %s: %m", path.c_str());
        return false;
    }
    std::string text;
    std::vector<char> chunk(4096);
    for (std::size_t count = 0; (count = fread(chunk.data(), 1, chunk.size(), stream)) > 0;) {
        text.append(chunk.data(), count);
    }
    fclose(stream);
    bool is_valid = true;
    int line_number = 0;
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
        std::string entry = trimmed(content.substr(0, bar));
        std::string reason = bar == std::string::npos ? std::string{} : trimmed(content.substr(bar + 1));
        if (entry.empty() || reason.empty()) {
            error("quarantine: %s:%d: an entry is %<NAME | REASON%>, and the reason is necessary", path.c_str(),
                  line_number);
            is_valid = false;
            continue;
        }
        if (entry.front() == '<' && entry.back() == '>' && entry.size() > 2) {
            state.admitted_headers.push_back("/" + entry.substr(1, entry.size() - 2));
        } else {
            state.admitted_names.insert(entry);
        }
    }
    return is_valid;
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
    std::string admitted_argument;
    for (int index = 0; index < plugin_info->argc; ++index) {
        std::string key = plugin_info->argv[index].key;
        std::string value = plugin_info->argv[index].value != nullptr ? plugin_info->argv[index].value : "";
        if (key == "root") {
            root_argument = value;
        } else if (key == "build") {
            if (!set_build(value)) {
                return 1;
            }
        } else if (key == "admitted") {
            admitted_argument = value;
        } else if (key == "out") {
            state.out_dir = value;
        } else if (key == "mode") {
            if (value != "report" && value != "error") {
                error("quarantine: mode is %qs; the modes are report and error", value.c_str());
                return 1;
            }
            state.is_error_mode = value == "error";
        } else if (key != "stamp") {
            error("quarantine: unknown argument %qs; the arguments are root, build, admitted, mode, out and stamp",
                  key.c_str());
            return 1;
        }
    }
    if (!set_root(root_argument)) {
        return 1;
    }
    if (!admitted_argument.empty() && !load_admitted(admitted_argument)) {
        return 1;
    }
    register_contract_rule(plugin_info->base_name);
    register_callback(plugin_info->base_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT, on_finish_unit, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_FINISH, on_finish, nullptr);
    register_callback(plugin_info->base_name, PLUGIN_GGC_START, on_collection, nullptr);
    return 0;
}
