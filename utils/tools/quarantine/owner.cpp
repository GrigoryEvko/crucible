// The family owner: each named reference of the code.  A function, a variable,
// a template, an overload set, a qualified name, a type and a using-declaration
// that the source names get their finding here, at the place of the expression
// or the declaration that names them.

#include "plugin.h"

namespace crucible_plugin {

namespace {

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

// An identifier that the source spells, and the first character after it that
// is not a space or a tab, or '\0' at the end of the line.
struct SpelledName {
    std::string identifier;
    char next = '\0';
};

// The identifier that the source spells at the start of the range of
// LOCATION, after each space, '(' and ':' there, or "" when none.  The
// spelling location decides, so a macro of a system header gives the
// identifier of its definition.  Complexity: O(length of the line).
SpelledName spelled_name_at(location_t location) {
    SpelledName spelled;
    location_t start = linemap_resolve_location(line_table, get_start(location), LRK_SPELLING_LOCATION, nullptr);
    expanded_location from = expand_location(start);
    if (from.file == nullptr || from.line <= 0 || from.column <= 0) {
        return spelled;
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(from.file, from.line);
    if (!line) {
        return spelled;
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
    spelled.identifier = text.substr(at, end - at);
    std::size_t after = end;
    while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
        ++after;
    }
    spelled.next = after < text.size() ? text[after] : '\0';
    return spelled;
}

// A use of DECL, a function or a variable that no library declares.  A
// foreign symbol of the tree is a use of the C library: the code reaches a
// symbol that no file of the root defines.  A symbol of C language linkage
// counts only when the unit does not define it, so the report judges the use.
// Returns true when it made a finding or keeps a use for the report.
bool check_foreign_use(tree decl, location_t location) {
    std::string binding = foreign_binding_of(decl);
    if (!binding.empty()) {
        record(Kind::c_library_call, location, qualified_name(decl) + " (" + binding + ")");
        return true;
    }
    if (!has_c_linkage(decl) || is_inside_system_assert(location)) {
        return false;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class != FileClass::quarantined) {
        return false;
    }
    state.foreign_uses.push_back(ForeignUse{DECL_UID(decl), place, qualified_name(decl) + " (C linkage)"});
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
            record_library(Kind::c_library_call, location, qualified_name(function));
            return true;
        case Library::none:
            return check_foreign_use(function, location);
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
            record_library(Kind::c_library_call, location, qualified_name(variable));
            break;
        case Library::none:
            check_foreign_use(variable, location);
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

}  // namespace

std::string identifier_spelled_at(location_t location) { return spelled_name_at(location).identifier; }

// True when the source spells the class CLASS_TYPE, or an alias of it in its
// namespace, at the start of the range of LOCATION, and a '(', a '{' or a '<'
// follows the name.  An explicit temporary, such as std::vector<int>{1, 2} or
// std::string("abc"), starts at the name of its type.  An implicit conversion
// starts at the expression that it converts.
bool spells_class_at(location_t location, tree class_type) {
    SpelledName spelled = spelled_name_at(location);
    if (spelled.identifier.empty() || (spelled.next != '(' && spelled.next != '{' && spelled.next != '<')) {
        return false;
    }
    tree decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(class_type));
    if (decl == NULL_TREE || spelled.identifier == simple_name(decl)) {
        return decl != NULL_TREE;
    }
    tree found =
        lookup_qualified_name(top_namespace(decl), get_identifier(spelled.identifier.c_str()), LOOK_want::TYPE, false);
    return found != NULL_TREE && found != error_mark_node && TREE_CODE(found) == TYPE_DECL
        && TYPE_MAIN_VARIANT(TREE_TYPE(found)) == TYPE_MAIN_VARIANT(class_type);
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

// ── The foreign symbols ─────────────────────────────────────────────────

// The binding of DECL to a foreign symbol that the declaration spells: an asm
// label or the attribute weakref, or "" when it spells none.  The front end
// gives a user assembler name a leading '*'.  A variable in a register is no
// symbol, and the source spells no artificial declaration.
std::string foreign_binding_of(tree decl) {
    if (DECL_ARTIFICIAL(decl) || (VAR_P(decl) && DECL_HARD_REGISTER(decl))) {
        return {};
    }
    if (DECL_ASSEMBLER_NAME_SET_P(decl)) {
        tree name = DECL_ASSEMBLER_NAME_RAW(decl);
        if (name != NULL_TREE && IDENTIFIER_POINTER(name)[0] == '*') {
            return std::string("asm label ") + (IDENTIFIER_POINTER(name) + 1);
        }
    }
    if (lookup_attribute("weakref", DECL_ATTRIBUTES(decl)) != NULL_TREE) {
        return "attribute weakref";
    }
    return {};
}

// True when DECL is a function or a variable of a namespace with C language
// linkage that the source declares.  The front end declares some library
// functions itself, such as __cxa_guard_acquire for a static local, and marks
// each one artificial.
bool has_c_linkage(tree decl) {
    return (TREE_CODE(decl) == FUNCTION_DECL || VAR_P(decl)) && !DECL_ARTIFICIAL(decl)
        && DECL_LANG_SPECIFIC(decl) != nullptr && DECL_NAMESPACE_SCOPE_P(decl) && DECL_EXTERN_C_P(decl);
}

// ── The names ───────────────────────────────────────────────────────────

// The library name that a member of the namespace NS gives, or NS itself
// when MEMBER is empty, as an admit row spells it: a qualified name that a
// template does not resolve, or the namespace of a using-directive.
void record_namespace_name(tree ns, const std::string& member, location_t location) {
    if (ns != std_node && top_namespace(ns) != std_node) {
        return;
    }
    std::string name = member.empty() ? qualified_name(ns) : qualified_name(ns) + "::" + member;
    const AdmitRow* row = admitting_row_of_name(name);
    if (!is_plainly_admitted(row)) {
        if (const FileEntry* language = language_file_of(location)) {
            row = admitting_row_of_name(name, language);
        }
    }
    if (row == nullptr) {
        record_library(Kind::std_entity, location, name);
    } else if (!row->pending_kind.empty()) {
        record_pending(*row, location, name);
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
        record_namespace_name(scope, member, location);
        return;
    }
    if (TYPE_P(scope)) {
        const FileEntry* language = nullptr;
        LibraryClasses classes = library_classes_at(scope, location, Use::other, language);
        if (is_standard_class(classes.refused)) {
            record_library(Kind::std_entity, location, qualified_name(classes.refused) + "::" + member);
        }
        record_pending_class(classes, location, "::" + member);
    }
}

// A type that a template names: the type of a cast, an explicit template
// argument, a compound literal.
void check_type_use(tree type, location_t location) { record_named_entities(type, location); }

// A using-declaration in a function body or in a class: the library entity
// that it brings into scope.  The front end keeps a using-declaration of
// namespace scope as a binding of the namespace, with no tree that has a
// location (a control of the plugin witness pins it).
void check_using(tree decl) {
    location_t location = DECL_SOURCE_LOCATION(decl);
    tree scope = USING_DECL_SCOPE(decl);
    if (scope == NULL_TREE) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    const FileEntry* language = language_of(place);
    if (language == nullptr && place.file_class != FileClass::quarantined) {
        return;
    }
    std::string member = DECL_NAME(decl) != NULL_TREE ? member_name_text(DECL_NAME(decl)) : std::string{};
    if (TYPE_P(scope)) {
        LibraryClasses classes = library_class_in(scope, 0, Use::other, language);
        if (is_standard_class(classes.refused)) {
            record_library(Kind::std_entity, location, qualified_name(classes.refused) + "::" + member);
        }
        record_pending_class(classes, location, "::" + member);
        return;
    }
    if (TREE_CODE(scope) != NAMESPACE_DECL) {
        return;
    }
    // A function or a variable of the C library keeps its kind.  Each other
    // target is the qualified name that the source spells.  The declaration
    // brings each overload of the set into scope, so a row admits it only
    // when a row admits each library declaration of the set, also through its
    // header.
    tree targets = USING_DECL_DECLS(decl);
    if (targets == NULL_TREE) {
        record_namespace_name(scope, member, location);
        return;
    }
    tree first = TREE_CODE(targets) == OVERLOAD ? OVL_FIRST(targets) : targets;
    if ((TREE_CODE(first) == FUNCTION_DECL || VAR_P(first)) && library_of(first) == Library::c_library) {
        record_library(Kind::c_library_call, location, qualified_name(first));
        return;
    }
    const AdmitRow* pending = nullptr;
    bool is_refused = false;
    for (lkp_iterator candidate(targets); candidate; ++candidate) {
        // A call names the function of a function template, which has the
        // location of its definition, so the set is read the same way.
        tree entity = *candidate;
        if (TREE_CODE(entity) == TEMPLATE_DECL && DECL_TEMPLATE_RESULT(entity) != NULL_TREE
            && TREE_CODE(DECL_TEMPLATE_RESULT(entity)) == FUNCTION_DECL) {
            entity = DECL_TEMPLATE_RESULT(entity);
        }
        if (library_of(entity) == Library::none) {
            continue;
        }
        const AdmitRow* row = admitting_row(entity, Use::other, NULL_TREE, language);
        is_refused = is_refused || row == nullptr;
        if (row != nullptr && !row->pending_kind.empty() && pending == nullptr) {
            pending = row;
        }
    }
    std::string spelled = qualified_name(scope) + "::" + member;
    if (is_refused) {
        record_library(Kind::std_entity, location, spelled);
    } else if (pending != nullptr) {
        record_pending(*pending, location, spelled);
    }
}

// One declaration that the code names, at the location of the expression that
// holds it.  ARGUMENTS is the argument count when NAMED is the callee of a
// call, or -1.  EXPLICIT_ARGUMENTS are the explicit template arguments when a
// template-id names NAMED, or null.
void check_named(tree named, location_t location, int arguments, tree explicit_arguments) {
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

}  // namespace crucible_plugin
