// The family owner: each named reference of the code.  A function, a variable,
// a template, an overload set, a qualified name, a type and a using-declaration
// that the source names get their finding here, at the place of the expression
// or the declaration that names them.
//
// THE OWNER ROWS
//     The rule table gives the sites of an entity in a row:
//         owner RULE KIND ENTITY [PATH...] [by=FUNCTION] | REASON
//     A use of ENTITY, or of a declaration inside it, stands only at a site of
//     the row: in a file under a PATH, and in the body of FUNCTION when the row
//     names one.  A row with neither admits no site.  Each other use is a
//     finding of the kind KIND of the rule RULE.  ENTITY is a qualified name,
//     and a function of the global namespace has its own name.  A use is a
//     declaration that an expression names: the callee of a call, an address,
//     a candidate of a dependent call and an explicit template argument.  In a
//     template pattern, the unresolved name of a call is a use of each entity
//     with that last name, because argument-dependent lookup can resolve it in
//     an instantiation.  A call of syscall() whose number is the value of the
//     macro SYS_NAME is a use of the function NAME.  A lambda is a part of the
//     function that holds it.
//
// THE CLAIMS (the rule syscall-capability)
//     The rule table names the system calls and the primitive of the base:
//         syscall NAME... | REASON
//         primitive FUNCTION | REASON
//     A call of a function NAME of the C library, a call of syscall(), and a
//     call of a primitive FUNCTION is the kind raw-syscall in a file under
//     include/, src/ or vessel/, outside each directory named test, bench,
//     examples, third_party, external or vendor, and each directory whose
//     name starts with build.  These are the claim trees.  The ledger of the
//     rule admits each claim with its reason.  The first argument of a call
//     of a primitive is an enumerator, and its name is the name of the system
//     call.
//
// THE DENIED CALLS (the rule syscall-capability)
//     The rule table names the system calls that the base does not issue raw:
//         deny NAME... | REASON
//     A call of syscall() with the number of the macro SYS_NAME, and a call of
//     a primitive with the enumerator NAME, is the kind denied-syscall in each
//     base and each quarantined file that a build compiles.  No ledger row
//     admits it.
//
// THE PLACE AND THE ENTITY
//     The place of a use is the place of its expression (place_of), in a base
//     or a quarantined file.  The entity of a finding is HOLDER: SHAPE.  A use
//     that the replacement list of a macro of a base or a quarantined file
//     spells has the holder "macro NAME", so the key of one macro body is the
//     same in each file that expands it.  A use in the body of a function has
//     the function_signature of that function, with the name of the class for
//     a constructor and a destructor, and a lambda is a part of the function
//     that holds it.  Each other use has the holder namespace-scope: an
//     initializer of a variable, a default member initializer and a default
//     argument.  The compiler copies the last two into each constructor and
//     each call, and it gives a copied call the location of the call or the
//     constructor.  The callee of the copy keeps the location of the
//     declaration, outside the range of the call (is_copied_call), so a copy
//     keeps the place and the holder of its declaration.  The shape is
//     the entity of the owner row,
//     the name of the system call, syscall(SYS_NAME) for a call of
//     syscall(), or FUNCTION(NAME) for a call of a primitive whose last name
//     is FUNCTION with the enumerator NAME.
//
// THE WALKS
//     The walk of a body of expression.cpp gives each node of a walked file
//     to check_owned_node, and each node of the function of the dynamic
//     initializers of the unit.  A walked file is a quarantined file, a file
//     of the language, and a base file in its sentinel unit (is_walked_file of
//     plugin.h).  The walks read each body, each pattern, each default
//     argument, each initializer of a variable and each default member
//     initializer of such a file, also a specialization of a template of a
//     different file, such as a specialization of std::hash.
//
// WHAT THE FAMILY DOES NOT READ
//     Code that no unit of a counted build compiles: a macro body that no unit
//     expands, a preprocessor arm that no build selects, and vessel/torch with
//     no TORCH_DIR (decision 5 of section 18.23 of
//     misc/01_10_2026_quarantine.md).  A name that the front end folds while
//     it parses, such as an operand of sizeof or a condition of a
//     static_assert outside a template, holds no call that runs.  The rule
//     quarantine reads such a name from the event of the parser (THE FOLDED
//     NAMES).

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

// False when the source spells a different text than IDENTIFIER at the start
// of the range of LOCATION, or at its caret when IS_CARET.  The parser gives
// the first name of a qualified name, and the :: of a global name, an event of
// the declaration too.  The instantiation of a template gives a member access
// the range of the whole access, with its caret at the member.  The spelling
// location decides, so a macro of the tree gives the text of its definition.
// A line that the file cache cannot read keeps the event.  Complexity:
// O(length of the identifier).
bool spells_identifier_at(location_t location, tree identifier, bool is_caret = false) {
    location_t point = is_caret ? get_pure_location(line_table, location) : get_start(location);
    location_t start = linemap_resolve_location(line_table, point, LRK_SPELLING_LOCATION, nullptr);
    expanded_location from = expand_location(start);
    if (from.file == nullptr || from.line <= 0 || from.column <= 0) {
        return true;
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(from.file, from.line);
    if (!line) {
        return true;
    }
    auto at = static_cast<std::size_t>(from.column - 1);
    auto length = static_cast<std::size_t>(IDENTIFIER_LENGTH(identifier));
    if (at + length > line.length()
        || std::memcmp(line.get_buffer() + at, IDENTIFIER_POINTER(identifier), length) != 0) {
        return false;
    }
    return at + length == line.length() || !is_identifier_byte(line[at + length]);
}

// The first character before the start of the range of LOCATION that is not a
// space or a tab, on the same line, or '\0' when none.  ':' tells a qualified
// name.  Complexity: O(length of the line).
char spelled_before_at(location_t location) {
    location_t start = linemap_resolve_location(line_table, get_start(location), LRK_SPELLING_LOCATION, nullptr);
    expanded_location from = expand_location(start);
    if (from.file == nullptr || from.line <= 0 || from.column <= 1) {
        return '\0';
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(from.file, from.line);
    if (!line) {
        return '\0';
    }
    std::size_t at = std::min(static_cast<std::size_t>(from.column - 1), line.length());
    while (at > 0 && (line[at - 1] == ' ' || line[at - 1] == '\t')) {
        --at;
    }
    return at > 0 ? line[at - 1] : '\0';
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
    std::string entity = qualified_name(decl) + " (C linkage)";
    if (Place expansion = expansion_place_of(location, place); expansion.file != nullptr) {
        state.foreign_uses.push_back(ForeignUse{DECL_UID(decl), expansion, entity});
    }
    state.foreign_uses.push_back(ForeignUse{DECL_UID(decl), place, std::move(entity)});
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

// True when the source spells the class CLASS_TYPE at the start of the range
// of LOCATION, and a '(', a '{' or a '<' follows the name.  An explicit
// temporary, such as std::vector<int>{1, 2} or std::string("abc"), starts at
// the name of its type.  An implicit conversion starts at the expression that
// it converts.  The source can spell the class by its own name, or by the
// name of the type as the code writes it: an alias such as
// std::chrono::seconds, or an alias template such as std::chrono::sys_time.
// The front end keeps that name in TYPE_NAME of the type of the temporary.  An
// alias of the class in its top namespace also counts, when the type of the
// temporary holds no alias name.
bool spells_class_at(location_t location, tree class_type) {
    SpelledName spelled = spelled_name_at(location);
    if (spelled.identifier.empty() || (spelled.next != '(' && spelled.next != '{' && spelled.next != '<')) {
        return false;
    }
    tree decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(class_type));
    if (decl == NULL_TREE || spelled.identifier == simple_name(decl)) {
        return decl != NULL_TREE;
    }
    tree written = TYPE_NAME(class_type);
    if (written != NULL_TREE && TREE_CODE(written) == TYPE_DECL && spelled.identifier == simple_name(written)) {
        return true;
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
    note_project_reference(scope, location);
    tree name = TREE_OPERAND(scope_ref, 1);
    if (name != NULL_TREE && TREE_CODE(name) == TEMPLATE_ID_EXPR) {
        name = TREE_OPERAND(name, 0);
    }
    if (name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE) {
        note_walked_identifier(name, location);
    } else {
        note_walked_decl(name, location);
    }
    if (TREE_CODE(scope) == NAMESPACE_DECL) {
        record_namespace_name(scope, member, location);
        return;
    }
    if (TYPE_P(scope)) {
        note_walked_type(scope, location, false);
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
// location, and the event of the parser gives it (judge_namespace_using).  The
// front end also adds an artificial using-declaration of operator= to the
// pattern of a class template, such as a partial specialization of std::hash,
// and the source does not spell it.  A using-enum-declaration gives one
// using-declaration of each enumerator, at the location after its semicolon.
// The source spells no enumerator there, and the parsed name of the
// enumeration is its use (THE FOLDED NAMES).
void check_using(tree decl) {
    location_t location = DECL_SOURCE_LOCATION(decl);
    tree scope = USING_DECL_SCOPE(decl);
    if (scope == NULL_TREE || DECL_ARTIFICIAL(decl)) {
        return;
    }
    if (TREE_CODE(scope) == ENUMERAL_TYPE && DECL_NAME(decl) != NULL_TREE
        && TREE_CODE(DECL_NAME(decl)) == IDENTIFIER_NODE && !spells_identifier_at(location, DECL_NAME(decl))) {
        return;
    }
    note_project_reference(scope, location);
    Place place = place_of(location, Scope::quarantine);
    const FileEntry* language = language_of(place);
    if (language == nullptr && place.file_class != FileClass::quarantined) {
        return;
    }
    std::string member = DECL_NAME(decl) != NULL_TREE ? member_name_text(DECL_NAME(decl)) : std::string{};
    note_walked_identifier(DECL_NAME(decl), location);
    if (TYPE_P(scope)) {
        note_walked_type(scope, location, false);
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
    note_walked_decl(named, location);
    switch (TREE_CODE(named)) {
        case FUNCTION_DECL:
        case VAR_DECL:
        case TEMPLATE_DECL:
        case OVERLOAD:
        case BASELINK:
            note_project_reference(named, location);
            break;
        default:
            break;
    }
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

// ── The owner rules (THE OWNER ROWS, THE CLAIMS) ────────────────────────

namespace {

// The claim trees, and the names of the directories that keep a file out of
// them (THE CLAIMS).
constexpr const char* kClaimTrees[] = {"include/", "src/", "vessel/"};
constexpr const char* kClaimExcluded[] = {"test", "bench", "examples", "third_party", "external", "vendor"};
constexpr char kClaimExcludedPrefix[] = "build";
// The holder of a use in no function, and the prefix of the holder of a use
// that a macro body spells.
constexpr char kNoHolder[] = "namespace-scope";
constexpr char kMacroHolder[] = "macro ";
// The prefix of the macro that gives the number of a system call.
constexpr char kSyscallMacroPrefix[] = "SYS_";
constexpr std::size_t kSyscallMacroPrefixLength = sizeof kSyscallMacroPrefix - 1;

// The names that the owner rules compare, as identifiers, made one time.  An
// identifier is never collected, so the sets stay valid in the unit.
struct OwnerIndex {
    bool is_ready = false;
    std::unordered_set<tree> last_names;  // the last name of each entity of an owner row
    std::unordered_set<tree> syscall_names;  // each name of a syscall row
    tree raw_syscall = NULL_TREE;  // the identifier syscall
    std::unordered_set<tree> primitive_last_names;  // the last name of the function of each primitive row
    std::unordered_set<std::string> denied_names;  // each name of a deny row
    // The name NAME of each macro SYS_NAME, by its number.  The unit reads the
    // macros one time, at the first call of syscall().
    bool are_numbers_read = false;
    std::unordered_map<long long, std::string> numbers;
};

OwnerIndex owner_index;

bool has_owner_rows() {
    return !state.table.owners.empty() || !state.table.syscalls.empty() || !state.table.primitives.empty()
        || !state.table.denied_syscalls.empty();
}

void build_owner_index() {
    if (owner_index.is_ready) {
        return;
    }
    owner_index.is_ready = true;
    for (const OwnerRow& row : state.table.owners) {
        if (reads_spelled_names(row.rule)) {
            continue;
        }
        std::size_t last = row.entity.rfind("::");
        std::string name = last == std::string::npos ? row.entity : row.entity.substr(last + 2);
        owner_index.last_names.insert(get_identifier(name.c_str()));
    }
    for (const std::string& name : state.table.syscalls) {
        owner_index.syscall_names.insert(get_identifier(name.c_str()));
    }
    owner_index.raw_syscall = get_identifier("syscall");
    for (const std::string& primitive : state.table.primitives) {
        std::size_t last = primitive.rfind("::");
        std::string name = last == std::string::npos ? primitive : primitive.substr(last + 2);
        owner_index.primitive_last_names.insert(get_identifier(name.c_str()));
    }
    owner_index.denied_names.insert(state.table.denied_syscalls.begin(), state.table.denied_syscalls.end());
}

// True when the function FUNCTION is the function of a primitive row.  A
// function whose last name is no last name of a primitive costs one test of
// a set.
bool is_primitive_function(tree function) {
    if (DECL_NAME(function) == NULL_TREE || owner_index.primitive_last_names.count(DECL_NAME(function)) == 0) {
        return false;
    }
    std::string name = qualified_name(function);
    for (const std::string& primitive : state.table.primitives) {
        if (primitive == name) {
            return true;
        }
    }
    return false;
}

// True when the claim trees hold the file RELATIVE (THE CLAIMS).
// Complexity: O(length of RELATIVE).
bool is_claim_file(const std::string& relative) {
    bool is_in_tree = false;
    for (const char* claim_tree : kClaimTrees) {
        is_in_tree = is_in_tree || has_prefix(relative, claim_tree);
    }
    if (!is_in_tree) {
        return false;
    }
    std::size_t start = 0;
    for (std::size_t slash = relative.find('/'); slash != std::string::npos; slash = relative.find('/', start)) {
        std::string part = relative.substr(start, slash - start);
        if (has_prefix(part, kClaimExcludedPrefix)) {
            return false;
        }
        for (const char* excluded : kClaimExcluded) {
            if (part == excluded) {
                return false;
            }
        }
        start = slash + 1;
    }
    return true;
}

// The function that holds a use in the body of FUNCTION: FUNCTION itself, or
// for a lambda the function that holds the lambda.  A clone of a constructor
// or a destructor, such as the complete and the base destructor, stands for
// the function that the source declares.  Null for an initializer and for a
// function that the compiler makes, such as the function of the dynamic
// initializers of a unit.
tree holder_of(tree function) {
    for (int depth = 0; function != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(function) != FUNCTION_DECL) {
            return NULL_TREE;
        }
        if (DECL_CLONED_FUNCTION_P(function)) {
            function = DECL_CLONED_FUNCTION(function);
            continue;
        }
        if (!LAMBDA_FUNCTION_P(function)) {
            return DECL_ARTIFICIAL(function) ? NULL_TREE : function;
        }
        tree closure = DECL_CONTEXT(function);
        function = closure != NULL_TREE && TYPE_P(closure) ? decl_function_context(TYPE_MAIN_DECL(closure)) : NULL_TREE;
    }
    return NULL_TREE;
}

// The qualified name of the function HOLDER as the source spells it.  The
// front end names a constructor and a destructor __ct and __dt, so their name
// is the name of the class, with '~' for a destructor.
std::string holder_name(tree holder) {
    tree type = DECL_CONTEXT(holder);
    bool is_special = DECL_CONSTRUCTOR_P(holder) || DECL_DESTRUCTOR_P(holder);
    if (!is_special || type == NULL_TREE || !TYPE_P(type) || TYPE_MAIN_DECL(type) == NULL_TREE) {
        return qualified_name(holder);
    }
    tree class_decl = TYPE_MAIN_DECL(type);
    return qualified_name(class_decl) + "::" + (DECL_DESTRUCTOR_P(holder) ? "~" : "") + simple_name(class_decl);
}

// The holder text of the function HOLDER: its function_signature, with the
// name of holder_name.
std::string holder_signature(tree holder) {
    std::string signature = function_signature(holder);
    std::string front_end_name = qualified_name(holder);
    if (signature.compare(0, front_end_name.size(), front_end_name) != 0) {
        return signature;
    }
    return holder_name(holder) + signature.substr(front_end_name.size());
}

// True when SPELLED, a spelling location, lies from FIRST to LAST, two
// locations of one file.  Complexity: O(1).
bool is_between(const expanded_location& spelled, location_t first, location_t last) {
    expanded_location start = expand_location(first);
    expanded_location end = expand_location(last);
    if (spelled.file == nullptr || start.file == nullptr || end.file == nullptr
        || std::strcmp(spelled.file, start.file) != 0 || std::strcmp(spelled.file, end.file) != 0) {
        return false;
    }
    bool is_after_start = spelled.line > start.line || (spelled.line == start.line && spelled.column >= start.column);
    bool is_before_end = spelled.line < end.line || (spelled.line == end.line && spelled.column <= end.column);
    return is_after_start && is_before_end;
}

// The name of the innermost macro of a base or a quarantined file whose
// replacement list spells the token at LOCATION, or "".  A token of a macro
// argument is spelled where the code invokes the macro, so no macro holds it.
// Complexity: O(depth of the expansion).
std::string spelling_macro_of(location_t location) {
    expanded_location spelled =
        expand_location(linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, nullptr));
    location_t current = location;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, current); ++depth) {
        const line_map* map = linemap_lookup(line_table, current);
        cpp_hashnode* node = MACRO_MAP_MACRO(linemap_check_macro(map));
        const cpp_macro* macro = node != nullptr && cpp_user_macro_p(node) ? node->value.macro : nullptr;
        if (macro != nullptr && macro->kind == cmk_macro && macro->count > 0) {
            location_t definition = cpp_macro_definition_location(node);
            expanded_location defined = expand_location(definition);
            if (defined.file != nullptr && owns_place(classify_file(defined.file).file_class, Scope::quarantine)
                && is_between(spelled, definition, macro->exp.tokens[macro->count - 1].src_loc)) {
                return reinterpret_cast<const char*>(NODE_NAME(node));
            }
        }
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return {};
}

// True when the call CALL with the callee CALLEE is a copy of a default
// argument or of a default member initializer.  The compiler gives such a copy
// the location of the call or of the constructor that copies it, and the
// callee keeps the location that the declaration spells, outside the range of
// the copy.  A location of a macro expansion gives no answer, and a call of
// it is no copy.  Complexity: O(1).
bool is_copied_call(tree call, tree callee) {
    if (!EXPR_HAS_LOCATION(call) || callee == NULL_TREE || !EXPR_P(callee) || !EXPR_HAS_LOCATION(callee)) {
        return false;
    }
    location_t call_location = EXPR_LOCATION(call);
    location_t callee_location = EXPR_LOCATION(callee);
    if (linemap_location_from_macro_expansion_p(line_table, call_location)
        || linemap_location_from_macro_expansion_p(line_table, callee_location)) {
        return false;
    }
    return !is_between(expand_location(callee_location), get_start(call_location), get_finish(call_location));
}

// The holder of a use at LOCATION in the body of FUNCTION, or in an
// initializer when FUNCTION is null (THE PLACE AND THE ENTITY).
std::string holder_text(tree function, location_t location) {
    std::string macro = spelling_macro_of(location);
    if (!macro.empty()) {
        return kMacroHolder + macro;
    }
    tree holder = holder_of(function);
    return holder != NULL_TREE ? holder_signature(holder) : std::string(kNoHolder);
}

// The place of a use at LOCATION when an owner rule reads it: a place in a base
// or a quarantined file, or an empty place.
Place owned_place(location_t location) {
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr || (place.file_class != FileClass::base && place.file_class != FileClass::quarantined)) {
        return Place{};
    }
    return place;
}

// True when a use at PLACE in the body of FUNCTION stands at a site of ROW
// (THE OWNER ROWS).
bool is_at_site(const OwnerRow& row, const Place& place, tree function) {
    if (row.paths.empty() && row.function.empty()) {
        return false;
    }
    if (!row.paths.empty()) {
        bool is_held = false;
        for (const std::string& path : row.paths) {
            is_held = is_held || row_holds(path, place.file->relative);
        }
        if (!is_held) {
            return false;
        }
    }
    if (!row.function.empty()) {
        tree holder = holder_of(function);
        if (holder == NULL_TREE || holder_name(holder) != row.function) {
            return false;
        }
    }
    return true;
}

// A finding for each owner row of the entity ENTITY that does not admit a use
// at LOCATION in the body of FUNCTION.  SHAPE is the shape of the entity.
void check_owner_rows(const std::string& entity, const std::string& shape, location_t location, tree function) {
    Place place;
    for (const OwnerRow& row : state.table.owners) {
        if (row.entity != entity || reads_spelled_names(row.rule)) {
            continue;
        }
        if (place.file == nullptr) {
            place = owned_place(location);
            if (place.file == nullptr) {
                return;
            }
        }
        if (!is_at_site(row, place, function)) {
            add_finding(row.kind, nullptr, place, holder_text(function, location) + ": " + shape);
        }
    }
}

// The use of the declaration DECL at LOCATION, in the body of FUNCTION: each
// owner row whose entity is DECL or holds it.  A scope whose name is no last
// name of an entity costs one test of a set.  Complexity: O(depth of the
// scopes).
void check_owned_decl(tree decl, location_t location, tree function) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        tree named = TYPE_P(current) ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(current)) : current;
        if (named != NULL_TREE && DECL_P(named) && DECL_NAME(named) != NULL_TREE
            && owner_index.last_names.count(DECL_NAME(named)) != 0) {
            std::string entity = qualified_name(named);
            check_owner_rows(entity, entity, location, function);
        }
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                  : NULL_TREE;
    }
}

// Each declaration that the operand OPERAND names: a function, a variable, the
// function of a template, and each candidate of an overload set.
void check_owned_operand(tree operand, location_t location, tree function, int depth = 0) {
    if (operand == NULL_TREE || depth > 8) {
        return;
    }
    switch (TREE_CODE(operand)) {
        case FUNCTION_DECL:
        case VAR_DECL:
            check_owned_decl(operand, location, function);
            return;
        case TEMPLATE_DECL:
            check_owned_decl(DECL_TEMPLATE_RESULT(operand) != NULL_TREE ? DECL_TEMPLATE_RESULT(operand) : operand,
                             location, function);
            return;
        case BASELINK:
            check_owned_operand(BASELINK_FUNCTIONS(operand), location, function, depth + 1);
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(operand, true); candidate; ++candidate) {
                check_owned_operand(*candidate, location, function, depth + 1);
            }
            return;
        default:
            return;
    }
}

// The unresolved name of the callee CALLEE of a call in a pattern, or null: a
// call that only argument-dependent lookup resolves keeps an identifier.
tree unresolved_callee_name(tree callee) {
    if (callee != NULL_TREE && TREE_CODE(callee) == TEMPLATE_ID_EXPR) {
        callee = TREE_OPERAND(callee, 0);
    }
    return callee != NULL_TREE && TREE_CODE(callee) == IDENTIFIER_NODE ? callee : NULL_TREE;
}

// Collects the name of each macro SYS_NAME of the unit (cpp_forall_identifiers).
int collect_syscall_macro(cpp_reader*, cpp_hashnode* node, void* data) {
    if (node != nullptr && cpp_user_macro_p(node)
        && std::strncmp(reinterpret_cast<const char*>(NODE_NAME(node)), kSyscallMacroPrefix, kSyscallMacroPrefixLength)
               == 0) {
        static_cast<std::vector<std::string>*>(data)->emplace_back(reinterpret_cast<const char*>(NODE_NAME(node)));
    }
    return 1;
}

// The name NAME of the macro SYS_NAME whose value is NUMBER, or "".  Two macros
// with one value give the first name in the order of the bytes.  The unit reads
// its macros one time.  Complexity: O(identifiers of the unit) at the first
// call, and O(1) after it.
std::string syscall_name_of(long long number) {
    if (!owner_index.are_numbers_read) {
        owner_index.are_numbers_read = true;
        std::vector<std::string> macros;
        cpp_forall_identifiers(parse_in, collect_syscall_macro, &macros);
        std::sort(macros.begin(), macros.end());
        for (const std::string& macro : macros) {
            long long value = 0;
            if (macro_number(macro, 0, value)) {
                owner_index.numbers.emplace(value, macro.substr(kSyscallMacroPrefixLength));
            }
        }
    }
    auto found = owner_index.numbers.find(number);
    return found != owner_index.numbers.end() ? found->second : std::string{};
}

// The system call of a call of syscall(): its shape, and the name NAME of its
// macro SYS_NAME, or "" when the number is no constant or no macro has it.
std::string raw_syscall_shape(tree call, std::string& name) {
    long long number = 0;
    if (!syscall_number_of(call, number)) {
        return "syscall(?)";
    }
    name = syscall_name_of(number);
    return name.empty() ? "syscall(" + std::to_string(number) + ")"
                        : "syscall(" + std::string(kSyscallMacroPrefix) + name + ")";
}

// The name of the enumerator that the first argument of the call CALL names,
// through its location wrappers and conversions, or "" when the argument is
// no enumerator: a value that no enumerator has, or an argument that is not a
// constant.  Complexity: O(enumerators of the enumeration).
std::string first_enumerator_name(tree call) {
    if (argument_count(call) == 0) {
        return {};
    }
    tree argument = argument_at(call, 0);
    for (int depth = 0; argument != NULL_TREE && depth < kTypeDepth; ++depth) {
        argument = tree_strip_any_location_wrapper(argument);
        if (TREE_CODE(argument) != NOP_EXPR && TREE_CODE(argument) != CONVERT_EXPR
            && TREE_CODE(argument) != NON_LVALUE_EXPR) {
            break;
        }
        argument = TREE_OPERAND(argument, 0);
    }
    if (argument == NULL_TREE) {
        return {};
    }
    if (TREE_CODE(argument) == CONST_DECL && DECL_NAME(argument) != NULL_TREE) {
        return IDENTIFIER_POINTER(DECL_NAME(argument));
    }
    tree type = TREE_TYPE(argument);
    if (TREE_CODE(argument) != INTEGER_CST || type == NULL_TREE || TREE_CODE(type) != ENUMERAL_TYPE) {
        return {};
    }
    for (tree value = TYPE_VALUES(TYPE_MAIN_VARIANT(type)); value != NULL_TREE; value = TREE_CHAIN(value)) {
        tree enumerator = TREE_VALUE(value);
        tree initial = TREE_CODE(enumerator) == CONST_DECL ? DECL_INITIAL(enumerator) : enumerator;
        if (initial != NULL_TREE && TREE_CODE(initial) == INTEGER_CST && tree_int_cst_equal(initial, argument)) {
            return IDENTIFIER_POINTER(TREE_PURPOSE(value));
        }
    }
    return {};
}

// A finding of the kind denied-syscall for a raw call of the system call NAME
// at LOCATION in the body of FUNCTION, when a deny row names NAME (THE DENIED
// CALLS).
void check_denied_call(const std::string& name, const std::string& shape, location_t location, tree function) {
    if (name.empty() || owner_index.denied_names.count(name) == 0) {
        return;
    }
    Place place = owned_place(location);
    if (place.file != nullptr) {
        add_finding(Kind::denied_syscall, nullptr, place, holder_text(function, location) + ": " + shape);
    }
}

// One call: each system call of a syscall row that it makes (THE CLAIMS), the
// owner rows of the system call of syscall(), and the owner rows of each name
// of a call that a pattern does not resolve.
void check_owned_call(tree call, location_t location, tree function) {
    tree callee = callee_of(call);
    // A copy of a default argument or of a default member initializer has the
    // place and the holder of its declaration: the place of its callee, and
    // no function.
    if (is_copied_call(call, callee)) {
        location = EXPR_LOCATION(callee);
        function = NULL_TREE;
    }
    if (tree name = unresolved_callee_name(callee); name != NULL_TREE && owner_index.last_names.count(name) != 0) {
        for (const OwnerRow& row : state.table.owners) {
            std::size_t last = row.entity.rfind("::");
            if (!reads_spelled_names(row.rule)
                && (last == std::string::npos ? row.entity : row.entity.substr(last + 2)) == IDENTIFIER_POINTER(name)) {
                check_owner_rows(row.entity, row.entity, location, function);
            }
        }
    }
    bool is_raw = false;
    std::string primitive;
    std::string called;
    for_each_function(callee, [&](tree candidate) {
        tree candidate_name = DECL_NAME(candidate);
        if (candidate_name == NULL_TREE) {
            return;
        }
        if (primitive.empty() && is_primitive_function(candidate)) {
            primitive = IDENTIFIER_POINTER(candidate_name);
            return;
        }
        if (library_of(candidate) != Library::c_library) {
            return;
        }
        if (candidate_name == owner_index.raw_syscall) {
            is_raw = true;
        } else if (called.empty() && owner_index.syscall_names.count(candidate_name) != 0) {
            called = IDENTIFIER_POINTER(candidate_name);
        }
    });
    if (!is_raw && primitive.empty() && called.empty()) {
        return;
    }
    std::string shape = called;
    if (is_raw) {
        std::string name;
        shape = raw_syscall_shape(call, name);
        if (!name.empty()) {
            check_owner_rows(name, shape, location, function);
        }
        check_denied_call(name, shape, location, function);
    } else if (!primitive.empty()) {
        std::string name = first_enumerator_name(call);
        shape = primitive + "(" + (name.empty() ? std::string("?") : name) + ")";
        check_denied_call(name, shape, location, function);
    }
    Place place = owned_place(location);
    if (place.file != nullptr && is_claim_file(place.file->relative)) {
        add_finding(Kind::raw_syscall, nullptr, place, holder_text(function, location) + ": " + shape);
    }
}

// ── The names of the source (THE NAMES OF THE SOURCE) ───────────────────

// The name of a detail namespace, the prefix of the shape of a friend, the
// shapes of a member splice and of a walked return, and the files of the
// classes of the rule of the open static members.
constexpr char kDetailName[] = "detail";
constexpr char kFriendPrefix[] = "friend ";
constexpr char kSpliceShape[] = "member splice";
constexpr char kWalkedShape[] = "walked return";
constexpr const char* kMemberRoots[] = {"include/foundation/", "include/fixy/", "include/crucible/"};
// The functions of the namespace of the access class that walk the members or
// the bases of a class, the access contexts under which a walk gives only what
// its caller can name, and the members of a range that give one of its
// elements, or an iterator to one.
constexpr const char* kMemberWalks[] = {"members_of", "nonstatic_data_members_of", "static_data_members_of", "bases_of",
                                        "subobjects_of"};
constexpr const char* kCheckedContexts[] = {"current", "unprivileged"};
constexpr const char* kElementAccessors[] = {"operator[]", "operator*", "operator->", "front", "back",
                                             "at",         "begin",     "cbegin",     "data"};
// The rounds of the taint of the local variables of one function, and the
// depth of a folded constant that the walked return reads.
constexpr int kTraceDepth = 8;
// THE FIRST TEST
//     PLUGIN_NAME_USE gives an event for each name that the parser looks up,
//     and a large unit gives some hundred thousand events.  Almost no event
//     names a declaration that a rule can judge, so the handler reads the
//     verdict of the declaration first, by its DECL_UID, and the current
//     namespace.  Only the first event of a declaration, a judged use, an
//     overload set and a use in a namespace that is not quiet read more.
// The verdicts of THE FIRST TEST, and the length of their list.
constexpr unsigned char kVerdictFree = 1;
constexpr unsigned char kVerdictJudged = 2;
constexpr unsigned char kVerdictUnchecked = 3;
// The bits of FirstTest::marks: no row holds the declaration, and the body of
// the function uses the unchecked function.
constexpr unsigned char kMarkHeldByNone = 1;
constexpr unsigned char kMarkUncheckedUser = 2;
constexpr unsigned kVerdictSlots = 1U << 25;

// The head of a name row that holds a declaration: the row, and the class,
// the namespace or the function that is its head.
struct Held {
    const OwnerRow* row = nullptr;
    tree head = NULL_TREE;
};

// The facts of one namespace that is no alias: the row that the namespace
// itself heads, and the outermost head of a row that holds it.
struct NamespaceFacts {
    const OwnerRow* own = nullptr;
    Held held;
};

// The ordinary line map of the last location that ordinary_file_at read: its
// range of locations and the entry of its file.
struct FileRange {
    location_t first = 0;
    location_t end = 0;  // one past the last location of the range, or 0 for no range
    const FileEntry* entry = nullptr;
};

// The rows of the name rules, and the facts that the events read, made one
// time when the plugin loads.  An identifier is never collected, and a
// namespace is never collected, so these lists and the cache of the
// namespaces stay valid in the unit.
struct NameRules {
    bool has_rows = false;  // the table has a row of a rule that reads_spelled_names
    bool is_ready = false;  // build_name_rules made the lists
    std::vector<const OwnerRow*> rows;
    // The last name of the head of each row.  The table has a few such rows,
    // so a scan of the list costs less than a test of a hash set.  Bit N of
    // head_name_bits is set when the hash value of a head name modulo 64 is N,
    // so most names that are no head name leave after one test of a bit.
    // Each bit is set before make_name_rules makes the list.
    std::vector<tree> head_names;
    unsigned long long head_name_bits = ~0ULL;
    tree detail = NULL_TREE;
    const OwnerRow* unchecked_row = nullptr;  // the row of the rule no-unchecked-access, or null
    tree unchecked_name = NULL_TREE;
    std::string access_class;  // the class of the unchecked function, such as std::meta::access_context
    tree access_class_name = NULL_TREE;  // the last name of that class
    std::vector<std::string> access_scopes;  // each namespace that holds that class
    std::vector<tree> access_scope_names;  // the last name of each such namespace
    tree define_static_array = NULL_TREE;
    bool has_detail_rows = false;
    // The facts of each namespace that an event or a walk asked, and the last
    // namespace with its facts.  A pointer into an unordered_map stays valid
    // when the map grows.
    std::unordered_map<tree, NamespaceFacts> namespaces;
    tree last_namespace = NULL_TREE;
    const NamespaceFacts* last_facts = nullptr;
    // The line map of the last location that ordinary_file_at read.
    FileRange last_range;
    // The current namespace of the last event that check_detail_scope read,
    // and the outermost detail namespace of a row that holds it, or null.
    tree last_scope = NULL_TREE;
    tree last_scope_head = NULL_TREE;
    // Each function whose body uses the unchecked function (THE WALKED
    // RETURN) and whose DECL_UID has no slot in FirstTest::marks, and each
    // pattern of a counted file, which the end of the unit reads.  The
    // template of a pattern keeps the pattern, so a collection does not free
    // it.
    std::unordered_set<unsigned> unchecked_users_without_slot;
    bool has_unchecked_users = false;
    std::vector<tree> patterns;
    // The TYPE_UID of a class and the DECL_UID of a head of a door rule, for
    // each name of the head that an event gave while the parser read the
    // class.  A qualified friend is such a name (check_door_friends).
    std::unordered_set<unsigned long long> class_heads;
};

NameRules name_rules;

// The state of THE FIRST TEST: the verdict of each declaration that an event
// of PLUGIN_NAME_USE named, by its DECL_UID, and the current namespace when it
// is quiet.  A verdict is 0 before the first read, kVerdictFree when no rule
// can judge a use of the declaration, and kVerdictJudged when a rule can.  The
// facts of a verdict never change in the unit, and a DECL_UID is never used
// again, so a verdict stays true after a collection.  The list is zero at
// load, so the kernel gives a page of it only when an event writes the page.
// A declaration with a DECL_UID of kVerdictSlots or more gets its verdict at
// each event.  A namespace is quiet when no detail namespace of a row holds it
// (check_detail_scope), and the namespace is null when the current namespace
// is not quiet.  A use in a namespace that a detail namespace holds is quiet
// too when its file is outside the quarantine, so the state also keeps the
// last such namespace and the range of ordinary locations of its last file.
// The list is_decl_code gives DECL_P for each tree code with no read of the
// table of the compiler (register_owner).  The list member_verdicts keeps
// kVerdictFree for each member that a class member access names when no rule
// can judge the access (judge_member_access).  The list marks keeps the bits
// kMarkHeldByNone (held_by) and kMarkUncheckedUser (mark_unchecked) of each
// declaration.  The lists are static and never free memory, so they do not
// move the heap of the compiler.
//
// THE FIRST TEST reads the location of an event before its declaration.  A
// use at an ordinary location of a file outside the root never gives a
// finding (owned_place), and it marks no function of a counted file, so the
// event leaves at once.  Most events of a unit name a library declaration
// in a library header.  The state keeps the range of ordinary locations of
// the last such file, and the range of the last file inside the root, as a
// first location and a length (judge_location).
struct FirstTest {
    std::array<unsigned char, MAX_TREE_CODES> is_decl_code{};
    std::array<unsigned char, kVerdictSlots> verdicts{};
    std::array<unsigned char, kVerdictSlots> member_verdicts{};
    std::array<unsigned char, kVerdictSlots> marks{};
    location_t outside_first = 0;
    location_t outside_length = 0;
    location_t inside_first = 0;
    location_t inside_length = 0;
    // The file of the inside range is counted (THE FOLDED NAMES): a
    // quarantined file, or a file of language_of.
    bool is_inside_counted = false;
    tree quiet_namespace = NULL_TREE;
    tree detail_namespace = NULL_TREE;
    location_t quiet_first = 0;
    location_t quiet_end = 0;  // one past the last location of the range, or 0 for no range
};

FirstTest first_test;

bool has_name_rows() { return name_rules.has_rows; }

// The identifier of the last name of the qualified name NAME.
tree last_name_of(const std::string& name) {
    std::size_t last = name.rfind("::");
    return get_identifier(last == std::string::npos ? name.c_str() : name.c_str() + last + 2);
}

void push_unique(std::vector<tree>& names, tree name) {
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
    }
}

// The bit of the identifier NAME in NameRules::head_name_bits.
unsigned long long head_name_bit(tree name) { return 1ULL << (IDENTIFIER_HASH_VALUE(name) & 63U); }

// Makes the lists of NameRules at the first event, because the identifiers of
// the unit exist only after the plugin loads.
void make_name_rules() {
    name_rules.is_ready = true;
    name_rules.detail = get_identifier(kDetailName);
    for (const OwnerRow& row : state.table.owners) {
        if (!reads_spelled_names(row.rule)) {
            continue;
        }
        name_rules.rows.push_back(&row);
        if (row.rule == Rule::detail_namespace) {
            name_rules.has_detail_rows = true;
            push_unique(name_rules.head_names, name_rules.detail);
            continue;
        }
        tree name = last_name_of(row.entity);
        push_unique(name_rules.head_names, name);
        std::size_t last = row.entity.rfind("::");
        if (row.rule == Rule::no_unchecked_access && last != std::string::npos && name_rules.unchecked_row == nullptr) {
            name_rules.unchecked_row = &row;
            name_rules.unchecked_name = name;
            name_rules.access_class = row.entity.substr(0, last);
            name_rules.access_class_name = last_name_of(name_rules.access_class);
            for (std::size_t scope = name_rules.access_class.rfind("::"); scope != std::string::npos;
                 scope = scope == 0 ? std::string::npos : name_rules.access_class.rfind("::", scope - 1)) {
                name_rules.access_scopes.push_back(name_rules.access_class.substr(0, scope));
                push_unique(name_rules.access_scope_names, last_name_of(name_rules.access_scopes.back()));
            }
        }
    }
    name_rules.define_static_array = get_identifier("define_static_array");
    name_rules.head_name_bits = 0;
    for (tree name : name_rules.head_names) {
        name_rules.head_name_bits |= head_name_bit(name);
    }
}

inline void build_name_rules() {
    if (!name_rules.is_ready) {
        make_name_rules();
    }
}

// True when NAME is the last name of the head of a row.  A name whose bit is
// not set costs one test, and each other name a scan of the short list.  Each
// DECL_NAME is an identifier or null.  Complexity: O(rows).
inline bool is_head_name(tree name) {
    return name != NULL_TREE && (name_rules.head_name_bits & head_name_bit(name)) != 0
        && std::find(name_rules.head_names.begin(), name_rules.head_names.end(), name) != name_rules.head_names.end();
}

// The rule of a row whose uses the class walk and the declaration walk read:
// a row whose head is a class or a namespace that a type can name.
bool is_door_rule(Rule rule) { return rule == Rule::ctx_init_door || rule == Rule::ctx_testing_boundary; }

// The declaration that an event names: the first function of an overload set,
// a template, a namespace, a type or a variable, or null.
tree named_decl(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTraceDepth; ++depth) {
        if (BASELINK_P(node)) {
            node = BASELINK_FUNCTIONS(node);
        } else if (TREE_CODE(node) == TEMPLATE_ID_EXPR) {
            node = TREE_OPERAND(node, 0);
        } else if (TREE_CODE(node) == OVERLOAD) {
            node = OVL_FIRST(node);
        } else {
            break;
        }
    }
    return node != NULL_TREE && DECL_P(node) ? node : NULL_TREE;
}

// The declaration that an alias names: the class of a type alias, or the
// namespace of a namespace alias.  Null when DECL is no alias.
tree aliased_decl(tree decl) {
    if (TREE_CODE(decl) == NAMESPACE_DECL) {
        return DECL_NAMESPACE_ALIAS(decl) != NULL_TREE ? ORIGINAL_NAMESPACE(decl) : NULL_TREE;
    }
    if (TREE_CODE(decl) != TYPE_DECL || DECL_IMPLICIT_TYPEDEF_P(decl) || DECL_SELF_REFERENCE_P(decl)
        || TREE_TYPE(decl) == NULL_TREE || !TYPE_P(TREE_TYPE(decl))) {
        return NULL_TREE;
    }
    tree main = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(TREE_TYPE(decl)));
    return main != NULL_TREE && main != decl ? main : NULL_TREE;
}

// The row whose head the declaration DECL is, with no cache, or null.  The
// head of a row of the rule detail-namespace is each namespace named detail
// inside the top namespace of the row, and the head of each other row is its
// entity.
const OwnerRow* row_headed_by_name(tree decl) {
    bool is_detail = DECL_NAME(decl) == name_rules.detail && TREE_CODE(decl) == NAMESPACE_DECL
                  && DECL_NAMESPACE_ALIAS(decl) == NULL_TREE;
    std::string name;
    for (const OwnerRow* row : name_rules.rows) {
        if (row->rule == Rule::detail_namespace) {
            tree top = is_detail ? top_namespace(decl) : NULL_TREE;
            if (top != NULL_TREE && top != decl && qualified_name(top) == row->entity) {
                return row;
            }
            continue;
        }
        if (name.empty()) {
            name = qualified_name(decl);
        }
        if (name == row->entity) {
            return row;
        }
    }
    return nullptr;
}

bool is_plain_namespace(tree decl) {
    return decl != NULL_TREE && TREE_CODE(decl) == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(decl) == NULL_TREE;
}

// The facts of the namespace NS, which is no alias.  The facts of a namespace
// never change in the unit, so each namespace is read one time, and the last
// namespace answers most calls with no test of the map.  Complexity: O(depth
// of the namespaces) at the first call for NS, and O(1) after it.
const NamespaceFacts& facts_of(tree ns) {
    if (ns == name_rules.last_namespace) {
        return *name_rules.last_facts;
    }
    auto found = name_rules.namespaces.find(ns);
    if (found == name_rules.namespaces.end()) {
        NamespaceFacts facts;
        if (DECL_NAME(ns) != NULL_TREE && is_head_name(DECL_NAME(ns))) {
            facts.own = row_headed_by_name(ns);
        }
        tree parent = CP_DECL_CONTEXT(ns);
        if (parent != NULL_TREE && parent != global_namespace && is_plain_namespace(parent)) {
            facts.held = facts_of(parent).held;
        }
        if (facts.held.row == nullptr && facts.own != nullptr) {
            facts.held = Held{facts.own, ns};
        }
        found = name_rules.namespaces.emplace(ns, facts).first;
    }
    name_rules.last_namespace = ns;
    name_rules.last_facts = &found->second;
    return found->second;
}

// The row whose head DECL is, or null.  A name that is no last name of a head
// costs a scan of a short list, and a namespace costs one read of its facts.
const OwnerRow* row_headed_by(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || !is_head_name(DECL_NAME(decl))) {
        return nullptr;
    }
    return is_plain_namespace(decl) ? facts_of(decl).own : row_headed_by_name(decl);
}

// The outermost head of a row that holds DECL, or DECL itself, with its row.
// The walk reads each class and function scope, and the facts of the first
// namespace give the namespaces outside it.  Complexity: O(depth of the
// scopes inside the first namespace).
Held walk_held_by(tree decl) {
    Held found;
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        if (is_plain_namespace(current)) {
            const Held& outer = facts_of(current).held;
            return outer.row != nullptr ? outer : found;
        }
        tree named = TYPE_P(current) ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(current)) : current;
        if (named != NULL_TREE && DECL_P(named)) {
            if (const OwnerRow* row = row_headed_by(named)) {
                found = Held{row, named};
            }
        }
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                  : NULL_TREE;
    }
    return found;
}

// True when DECL, or a class, a function or a namespace that holds it, has the
// last name of a head.  Only then can walk_held_by find a row, so the test
// spares the read of the facts of a namespace.  A chain deeper than kTypeDepth
// gives true.  Complexity: O(depth of the scopes).
bool may_be_held(tree decl) {
    tree current = decl;
    for (int depth = 0; depth < kTypeDepth; ++depth) {
        if (current == NULL_TREE || current == global_namespace) {
            return false;
        }
        tree named = TYPE_P(current) ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(current)) : current;
        if (named != NULL_TREE && DECL_P(named) && is_head_name(DECL_NAME(named))) {
            return true;
        }
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                  : NULL_TREE;
    }
    return true;
}

// held_by with the mark kMarkHeldByNone: a declaration that no row holds
// costs one test of a list after its first read.  A DECL_UID is never used
// again, so a mark stays true after a collection.
Held held_by(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_UID(decl) >= kVerdictSlots) {
        return walk_held_by(decl);
    }
    unsigned char& mark = first_test.marks[DECL_UID(decl)];
    if ((mark & kMarkHeldByNone) != 0) {
        return Held{};
    }
    Held held = walk_held_by(decl);
    if (held.row == nullptr) {
        mark |= kMarkHeldByNone;
    }
    return held;
}

// The outermost detail namespace of a row that holds the namespace NS, or
// null.  Complexity: O(1) after the first call for NS (facts_of).
tree detail_head_of(tree ns) {
    if (!name_rules.has_detail_rows || !is_plain_namespace(ns)) {
        return NULL_TREE;
    }
    const Held& held = facts_of(ns).held;
    return held.row != nullptr && held.row->rule == Rule::detail_namespace ? held.head : NULL_TREE;
}

// The entry of the file of LOCATION, a location that no macro expansion
// gives, or null.  The last line map answers most calls with no search of the
// line table: the map holds each location from its first location to the
// first location of the next map.  The last map of the table can still grow,
// so its range ends after the highest location of the table at the read.
// Complexity: O(1) for a location of the last map, and O(log maps) otherwise.
const FileEntry* ordinary_file_at(location_t location) {
    FileRange& range = name_rules.last_range;
    location_t pure = get_pure_location(line_table, location);
    if (pure >= range.first && pure < range.end) {
        return range.entry;
    }
    const line_map* found = linemap_lookup(line_table, pure);
    if (found == nullptr || !MAP_ORDINARY_P(found)) {
        return nullptr;
    }
    const line_map_ordinary* map = linemap_check_ordinary(found);
    const char* file = ORDINARY_MAP_FILE_NAME(map);
    if (file == nullptr) {
        return nullptr;
    }
    auto index = static_cast<line_map_uint_t>(map - line_table->info_ordinary.maps);
    range.first = MAP_START_LOCATION(map);
    range.end = index + 1 < LINEMAPS_ORDINARY_USED(line_table)
                  ? MAP_START_LOCATION(LINEMAPS_ORDINARY_MAP_AT(line_table, index + 1))
                  : line_table->highest_location + 1;
    range.entry = &classify_file(file);
    return range.entry;
}

// The shape of a use of the entity of HELD: the outermost detail namespace
// for a row of the rule detail-namespace, and the entity of each other row.
std::string shape_of(const Held& held) {
    return held.row->rule == Rule::detail_namespace ? qualified_name(held.head) : held.row->entity;
}

// The pattern of the instantiation FUNCTION, or FUNCTION itself.  A function
// of a template is one holder in each instantiation.
tree pattern_of(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || DECL_TEMPLATE_INFO(function) == NULL_TREE || !DECL_TEMPLOID_INSTANTIATION(function)
        || TREE_CODE(DECL_TI_TEMPLATE(function)) != TEMPLATE_DECL) {
        return function;
    }
    tree template_decl = template_for_substitution(function);
    tree pattern = template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
                     ? DECL_TEMPLATE_RESULT(template_decl)
                     : NULL_TREE;
    return pattern != NULL_TREE && TREE_CODE(pattern) == FUNCTION_DECL ? pattern : function;
}

// True when the event runs at the instantiation of a template: its place is
// the pattern, and a unit that is not the sentinel of a base file can be the
// only one that makes the instantiation.
bool is_late_event() {
    return processing_template_decl == 0 && current_function_decl != NULL_TREE
        && is_in_instantiation(current_function_decl);
}

// The holder text of a use at LOCATION in the body of FUNCTION: the macro
// that spells it, the function_signature of the pattern of its holder, or
// namespace-scope (THE PLACE AND THE ENTITY).
std::string name_holder_text(tree function, location_t location, bool is_macro_read) {
    if (is_macro_read) {
        std::string macro = spelling_macro_of(location);
        if (!macro.empty()) {
            return kMacroHolder + macro;
        }
    }
    tree holder = pattern_of(holder_of(function));
    return holder != NULL_TREE ? holder_signature(holder) : std::string(kNoHolder);
}

// One use of the kind KIND with SHAPE at LOCATION, in the body of FUNCTION, or
// in an initializer when FUNCTION is null.  A use at a site of ROW is none.  A
// base file counts in its sentinel unit, and a late event counts in each unit
// (is_late_event).  A use that the body of a macro of the rule
// no-unchecked-access spells also counts at the expansion point of the macro,
// in the scope of the expansion (THE NAMES OF THE SOURCE).
void record_named(Kind kind, const OwnerRow* row, const std::string& shape, location_t location, tree function,
                  bool is_late) {
    Place place = owned_place(location);
    if (place.file == nullptr || (place.file_class == FileClass::base && !place.file->is_sentinel && !is_late)
        || (row != nullptr && is_at_site(*row, place, function))) {
        return;
    }
    add_finding(kind, nullptr, place, name_holder_text(function, location, true) + ": " + shape);
    if (rule_of(kind) != Rule::no_unchecked_access || spelling_macro_of(location).empty()) {
        return;
    }
    Place outer = owned_place(linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr));
    if (outer.file != nullptr && (outer.file_class != FileClass::base || outer.file->is_sentinel || is_late)) {
        add_finding(kind, nullptr, outer, name_holder_text(function, location, false) + ": " + shape);
    }
}

void record_held(const Held& held, const std::string& prefix, location_t location, tree function, bool is_late) {
    record_named(held.row->kind, held.row, prefix + shape_of(held), location, function, is_late);
}

// The functions of the chain of FUNCTION: FUNCTION, and for a lambda each
// function that holds it.  A function whose lambda uses unchecked uses it too.
template <typename Visit>
void for_each_enclosing(tree function, Visit&& visit) {
    for (int depth = 0; function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL && depth < kTypeDepth; ++depth) {
        visit(function);
        if (!LAMBDA_FUNCTION_P(function)) {
            return;
        }
        tree closure = DECL_CONTEXT(function);
        function = closure != NULL_TREE && TYPE_P(closure) ? decl_function_context(TYPE_MAIN_DECL(closure)) : NULL_TREE;
    }
}

// Marks FUNCTION, and each function that holds the lambda FUNCTION, as a user
// of the unchecked function (THE WALKED RETURN): the mark kMarkUncheckedUser,
// or the set of NameRules for a DECL_UID with no slot.
void mark_unchecked(tree function) {
    for_each_enclosing(function, [](tree enclosing) {
        unsigned uid = DECL_UID(enclosing);
        if (uid < kVerdictSlots) {
            first_test.marks[uid] |= kMarkUncheckedUser;
        } else {
            name_rules.unchecked_users_without_slot.insert(uid);
        }
        name_rules.has_unchecked_users = true;
    });
}

// True when FUNCTION has the mark of mark_unchecked.
inline bool is_unchecked_user(tree function) {
    unsigned uid = DECL_UID(function);
    return uid < kVerdictSlots ? (first_test.marks[uid] & kMarkUncheckedUser) != 0
                               : name_rules.unchecked_users_without_slot.count(uid) != 0;
}

// The mark of THE WALKED RETURN for a declaration that the body of the current
// function names: the unchecked function.
void note_walk_marks(tree decl) {
    if (DECL_NAME(decl) == name_rules.unchecked_name && name_rules.unchecked_row != nullptr
        && current_function_decl != NULL_TREE && qualified_name(decl) == name_rules.unchecked_row->entity) {
        mark_unchecked(current_function_decl);
    }
}

// The name of the access class or of a namespace that holds it, when ENTITY
// is one of them or an alias of one, or "".
std::string access_name_of(tree entity) {
    if (entity == NULL_TREE || name_rules.unchecked_row == nullptr) {
        return {};
    }
    if (TREE_CODE(entity) == NAMESPACE_DECL) {
        tree ns = DECL_NAMESPACE_ALIAS(entity) != NULL_TREE ? ORIGINAL_NAMESPACE(entity) : entity;
        const std::vector<tree>& names = name_rules.access_scope_names;
        if (ns == NULL_TREE || std::find(names.begin(), names.end(), DECL_NAME(ns)) == names.end()) {
            return {};
        }
        std::string name = qualified_name(ns);
        for (const std::string& scope : name_rules.access_scopes) {
            if (name == scope) {
                return name;
            }
        }
        return {};
    }
    tree type = TYPE_P(entity) ? entity : TREE_CODE(entity) == TYPE_DECL ? TREE_TYPE(entity) : NULL_TREE;
    tree decl = type != NULL_TREE ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type)) : NULL_TREE;
    return decl != NULL_TREE && DECL_NAME(decl) == name_rules.access_class_name
                && qualified_name(decl) == name_rules.access_class
             ? name_rules.access_class
             : std::string{};
}

// True when a splice that designates ENTITY names a member of an object: a
// nonstatic data member, a member function that is not static, or a base.
bool is_object_member(tree entity) {
    if (entity == NULL_TREE) {
        return false;
    }
    if (TREE_CODE(entity) == TREE_BINFO || TREE_CODE(entity) == FIELD_DECL) {
        return true;
    }
    tree decl = named_decl(entity);
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    return decl != NULL_TREE && TREE_CODE(decl) == FUNCTION_DECL && DECL_OBJECT_MEMBER_FUNCTION_P(decl);
}

// True when a use at LOCATION, a location that no macro expansion gives, is in
// a file at a site of ROW, a row of the rule detail-namespace: such a use
// never counts.  A row of that rule names no function, so its sites are files
// only.  Complexity: O(1) for a location of the last line map.
bool is_at_detail_site(const OwnerRow& row, location_t location) {
    const FileEntry* entry = ordinary_file_at(location);
    if (entry == nullptr || entry->file_class != FileClass::base) {
        return false;
    }
    Place place;
    place.file = entry;
    place.file_class = entry->file_class;
    return is_at_site(row, place, NULL_TREE);
}

// A use inside a detail namespace of a detail row, in a file outside the
// sites of that row: code that a file outside the layer puts into a detail
// namespace of the layer.  Only a quarantined file can hold such a use, and
// the last line map answers most events (THE NAMES OF THE SOURCE).
[[gnu::noinline]] void check_detail_scope(location_t location) {
    if (current_namespace != name_rules.last_scope) {
        name_rules.last_scope = current_namespace;
        name_rules.last_scope_head = detail_head_of(current_namespace);
    }
    tree head = name_rules.last_scope_head;
    first_test.quiet_namespace = head == NULL_TREE ? current_namespace : NULL_TREE;
    if (head == NULL_TREE) {
        return;
    }
    if (!linemap_location_from_macro_expansion_p(line_table, location)) {
        const FileEntry* entry = ordinary_file_at(location);
        if (entry != nullptr && entry->file_class != FileClass::quarantined) {
            // Each ordinary location of the range of this file is quiet in the
            // current namespace (judge_event).
            first_test.detail_namespace = current_namespace;
            first_test.quiet_first = name_rules.last_range.first;
            first_test.quiet_end = name_rules.last_range.end;
            return;
        }
    }
    Held held = held_by(head);
    if (held.row != nullptr) {
        record_held(held, "", location, current_function_decl, is_late_event());
    }
}

// The key of a class and a head in NameRules::class_heads.
unsigned long long class_head_key(tree type, tree head) {
    return (static_cast<unsigned long long>(TYPE_UID(TYPE_MAIN_VARIANT(type))) << 32) | DECL_UID(head);
}

// True when the token at LOCATION spells NAME, or when the source holds no
// text for LOCATION.  The parser gives the last name of a qualified
// id-expression a second event at the first name, and that event spells a
// different name.
bool spells_name_at(location_t location, tree name) {
    std::string spelled = identifier_spelled_at(location);
    return spelled.empty() || name == NULL_TREE || spelled == IDENTIFIER_POINTER(name);
}

// The head of a door rule whose namespace holds DECL directly, when the source
// spells DECL with no qualifier at LOCATION: a member of the testing door after
// a using-directive, such as bg().  A qualified use names the head itself.
Held unqualified_door_member(tree decl, location_t location) {
    tree scope = CP_DECL_CONTEXT(decl);
    if (scope == NULL_TREE || TREE_CODE(scope) != NAMESPACE_DECL || !is_head_name(DECL_NAME(scope))) {
        return Held{};
    }
    const OwnerRow* row = row_headed_by(scope);
    if (row == nullptr || !is_door_rule(row->rule) || !spells_name_at(location, DECL_NAME(decl))
        || spelled_before_at(location) == ':') {
        return Held{};
    }
    return Held{row, scope};
}

// The head of a row that a use of DECL names, before the test of the place: a
// door or a detail namespace that DECL is or aliases, or for a declaration
// that a door namespace holds directly, that namespace.  The spelling at the
// place decides the last case (unqualified_door_member).
Held named_head(tree decl) {
    if (tree aliased = aliased_decl(decl)) {
        // A type alias of a detail type is a public name of the layer, or a
        // declaration that named the detail namespace where it stands.
        Held held = may_be_held(aliased) ? held_by(aliased) : Held{};
        return held.row != nullptr && held.row->rule == Rule::detail_namespace && TREE_CODE(decl) != NAMESPACE_DECL
                 ? Held{}
                 : held;
    }
    if (const OwnerRow* row = row_headed_by(decl)) {
        return row->rule == Rule::detail_namespace ? held_by(decl) : Held{row, decl};
    }
    tree scope = CP_DECL_CONTEXT(decl);
    if (scope == NULL_TREE || TREE_CODE(scope) != NAMESPACE_DECL || !is_head_name(DECL_NAME(scope))) {
        return Held{};
    }
    const OwnerRow* row = row_headed_by(scope);
    return row != nullptr && is_door_rule(row->rule) ? Held{row, scope} : Held{};
}

// The verdict of DECL at its first event (THE FIRST TEST): kVerdictUnchecked
// for the unchecked function, kVerdictJudged when a use of DECL can name the
// head of a row, and kVerdictFree otherwise.  The tests read the name of DECL
// against the bits of the head names before any other read.  Complexity:
// O(depth of the scopes inside the first namespace) one time for each
// declaration.
unsigned char verdict_of(tree decl) {
    build_name_rules();
    tree name = DECL_NAME(decl);
    if (name != NULL_TREE && name == name_rules.unchecked_name && name_rules.unchecked_row != nullptr
        && qualified_name(decl) == name_rules.unchecked_row->entity) {
        return kVerdictUnchecked;
    }
    // named_head gives a row only for an alias, for a declaration with the
    // last name of a head, and for a member of a namespace with such a name.
    // Only a type and a namespace can be an alias.
    tree_code code = TREE_CODE(decl);
    bool can_name_head =
        is_head_name(name) || ((code == TYPE_DECL || code == NAMESPACE_DECL) && aliased_decl(decl) != NULL_TREE);
    if (!can_name_head) {
        tree scope = CP_DECL_CONTEXT(decl);
        can_name_head = scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && is_head_name(DECL_NAME(scope));
    }
    return can_name_head && named_head(decl).row != nullptr ? kVerdictJudged : kVerdictFree;
}

// A use at LOCATION of DECL, which a rule can judge (verdict_of), and which is
// the unchecked function when IS_UNCHECKED.  A use in a base file that is not
// the sentinel of the unit, at a location that no macro expansion gives,
// outside an instantiation, never counts: the use, and a class that the parser
// reads around it, are in that file.  Such a use leaves after the mark of THE
// WALKED RETURN, with no read of the source.
void judge_name_use(tree decl, location_t location, bool is_unchecked) {
    if (is_unchecked && current_function_decl != NULL_TREE) {
        mark_unchecked(current_function_decl);
    }
    bool is_ordinary = !linemap_location_from_macro_expansion_p(line_table, location);
    if (is_ordinary) {
        const FileEntry* entry = ordinary_file_at(location);
        if (entry != nullptr && entry->file_class == FileClass::base && !entry->is_sentinel && !is_late_event()) {
            return;
        }
    }
    Held held;
    if (aliased_decl(decl) != NULL_TREE || row_headed_by(decl) != nullptr) {
        held = named_head(decl);
    } else {
        held = unqualified_door_member(decl, location);
    }
    if (held.row != nullptr) {
        if (is_ordinary && held.row->rule == Rule::detail_namespace && is_at_detail_site(*held.row, location)) {
            return;
        }
        if (spells_name_at(location, DECL_NAME(decl))) {
            record_held(held, "", location, current_function_decl, is_late_event());
            if (current_class_type != NULL_TREE && is_door_rule(held.row->rule)) {
                name_rules.class_heads.insert(class_head_key(current_class_type, held.head));
            }
        }
        return;
    }
    check_detail_scope(location);
}

// The rest of a use at LOCATION of DECL with the verdict VERDICT: a use that a
// rule can judge, and a use in a namespace that is not quiet.  A use at an
// ordinary location of the last file outside the quarantine that
// check_detail_scope read, in the same namespace, is quiet with no read of the
// line table: a raw location in that range is neither an ad hoc location nor a
// location of a macro expansion.
inline void judge_verdict(tree decl, location_t location, unsigned char verdict) {
    if (verdict != kVerdictFree) {
        judge_name_use(decl, location, verdict == kVerdictUnchecked);
        return;
    }
    tree scope = current_namespace;
    if (scope == first_test.quiet_namespace
        || (scope == first_test.detail_namespace && location >= first_test.quiet_first
            && location < first_test.quiet_end)) {
        return;
    }
    check_detail_scope(location);
}

// True when the bit of the identifier NAME is set in the bits of the head
// names: NAME can be a head name (is_head_name).
inline bool has_head_name_bit(tree name) {
    return name != NULL_TREE && (name_rules.head_name_bits & head_name_bit(name)) != 0;
}

// False when verdict_of gives kVerdictFree for DECL by tests that read only
// the tree of DECL: the name of DECL and of its namespace have no bit of a
// head name, and DECL is no alias (the tests of aliased_decl, with no call).
// The unchecked function has a head name.  Before make_name_rules, each bit
// is set, so the first event goes to verdict_of, which makes the rules.
inline bool may_name_head(tree decl) {
    if (has_head_name_bit(DECL_NAME(decl))) {
        return true;
    }
    tree_code code = TREE_CODE(decl);
    if (code == TYPE_DECL && !DECL_IMPLICIT_TYPEDEF_P(decl) && !DECL_SELF_REFERENCE_P(decl)) {
        tree type = TREE_TYPE(decl);
        if (type != NULL_TREE && TYPE_P(type)) {
            tree main = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type));
            if (main != NULL_TREE && main != decl) {
                return true;
            }
        }
    } else if (code == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(decl) != NULL_TREE) {
        return true;
    }
    tree scope = CP_DECL_CONTEXT(decl);
    return scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && has_head_name_bit(DECL_NAME(scope));
}

// The first event of DECL when may_name_head is true: the list keeps the
// verdict of verdict_of for each later event.
[[gnu::noinline]] void judge_first_candidate(tree decl, location_t location) {
    unsigned char verdict = verdict_of(decl);
    if (DECL_UID(decl) < kVerdictSlots) {
        first_test.verdicts[DECL_UID(decl)] = verdict;
    }
    judge_verdict(decl, location, verdict);
}

// The first event of DECL: the list keeps its verdict for each later event.
// A declaration that may_name_head refuses costs no call of verdict_of.
[[gnu::noinline]] void judge_first_event(tree decl, location_t location) {
    if (may_name_head(decl)) {
        judge_first_candidate(decl, location);
        return;
    }
    if (DECL_UID(decl) < kVerdictSlots) {
        first_test.verdicts[DECL_UID(decl)] = kVerdictFree;
    }
    judge_verdict(decl, location, kVerdictFree);
}

// An event of DECL that THE FIRST TEST did not end: a use that a rule can
// judge, a use in a namespace that is not quiet, and each event of a
// declaration whose DECL_UID has no slot in the list.
[[gnu::noinline]] void judge_event(tree decl, location_t location) {
    unsigned uid = DECL_UID(decl);
    unsigned char verdict = uid < kVerdictSlots ? first_test.verdicts[uid] : 0;
    if (verdict == 0) {
        judge_first_event(decl, location);
        return;
    }
    judge_verdict(decl, location, verdict);
}

// THE FIRST TEST of an event of DECL.  A declaration with the verdict
// kVerdictFree, in a quiet namespace, leaves after one read of the list of
// verdicts by its DECL_UID and one test of the current namespace, with no read
// of a name or of the line table.  The first event of a declaration goes to
// judge_first_event, and each other event to judge_event.
inline void judge_decl_event(tree decl, location_t location) {
    unsigned uid = DECL_UID(decl);
    if (uid < kVerdictSlots) {
        unsigned char verdict = first_test.verdicts[uid];
        if (verdict == kVerdictFree && current_namespace == first_test.quiet_namespace) {
            return;
        }
        if (verdict == 0) {
            judge_first_event(decl, location);
            return;
        }
    }
    judge_event(decl, location);
}

// An event of an overload set: THE FIRST TEST of its first function in the
// usual shapes, a member function and a function, and named_decl for each
// other node that is no declaration.
[[gnu::noinline]] void judge_overload_event(tree node, location_t location) {
    tree functions = BASELINK_P(node) ? BASELINK_FUNCTIONS(node) : node;
    tree first = functions != NULL_TREE && TREE_CODE(functions) == OVERLOAD ? OVL_FIRST(functions) : functions;
    tree decl = first != NULL_TREE && DECL_P(first) ? first : named_decl(node);
    if (decl != NULL_TREE) {
        judge_decl_event(decl, location);
    }
}

// True when LOCATION is an ordinary location of a file outside the root.  The
// function keeps the range of the file of an ordinary LOCATION in the state of
// THE FIRST TEST: the outside range for a file outside the root, and the inside
// range for each other file.  A location of a macro expansion keeps no range.
[[gnu::noinline]] bool judge_location(location_t location) {
    bool is_adhoc = IS_ADHOC_LOC(location);
    if ((!is_adhoc && location >= LINEMAPS_MACRO_LOWEST_LOCATION(line_table))
        || (is_adhoc && linemap_location_from_macro_expansion_p(line_table, location))) {
        return false;
    }
    const FileEntry* entry = ordinary_file_at(location);
    const FileRange& range = name_rules.last_range;
    if (entry == nullptr || range.end <= range.first) {
        return false;
    }
    if (entry->file_class == FileClass::outside) {
        first_test.outside_first = range.first;
        first_test.outside_length = range.end - range.first;
        return true;
    }
    first_test.inside_first = range.first;
    first_test.inside_length = range.end - range.first;
    first_test.is_inside_counted = entry->file_class == FileClass::quarantined || is_read_as_language(*entry);
    return false;
}

// ── The folded names (THE FOLDED NAMES) ─────────────────────────────────

// THE FOLDED NAMES
//     The front end folds a constant expression and an unevaluated operand
//     while it parses.  A call of strlen in a constant initializer, an
//     alignof, a noexcept, a type trait, a static_assert and a template
//     argument then leave no name in the tree that the walk of a body reads.
//     PLUGIN_NAME_USE gives each name that the parser resolves, before the
//     fold, and PLUGIN_MEMBER_ACCESS gives the member of each class member
//     access, such as size in std::array<int, 3>{}.size().  The instantiation
//     of a template gives the member of an access that depends on a template
//     parameter, such as items.push_back(1) for a std::vector.
//     PLUGIN_REFLECTION gives the entity of a splice, which spells no name of
//     it (note_reflection_name).  The plugin keeps each such name of a library
//     declaration in a counted file (a parsed name).  A data member of a member access is no parsed name, because the
//     walk gives no finding of a data member.  The parser gives a qualified
//     name an event at its first name too, and only the event where the
//     source spells the identifier of the declaration is a parsed name.  The
//     plugin also keeps each library name that a walk reads (a walked name).
//     At the end of the unit, a walked name claims parsed names of its
//     identifier in its file (Claim).  A parsed typedef also has the
//     identifier of its class:
//       * A declared type claims each parsed name of each of its library
//         names on the line of the declaration and on the kSpanLines lines
//         before it, except a name of an unevaluated operand.
//       * Each other type that a walk reads claims each such parsed name
//         kSpanLines lines before or after its line at most.
//       * Each other walked name claims the nearest parsed name that no other
//         walked name claimed, in the same lines.  The walk gives an operand
//         the location of the last node with a location, so a name can stand
//         some lines from its record.
//     The parser finishes a declaration of a function before the unit can
//     define it, and the definition moves the location of the function.  So
//     the end of each declaration and of each class adds the declared types
//     of its functions and members as walked names, at their location then.
//     The parameters count only for a function that the unit defines, because
//     the walk reads no parameter of a function with no definition.
//     A folded member claims the parsed names of its classes before it on
//     its line, so std::numeric_limits<int>::max is one use.  The member of
//     a member access claims no class, because its object is a use of its
//     own.  Each other parsed name that no walked name claims is a folded
//     name.  The walk's own judgment of its declaration gives its finding: the
//     admit rows, the constants of an enumeration and the C library.  An
//     overload set has no argument count.  In an evaluated operand, a
//     candidate that a row admits admits the set, because the front end folds
//     std::move(value) to a cast before the walk.  In an unevaluated operand,
//     the first candidate that no row admits gives the finding, such as
//     std::move of three arguments.  A dependent name gives no event at parse
//     time.  The walk of the pattern reads a dependent qualified name, and the
//     instantiation gives a dependent member.  A name that only
//     argument-dependent lookup finds gives no event either (the control
//     control-known-gaps of the plugin witness).  A using-enum-declaration in a function body or in
//     a class names its enumeration, and not each enumerator (check_using).
constexpr int kSpanLines = 2;
// The verdicts of library_name_verdicts: no library declaration, and a
// library declaration.
constexpr unsigned char kNotLibrary = 1;
constexpr unsigned char kLibrary = 2;

// One name of the parser: its place, its identifier, the declaration or the
// candidates of an overload set, and whether the parser read it in an
// unevaluated operand.  A declaration of the library is never collected, so
// the trees stay valid to the end of the unit.
struct ParsedName {
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    tree identifier = NULL_TREE;
    tree class_identifier = NULL_TREE;  // the class of a typedef (class_identifier_of), or null
    location_t location = UNKNOWN_LOCATION;
    tree decl = NULL_TREE;
    std::vector<tree> candidates;
    bool is_unevaluated = false;
    bool is_claimed = false;
    // The member of a class member access (PLUGIN_MEMBER_ACCESS), whose object
    // is a use of its own, and not the class of a qualified name.
    bool is_member = false;
};

// The parsed names that one walked name claims (THE FOLDED NAMES): the
// nearest one, each one of a type around its line, and each one of a declared
// type on its line and before it.
enum class Claim : std::uint8_t {
    nearest,
    around,
    before,
};

// One library name that a walk read.  A name in a macro expansion also keeps
// the outermost expansion point, when that point is in a counted file at a
// different place.  The walk can give an operand of a macro argument the
// location of a token of the macro body, such as the ! of if (!(cond)), and
// the parsed name of the argument stands at the expansion point.
struct WalkedName {
    const FileEntry* file = nullptr;
    int line = 0;
    int column = 0;
    tree identifier = NULL_TREE;
    Claim claim = Claim::nearest;
    const FileEntry* expansion_file = nullptr;
    int expansion_line = 0;
    int expansion_column = 0;
};

// The parameters of a function declaration that the parser finished: the type
// and the location of each.  The walk reads them at the definition of the
// function, when the unit defines it.
struct DeclaredParameters {
    unsigned uid = 0;
    std::vector<std::pair<tree, location_t>> parameters;
};

struct FoldedNames {
    std::vector<ParsedName> parsed;
    std::vector<WalkedName> walked;
    // The parameters of each declaration of a function, and the DECL_UID of
    // each function that the unit defines.
    std::vector<DeclaredParameters> declared;
    std::unordered_set<unsigned> defined;
    // The parsed and the walked names by their place and identifier, so a
    // second event of one use, as a tentative parse gives, keeps one entry.
    std::unordered_set<std::string> parsed_keys;
    std::unordered_set<std::string> walked_keys;
    // The library names of each type that a walk read, by its TYPE_UID.  A
    // TYPE_UID is never used again, and an identifier is never collected, so
    // the map stays valid after a collection.
    std::unordered_map<unsigned, std::vector<tree>> type_names;
};

FoldedNames folded_names;

// The library verdict of each declaration by its DECL_UID, 0 before its first
// read (library_of).  The list is zero at load, as the lists of THE FIRST TEST
// are, and a declaration with a DECL_UID of kVerdictSlots or more gets its
// verdict at each read.
std::array<unsigned char, kVerdictSlots> library_name_verdicts{};

bool is_library_decl(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl)) {
        return false;
    }
    unsigned uid = DECL_UID(decl);
    if (uid < kVerdictSlots && library_name_verdicts[uid] != 0) {
        return library_name_verdicts[uid] == kLibrary;
    }
    bool is_library = library_of(decl) != Library::none;
    if (uid < kVerdictSlots) {
        library_name_verdicts[uid] = is_library ? kLibrary : kNotLibrary;
    }
    return is_library;
}

// A place of a counted file: a quarantined file, or a file of language_of (THE
// BASE KINDS of plugin.h).
bool counts_folded_name(const Place& place) {
    return place.file != nullptr && (place.file_class == FileClass::quarantined || language_of(place) != nullptr);
}

// False when the place of LOCATION cannot be in a counted file, after one read
// of the range of a file (ordinary_file_at).  The place of an ordinary location
// is its own file.  A macro expansion whose outermost expansion point is in a
// file outside the root spells each token outside the root too, as a macro of
// a library header that a library header expands.  Each other location of a
// macro expansion needs place_of.
bool may_count_at(location_t location) {
    if (linemap_location_from_macro_expansion_p(line_table, location)) {
        location_t point = linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
        const FileEntry* entry = ordinary_file_at(point);
        return entry != nullptr && entry->file_class != FileClass::outside;
    }
    const FileEntry* entry = ordinary_file_at(location);
    return entry != nullptr && (entry->file_class == FileClass::quarantined || is_read_as_language(*entry));
}

std::string name_key(const Place& place, tree identifier) {
    return place.file->relative + ':' + std::to_string(place.line) + ':' + std::to_string(place.column) + ' '
         + IDENTIFIER_POINTER(identifier);
}

// One walked name of IDENTIFIER at PLACE, the place of LOCATION.  A second
// read of one use at one place keeps one entry, so one use claims one parsed
// name.  Each expansion of one token of a macro body is a use of its own, so
// the expansion point is part of the place.
void add_walked(const Place& place, location_t location, tree identifier, Claim claim) {
    if (identifier == NULL_TREE || !counts_folded_name(place)) {
        return;
    }
    WalkedName walked{place.file, place.line, place.column, identifier, claim};
    std::string key = name_key(place, identifier) + ' ' + std::to_string(static_cast<int>(claim));
    if (linemap_location_from_macro_expansion_p(line_table, location)) {
        Place point = place_of(linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr),
                               Scope::quarantine);
        if (counts_folded_name(point) && (point.file != place.file || point.line != place.line)) {
            walked.expansion_file = point.file;
            walked.expansion_line = point.line;
            walked.expansion_column = point.column;
            key += ' ' + name_key(point, identifier);
        }
    }
    if (folded_names.walked_keys.insert(key).second) {
        folded_names.walked.push_back(walked);
    }
}

// Each place of WALKED: its own place, and its expansion point (WalkedName).
template <typename Visit>
void for_each_walked_place(const WalkedName& walked, Visit&& visit) {
    visit(walked.file, walked.line, walked.column);
    if (walked.expansion_file != nullptr) {
        visit(walked.expansion_file, walked.expansion_line, walked.expansion_column);
    }
}

// The identifier of DECL and of each class that holds it: a walk that reads
// std::numeric_limits<int>::max reads the name of the class too.
template <typename Visit>
void for_each_held_name(tree decl, Visit&& visit) {
    for (int depth = 0; decl != NULL_TREE && depth < kTraceDepth; ++depth) {
        if (TYPE_P(decl)) {
            decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(decl));
            continue;
        }
        if (!DECL_P(decl) || TREE_CODE(decl) == NAMESPACE_DECL) {
            return;
        }
        if (DECL_NAME(decl) != NULL_TREE && TREE_CODE(DECL_NAME(decl)) == IDENTIFIER_NODE) {
            visit(DECL_NAME(decl));
        }
        decl = CP_DECL_CONTEXT(decl);
    }
}

// The library names that TYPE spells: a typedef and an alias template of the
// library, each library class and each library template that the type, its
// template arguments, its enclosing classes and the types of a function type
// hold.  A decltype holds names of an unevaluated operand, which the parser
// gives as their own names.  The answer of each type is kept.  Complexity:
// O(nodes of the type tree), one time for each type.
const std::vector<tree>& library_names_of_type(tree type, int depth);

void add_library_names_of_type(tree type, int depth, std::vector<tree>& names) {
    const std::vector<tree>& found = library_names_of_type(type, depth);
    names.insert(names.end(), found.begin(), found.end());
}

const std::vector<tree>& library_names_of_type(tree type, int depth) {
    static const std::vector<tree> none;
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type) || depth > kTypeDepth) {
        return none;
    }
    auto cached = folded_names.type_names.find(TYPE_UID(type));
    if (cached != folded_names.type_names.end()) {
        return cached->second;
    }
    std::vector<tree> names;
    if (tree name = TYPE_NAME(type); name != NULL_TREE && TREE_CODE(name) == TYPE_DECL) {
        if (is_library_decl(name)) {
            for_each_held_name(name, [&names](tree identifier) { names.push_back(identifier); });
        }
        if (tree info = TYPE_ALIAS_TEMPLATE_INFO(type);
            info != NULL_TREE && TI_TEMPLATE(info) != NULL_TREE && is_library_decl(TI_TEMPLATE(info))) {
            names.push_back(DECL_NAME(TI_TEMPLATE(info)));
        }
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
        case ARRAY_TYPE:
        case VECTOR_TYPE:
        case COMPLEX_TYPE:
            add_library_names_of_type(TREE_TYPE(type), depth + 1, names);
            break;
        case OFFSET_TYPE:
            add_library_names_of_type(TYPE_OFFSET_BASETYPE(type), depth + 1, names);
            add_library_names_of_type(TREE_TYPE(type), depth + 1, names);
            break;
        case FUNCTION_TYPE:
        case METHOD_TYPE:
            add_library_names_of_type(TREE_TYPE(type), depth + 1, names);
            for (tree parameter = TYPE_ARG_TYPES(type); parameter != NULL_TREE && parameter != void_list_node;
                 parameter = TREE_CHAIN(parameter)) {
                add_library_names_of_type(TREE_VALUE(parameter), depth + 1, names);
            }
            break;
        case TYPENAME_TYPE:
            add_library_names_of_type(TYPE_CONTEXT(type), depth + 1, names);
            break;
        case TYPE_PACK_EXPANSION:
            add_library_names_of_type(PACK_EXPANSION_PATTERN(type), depth + 1, names);
            break;
        case RECORD_TYPE:
        case UNION_TYPE:
        case ENUMERAL_TYPE: {
            if (TYPE_PTRMEMFUNC_P(type)) {
                add_library_names_of_type(TYPE_PTRMEMFUNC_FN_TYPE(type), depth + 1, names);
                break;
            }
            tree main_type = TYPE_MAIN_VARIANT(type);
            tree decl = TYPE_MAIN_DECL(main_type);
            if (decl != NULL_TREE && is_library_decl(decl)) {
                for_each_held_name(decl, [&names](tree identifier) { names.push_back(identifier); });
            }
            if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE
                && TI_ARGS(CLASSTYPE_TEMPLATE_INFO(main_type)) != NULL_TREE) {
                tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(CLASSTYPE_TEMPLATE_INFO(main_type)));
                for (int index = 0;
                     arguments != NULL_TREE && TREE_CODE(arguments) == TREE_VEC && index < TREE_VEC_LENGTH(arguments);
                     ++index) {
                    tree argument = TREE_VEC_ELT(arguments, index);
                    if (argument != NULL_TREE && TYPE_P(argument)) {
                        add_library_names_of_type(argument, depth + 1, names);
                    } else if (argument != NULL_TREE && TREE_CODE(argument) == TEMPLATE_DECL
                               && is_library_decl(argument)) {
                        names.push_back(DECL_NAME(argument));
                    }
                }
            }
            if (tree context = CP_TYPE_CONTEXT(main_type); context != NULL_TREE && TYPE_P(context)) {
                add_library_names_of_type(context, depth + 1, names);
            }
            break;
        }
        default:
            break;
    }
    return folded_names.type_names.emplace(TYPE_UID(type), std::move(names)).first->second;
}

// The identifier of the class that a typedef or an alias names, or null: the
// event of std::string names the typedef, and the walk reads the class
// std::basic_string.
tree class_identifier_of(tree decl) {
    if (TREE_CODE(decl) != TYPE_DECL || DECL_ORIGINAL_TYPE(decl) == NULL_TREE) {
        return NULL_TREE;
    }
    tree type = TYPE_MAIN_VARIANT(TREE_TYPE(decl));
    tree main = type != NULL_TREE && TYPE_P(type) ? TYPE_MAIN_DECL(type) : NULL_TREE;
    tree name = main != NULL_TREE && main != decl ? DECL_NAME(main) : NULL_TREE;
    return name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE ? name : NULL_TREE;
}

// True when a row with no `until` admits a candidate of an overload set, for a
// use at LOCATION.
bool admits_a_candidate(const std::vector<tree>& candidates, location_t location) {
    const FileEntry* language = language_file_of(location);
    for (tree candidate : candidates) {
        tree function = TREE_CODE(candidate) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(candidate) : candidate;
        if (function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL && library_of(function) == Library::standard
            && is_plainly_admitted(admitting_row(function, Use::other, NULL_TREE, language))) {
            return true;
        }
    }
    return false;
}

// The identifier of a declaration that an event names, or null.
tree identifier_of(tree decl) {
    tree name = decl != NULL_TREE && DECL_P(decl) ? DECL_NAME(decl) : NULL_TREE;
    return name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE ? name : NULL_TREE;
}

// The parsed name of NODE at LOCATION, when NODE names a library declaration
// in a counted file (THE FOLDED NAMES).  A namespace and an enumerator name
// no finding, and neither does a data member that IS_MEMBER marks, the member
// of a class member access.  A splice spells no name, so a name that is not
// IS_SPELLED takes no test of the spelling.  A first test of the library
// verdict by DECL_UID leaves for a declaration of the tree.
[[gnu::noinline]] void note_parsed_name(tree node, location_t location, bool is_member = false,
                                        bool is_spelled = true) {
    tree functions = BASELINK_P(node) ? BASELINK_FUNCTIONS(node) : node;
    tree first = functions != NULL_TREE && TREE_CODE(functions) == OVERLOAD ? OVL_FIRST(functions) : functions;
    if (first == NULL_TREE || !DECL_P(first) || TREE_CODE(first) == NAMESPACE_DECL || TREE_CODE(first) == CONST_DECL
        || (is_member && TREE_CODE(first) == FIELD_DECL) || !is_library_decl(first)) {
        return;
    }
    tree identifier = identifier_of(first);
    if (identifier == NULL_TREE || !may_count_at(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (!counts_folded_name(place)) {
        return;
    }
    // The parser gives a qualified name two events: one at its last name, and
    // one at its first name.  The source spells the identifier of the
    // declaration only at the last name, so the event there is the use.  A
    // tentative parse gives one event more than one time, and the place keys
    // one use.
    if ((is_spelled && !spells_identifier_at(location, identifier, is_member))
        || !folded_names.parsed_keys.insert(name_key(place, identifier)).second) {
        return;
    }
    ParsedName parsed;
    parsed.file = place.file;
    parsed.line = place.line;
    parsed.column = place.column;
    parsed.identifier = identifier;
    parsed.class_identifier = class_identifier_of(first);
    parsed.location = location;
    parsed.decl = first;
    parsed.is_unevaluated = cp_unevaluated_operand > 0;
    parsed.is_member = is_member;
    if (functions != first) {
        for (ovl_iterator candidate(functions, true); candidate; ++candidate) {
            parsed.candidates.push_back(*candidate);
        }
    }
    folded_names.parsed.push_back(std::move(parsed));
}

// The finding of a folded name, as the walk judges a use of its declaration.
void judge_parsed(const ParsedName& parsed) {
    tree decl = parsed.decl;
    location_t location = parsed.location;
    if (!parsed.candidates.empty()) {
        // The front end folds a call of std::move of one argument to a cast
        // before the walk of the body, so in an evaluated operand a candidate
        // that a row admits admits the set.  An unevaluated operand has no
        // call that the walk reads, and the set fails closed there.
        if (!parsed.is_unevaluated && admits_a_candidate(parsed.candidates, location)) {
            return;
        }
        for (tree candidate : parsed.candidates) {
            if (check_function_use(candidate, location)) {
                return;
            }
        }
        return;
    }
    switch (TREE_CODE(decl)) {
        case FUNCTION_DECL:
            check_function_use(decl, location);
            return;
        case VAR_DECL:
            check_variable_use(decl, location);
            return;
        case TEMPLATE_DECL:
            check_template_use(decl, location);
            return;
        case TYPE_DECL:
            record_named_entities(TREE_TYPE(decl), location);
            return;
        default:
            return;
    }
}

// The distance of two places of one file, by line first.
long name_distance(int line, int column, const ParsedName& parsed) {
    return static_cast<long>(std::abs(parsed.line - line)) * 100000L + std::abs(parsed.column - column);
}

// The claims of THE FOLDED NAMES, and the judgment of each parsed name that no
// walked name claims.  Complexity: O((p + w) log (p + w)) for p parsed and w
// walked names, and O(p * w) for the names of one identifier of one file.
void judge_folded() {
    // The walk reads the parameters of a declaration where the unit defines
    // the function.  A declaration with no definition in the unit keeps no
    // parameters at the end of the unit, so its parameter names are no
    // walked names.
    for (const DeclaredParameters& declared : folded_names.declared) {
        if (folded_names.defined.count(declared.uid) == 0) {
            continue;
        }
        for (const auto& [type, location] : declared.parameters) {
            note_walked_type(type, location, true);
        }
    }
    // A parsed typedef is found by its own identifier and by the identifier of
    // its class.
    std::unordered_map<std::string, std::vector<std::size_t>> parsed_of;
    for (std::size_t index = 0; index < folded_names.parsed.size(); ++index) {
        const ParsedName& parsed = folded_names.parsed[index];
        parsed_of[parsed.file->relative + ' ' + IDENTIFIER_POINTER(parsed.identifier)].push_back(index);
        if (parsed.class_identifier != NULL_TREE && parsed.class_identifier != parsed.identifier) {
            parsed_of[parsed.file->relative + ' ' + IDENTIFIER_POINTER(parsed.class_identifier)].push_back(index);
        }
    }
    // The types claim first, so a walked use does not take a name of a type.
    std::vector<const WalkedName*> uses;
    for (const WalkedName& walked : folded_names.walked) {
        if (walked.claim == Claim::nearest) {
            uses.push_back(&walked);
            continue;
        }
        bool is_declaration = walked.claim == Claim::before;
        for_each_walked_place(walked, [&](const FileEntry* file, int line, int) {
            auto found = parsed_of.find(file->relative + ' ' + IDENTIFIER_POINTER(walked.identifier));
            if (found == parsed_of.end()) {
                return;
            }
            int last = is_declaration ? line : line + kSpanLines;
            for (std::size_t index : found->second) {
                ParsedName& parsed = folded_names.parsed[index];
                if ((!is_declaration || !parsed.is_unevaluated) && parsed.line <= last
                    && parsed.line >= line - kSpanLines) {
                    parsed.is_claimed = true;
                }
            }
        });
    }
    for (const WalkedName* walked : uses) {
        ParsedName* nearest = nullptr;
        long nearest_distance = 0;
        for_each_walked_place(*walked, [&](const FileEntry* file, int line, int column) {
            auto found = parsed_of.find(file->relative + ' ' + IDENTIFIER_POINTER(walked->identifier));
            if (found == parsed_of.end()) {
                return;
            }
            for (std::size_t index : found->second) {
                ParsedName& parsed = folded_names.parsed[index];
                if (parsed.is_claimed || std::abs(parsed.line - line) > kSpanLines) {
                    continue;
                }
                long distance = name_distance(line, column, parsed);
                if (nearest == nullptr || distance < nearest_distance) {
                    nearest = &parsed;
                    nearest_distance = distance;
                }
            }
        });
        if (nearest != nullptr) {
            nearest->is_claimed = true;
        }
    }
    // A qualified name, such as std::numeric_limits<int>::max, gives a parsed
    // name of each class in it, and the walk reads it as one use of its last
    // name.  So a folded member claims the nearest name of each class that
    // holds it, before it on its line.  The object of a class member access is
    // a use of its own, so the member of such an access claims no class.
    for (ParsedName& member : folded_names.parsed) {
        if (member.is_claimed || member.is_member) {
            continue;
        }
        bool is_last = true;
        for_each_held_name(member.decl, [&](tree identifier) {
            if (is_last) {
                is_last = false;
                return;
            }
            auto found = parsed_of.find(member.file->relative + ' ' + IDENTIFIER_POINTER(identifier));
            if (found == parsed_of.end()) {
                return;
            }
            ParsedName* nearest = nullptr;
            for (std::size_t index : found->second) {
                ParsedName& holder = folded_names.parsed[index];
                if (!holder.is_claimed && holder.line == member.line && holder.column < member.column
                    && (nearest == nullptr || holder.column > nearest->column)) {
                    nearest = &holder;
                }
            }
            if (nearest != nullptr) {
                nearest->is_claimed = true;
            }
        });
    }
    for (const ParsedName& parsed : folded_names.parsed) {
        if (!parsed.is_claimed) {
            judge_parsed(parsed);
        }
    }
}

// The callback of each event of PLUGIN_NAME_USE.  The front end calls it
// directly (plugin_name_use_func) with a declaration, an overload set or a
// BASELINK, and never with null.  An event at a location of the last file
// outside the root leaves after one test of the location.  Each other event
// keeps its parsed name (THE FOLDED NAMES), and each event that THE FIRST
// TEST does not end goes to a function of its own, so the callback saves no
// register.
void on_name_use(tree node, location_t location, void*) {
    if (location - first_test.outside_first < first_test.outside_length) {
        return;
    }
    if (location - first_test.inside_first >= first_test.inside_length && judge_location(location)) {
        return;
    }
    // An event of the inside range of a file that is not counted keeps no
    // parsed name, with no call.
    if (location - first_test.inside_first >= first_test.inside_length || first_test.is_inside_counted) {
        note_parsed_name(node, location);
    }
    if (!name_rules.has_rows) {
        return;
    }
    if (first_test.is_decl_code[TREE_CODE(node)] == 0) {
        judge_overload_event(node, location);
        return;
    }
    judge_decl_event(node, location);
}

// The declared types of DECL, at the location that the declaration has now: a
// function, a variable and a data member.
void note_declared_types(tree decl) {
    if (TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_ARTIFICIAL(decl)) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    switch (TREE_CODE(decl)) {
        case FUNCTION_DECL: {
            note_walked_type(TREE_TYPE(TREE_TYPE(decl)), location, true);
            DeclaredParameters declared;
            declared.uid = DECL_UID(decl);
            for (tree parameter = DECL_ARGUMENTS(decl); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
                if (!DECL_ARTIFICIAL(parameter)) {
                    declared.parameters.emplace_back(TREE_TYPE(parameter), DECL_SOURCE_LOCATION(parameter));
                }
            }
            if (!declared.parameters.empty()) {
                folded_names.declared.push_back(std::move(declared));
            }
            return;
        }
        case VAR_DECL:
        case FIELD_DECL:
            note_walked_type(TREE_TYPE(decl), location, true);
            return;
        default:
            return;
    }
}

// The declared types of a declaration when the parser finishes it.  A function
// that a header declares and a source file of the unit defines takes the
// location of its definition, so the walk reads its types there.  The note of
// the declaration keeps the names of the header claimed.
void on_finish_decl_names(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl != NULL_TREE && DECL_P(decl) && may_count_at(DECL_SOURCE_LOCATION(decl))) {
        note_declared_types(decl);
    }
}

// Each function that the unit defines (DeclaredParameters).
void on_finish_parse_function_folded(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL) {
        folded_names.defined.insert(DECL_UID(function));
    }
}

// The declared types of each member of a class when the parser finishes it.
// The parser finishes a member function with no PLUGIN_FINISH_DECL, and its
// definition in a source file of the unit moves its location there.
void on_finish_type_names(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || TYPE_MAIN_DECL(type) == NULL_TREE
        || !may_count_at(DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(type)))) {
        return;
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        note_declared_types(member);
    }
}

// A using-declaration and a using-directive of namespace scope: the front end
// keeps each as a binding of the namespace, with no tree that a walk reads, so
// the event judges it as check_using and the walk of a body judge the forms
// of a function body and of a class.
void judge_namespace_using(const plugin_using_form_data& data) {
    if (current_function_decl != NULL_TREE || current_class_type != NULL_TREE
        || !counts_folded_name(place_of(data.location, Scope::quarantine))) {
        return;
    }
    if (data.kind == PLUGIN_USING_DECLARATION && data.decl != NULL_TREE && TREE_CODE(data.decl) == USING_DECL) {
        check_using(data.decl);
    } else if (data.kind == PLUGIN_USING_DIRECTIVE && TREE_CODE(data.target) == NAMESPACE_DECL) {
        record_namespace_name(data.target, std::string{}, data.location);
    }
}

// A using-declaration names its last name with no event of PLUGIN_NAME_USE:
// a use of a head, and of the access class.  An alias declaration of the
// access class is a use of it too.  A form of namespace scope is a use of the
// library too (judge_namespace_using).
void on_using_form(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_using_form_data*>(gcc_data);
    if (data == nullptr || data->target == NULL_TREE) {
        return;
    }
    judge_namespace_using(*data);
    if (!name_rules.has_rows) {
        return;
    }
    build_name_rules();
    bool is_late = is_late_event();
    if (data->kind == PLUGIN_ALIAS_DECLARATION) {
        std::string access = access_name_of(data->target);
        if (!access.empty()) {
            record_named(Kind::access_alias, name_rules.unchecked_row, access, data->location, current_function_decl,
                         is_late);
        }
        return;
    }
    if (data->kind != PLUGIN_USING_DECLARATION) {
        return;
    }
    tree targets = BASELINK_P(data->target) ? BASELINK_FUNCTIONS(data->target) : data->target;
    for (lkp_iterator candidate(targets); candidate; ++candidate) {
        tree target = *candidate;
        if (const OwnerRow* row = row_headed_by(target)) {
            record_held(row->rule == Rule::detail_namespace ? held_by(target) : Held{row, target}, "", data->location,
                        current_function_decl, is_late);
        }
        std::string access = access_name_of(target);
        if (!access.empty()) {
            record_named(Kind::access_alias, name_rules.unchecked_row, access, data->location, current_function_decl,
                         is_late);
        }
    }
}

// The member of a class member access: a member of a class of a head of a door
// rule, the unchecked function through an object, and a member that a splice
// designates.  A member of a class whose name and scope are no head costs two
// tests of the bits of the head names, and the list of the member verdicts
// keeps that verdict for each later access.
[[gnu::noinline]] void judge_member_access(tree node, location_t location, bool is_splice) {
    build_name_rules();
    if (is_splice && name_rules.unchecked_row != nullptr) {
        record_named(Kind::member_splice, name_rules.unchecked_row, kSpliceShape, location, current_function_decl,
                     is_late_event());
    }
    tree member = named_decl(node);
    if (member == NULL_TREE
        || (DECL_UID(member) < kVerdictSlots && first_test.member_verdicts[DECL_UID(member)] == kVerdictFree)) {
        return;
    }
    note_walk_marks(member);
    if (name_rules.unchecked_row != nullptr && DECL_NAME(member) == name_rules.unchecked_name
        && qualified_name(member) == name_rules.unchecked_row->entity) {
        record_named(Kind::unchecked_use, name_rules.unchecked_row, name_rules.unchecked_row->entity, location,
                     current_function_decl, is_late_event());
        return;
    }
    tree scope = CP_DECL_CONTEXT(member);
    tree class_decl = scope != NULL_TREE && TYPE_P(scope) ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(scope)) : NULL_TREE;
    tree outer = class_decl != NULL_TREE ? CP_DECL_CONTEXT(class_decl) : NULL_TREE;
    bool can_be_held = class_decl != NULL_TREE
                    && (is_head_name(DECL_NAME(class_decl))
                        || (outer != NULL_TREE && DECL_P(outer) && is_head_name(DECL_NAME(outer))));
    if (!can_be_held) {
        if (DECL_UID(member) < kVerdictSlots) {
            first_test.member_verdicts[DECL_UID(member)] = kVerdictFree;
        }
        return;
    }
    Held held = held_by(member);
    if (held.row != nullptr && is_door_rule(held.row->rule)) {
        record_held(held, "", location, current_function_decl, is_late_event());
    }
}

// The callback of each event of PLUGIN_MEMBER_ACCESS.  The front end calls it
// directly (plugin_member_access_func), at parse time and at the instantiation
// of a template.  An access in a counted file keeps its member as a parsed name
// (THE FOLDED NAMES): the front end can fold a call of a member function, such
// as std::array<int, 3>{}.size() in a constant initializer, and the walk of a
// pattern cannot resolve a dependent member.  A splice of a member spells no
// name of the member.  An access at a location of the last file outside the
// root leaves after one test of the location.  For the rules of THE NAMES OF
// THE SOURCE, an access to a data member that no rule can judge, with no
// splice, leaves after one read of the list of member verdicts.
void on_member_access(tree node, tree, location_t location, bool is_splice, void*) {
    bool is_outside = location - first_test.outside_first < first_test.outside_length
                   || (location - first_test.inside_first >= first_test.inside_length && judge_location(location));
    if (!is_outside
        && (location - first_test.inside_first >= first_test.inside_length || first_test.is_inside_counted)) {
        note_parsed_name(node, location, true, !is_splice);
    }
    if (!name_rules.has_rows) {
        return;
    }
    if (!is_splice && first_test.is_decl_code[TREE_CODE(node)] != 0 && DECL_UID(node) < kVerdictSlots
        && first_test.member_verdicts[DECL_UID(node)] == kVerdictFree) {
        return;
    }
    judge_member_access(node, location, is_splice);
}

// The reflection of a library namespace and the splice of a library entity in
// a counted file (THE FOLDED NAMES).  The parser gives each other name of a
// reflect-expression an event of PLUGIN_NAME_USE.  A splice spells no name of
// the entity that it designates.
void note_reflection_name(const plugin_reflection_data& data) {
    tree entity = data.entity;
    if (entity == NULL_TREE || entity == error_mark_node || !may_count_at(data.location)) {
        return;
    }
    if (TREE_CODE(entity) == NAMESPACE_DECL) {
        if (counts_folded_name(place_of(data.location, Scope::quarantine))) {
            record_namespace_name(entity, std::string{}, data.location);
        }
        return;
    }
    if (data.kind != PLUGIN_SPLICE) {
        return;
    }
    if (TYPE_P(entity)) {
        entity = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(entity));
    }
    if (entity != NULL_TREE) {
        note_parsed_name(entity, data.location, false, false);
    }
}

// A reflection and a splice: the names of THE FOLDED NAMES, a reflection of
// the access class or of a namespace that holds it, and a splice that
// designates a member of an object.
void on_reflection(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_reflection_data*>(gcc_data);
    if (data == nullptr) {
        return;
    }
    note_reflection_name(*data);
    if (!name_rules.has_rows) {
        return;
    }
    build_name_rules();
    if (name_rules.unchecked_row == nullptr) {
        return;
    }
    bool is_late = is_late_event();
    if (data->kind == PLUGIN_SPLICE) {
        if (is_object_member(data->entity)) {
            record_named(Kind::member_splice, name_rules.unchecked_row, kSpliceShape, data->location,
                         current_function_decl, is_late);
        }
        return;
    }
    std::string access = access_name_of(data->entity);
    if (!access.empty()) {
        record_named(Kind::access_reflection, name_rules.unchecked_row, access, data->location, current_function_decl,
                     is_late);
    }
}

// ── The walked return (THE WALKED RETURN) ───────────────────────────────

// The expression inside the wrappers that keep its value: a location wrapper,
// a conversion, an indirection, an address, a temporary and a cleanup point.
tree stripped_value(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        switch (TREE_CODE(node)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case INDIRECT_REF:
            case ADDR_EXPR:
            case CLEANUP_POINT_EXPR:
            case SAVE_EXPR:
            case PAREN_EXPR:
                node = TREE_OPERAND(node, 0);
                continue;
            case TARGET_EXPR:
                node = TARGET_EXPR_INITIAL(node);
                continue;
            case COMPOUND_EXPR:
                node = TREE_OPERAND(node, 1);
                continue;
            default:
                return node;
        }
    }
    return node;
}

// The name of the function that the callee CALLEE of a call names, and the
// object of a member call of a pattern, which a COMPONENT_REF holds.  A
// dependent callee gives its identifier.
std::string callee_name(tree callee, tree& object) {
    object = NULL_TREE;
    callee = stripped_value(callee);
    if (callee != NULL_TREE && TREE_CODE(callee) == COMPONENT_REF) {
        object = TREE_OPERAND(callee, 0);
        callee = TREE_OPERAND(callee, 1);
    }
    if (callee == NULL_TREE) {
        return {};
    }
    if (TREE_CODE(callee) == TEMPLATE_ID_EXPR) {
        callee = TREE_OPERAND(callee, 0);
    }
    if (TREE_CODE(callee) == IDENTIFIER_NODE) {
        return identifier_text(callee);
    }
    tree decl = named_decl(callee);
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL && DECL_TEMPLATE_RESULT(decl) != NULL_TREE) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    return decl != NULL_TREE && DECL_NAME(decl) != NULL_TREE ? identifier_text(DECL_NAME(decl)) : std::string{};
}

tree callee_decl(tree callee) {
    callee = stripped_value(callee);
    if (callee != NULL_TREE && TREE_CODE(callee) == COMPONENT_REF) {
        callee = TREE_OPERAND(callee, 1);
    }
    tree decl = callee != NULL_TREE ? named_decl(callee) : NULL_TREE;
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL && DECL_TEMPLATE_RESULT(decl) != NULL_TREE) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    return decl;
}

bool is_one_of(const std::string& name, const char* const* names, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (name == names[index]) {
            return true;
        }
    }
    return false;
}

// The values that the body of one function gives to each of its local
// variables: an initializer, an assignment, and the range of a range for or
// of an expansion statement.
using Bindings = std::unordered_map<tree, std::vector<tree>>;

struct Taint {
    Bindings bindings;
    std::unordered_set<tree> tainted;
};

bool is_walk_derived(tree node, const Taint& taint, int depth);

// True when CONTEXT, the access context of a member walk, is a call of
// access_context::current or unprivileged, or a local variable that only such
// a call sets.  A parameter and each other context fail closed.
bool is_checked_context(tree context, const Taint& taint, int depth) {
    context = stripped_value(context);
    if (context == NULL_TREE || depth > kTraceDepth) {
        return false;
    }
    if (TREE_CODE(context) == CALL_EXPR || TREE_CODE(context) == AGGR_INIT_EXPR) {
        tree decl = callee_decl(callee_of(context));
        if (decl == NULL_TREE || DECL_NAME(decl) == NULL_TREE
            || !is_one_of(identifier_text(DECL_NAME(decl)), kCheckedContexts, std::size(kCheckedContexts))) {
            return false;
        }
        tree scope = CP_DECL_CONTEXT(decl);
        return scope != NULL_TREE && TYPE_P(scope) && TYPE_MAIN_DECL(scope) != NULL_TREE
            && qualified_name(TYPE_MAIN_DECL(scope)) == name_rules.access_class;
    }
    if (!VAR_P(context)) {
        return false;
    }
    auto found = taint.bindings.find(context);
    if (found == taint.bindings.end() || found->second.empty()) {
        return false;
    }
    for (tree value : found->second) {
        if (!is_checked_context(value, taint, depth + 1)) {
            return false;
        }
    }
    return true;
}

// True when the call CALL walks the members or the bases of a class under a
// context that can see a private member.  A walk of a namespace gives no
// member of a class.
bool is_open_walk(tree call, const Taint& taint) {
    if (argument_count(call) == 0) {
        return true;
    }
    tree walked = stripped_value(argument_at(call, 0));
    if (walked != NULL_TREE && TREE_CODE(walked) == REFLECT_EXPR && REFLECT_EXPR_HANDLE(walked) != NULL_TREE
        && TREE_CODE(REFLECT_EXPR_HANDLE(walked)) == NAMESPACE_DECL) {
        return false;
    }
    return argument_count(call) < 2 || !is_checked_context(argument_at(call, 1), taint, 0);
}

// True when a reflection of a member of a class is ENTITY: a data member, a
// static data member, a member function, or a base.
bool is_class_member(tree entity) {
    if (entity == NULL_TREE) {
        return false;
    }
    if (TREE_CODE(entity) == TREE_BINFO || TREE_CODE(entity) == FIELD_DECL) {
        return true;
    }
    tree decl = named_decl(entity);
    return decl != NULL_TREE && DECL_CLASS_SCOPE_P(decl) && TREE_CODE(decl) != TYPE_DECL;
}

bool is_call_derived(tree call, const Taint& taint, int depth) {
    tree object = NULL_TREE;
    std::string name = callee_name(callee_of(call), object);
    if (name.empty()) {
        return false;
    }
    if (is_one_of(name, kMemberWalks, std::size(kMemberWalks))) {
        return is_open_walk(call, taint);
    }
    if (name == identifier_text(name_rules.define_static_array)) {
        for (int index = 0; index < argument_count(call); ++index) {
            if (is_walk_derived(argument_at(call, index), taint, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (!is_one_of(name, kElementAccessors, std::size(kElementAccessors))) {
        return false;
    }
    if (object == NULL_TREE && argument_count(call) > 0) {
        object = argument_at(call, 0);
    }
    return object != NULL_TREE && is_walk_derived(object, taint, depth + 1);
}

// True when NODE gives a member of a class that an open walk found: the
// result of an open walk, define_static_array over one, an element of one, a
// local variable that one of these sets, and a folded constant that holds a
// reflection of a member of a class (THE WALKED RETURN).
bool is_walk_derived(tree node, const Taint& taint, int depth) {
    node = stripped_value(node);
    if (node == NULL_TREE || depth > kTypeDepth) {
        return false;
    }
    switch (TREE_CODE(node)) {
        case ARRAY_REF:
            return is_walk_derived(TREE_OPERAND(node, 0), taint, depth + 1);
        case CALL_EXPR:
        case AGGR_INIT_EXPR:
            return is_call_derived(node, taint, depth);
        case REFLECT_EXPR:
            return is_class_member(REFLECT_EXPR_HANDLE(node));
        // A constant that the front end folded, such as the array of a walk
        // in a constexpr variable.  An aggregate that the code builds from a
        // variable or a call is no folded constant, and the rule does not
        // read inside it.
        case CONSTRUCTOR: {
            vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(node);
            for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
                tree element = stripped_value((*elements)[index].value);
                bool is_constant =
                    element != NULL_TREE && (TREE_CODE(element) == REFLECT_EXPR || TREE_CODE(element) == CONSTRUCTOR);
                if (is_constant && is_walk_derived(element, taint, depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        case VAR_DECL:
            if (taint.tainted.count(node) != 0) {
                return true;
            }
            return depth < kTraceDepth && DECL_INITIAL(node) != NULL_TREE && DECL_INITIAL(node) != error_mark_node
                && is_walk_derived(DECL_INITIAL(node), taint, depth + kTraceDepth / 2);
        default:
            return false;
    }
}

struct BodyScan {
    tree function = NULL_TREE;
    Taint* taint = nullptr;
    std::vector<tree>* returns = nullptr;
    bool uses_unchecked = false;
};

bool is_local_variable(tree decl, tree function) {
    return decl != NULL_TREE && VAR_P(decl) && !TREE_STATIC(decl) && DECL_CONTEXT(decl) == function;
}

// Collects the returns, the bindings of the local variables, and a dependent
// member named unchecked, of one function.  A lambda is a function of its own.
tree scan_body(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    BodyScan& scan = *static_cast<BodyScan*>(data);
    switch (TREE_CODE(node)) {
        case LAMBDA_EXPR:
            *walk_subtrees = 0;
            return NULL_TREE;
        case RETURN_EXPR: {
            tree value = TREE_OPERAND(node, 0);
            if (value != NULL_TREE && (TREE_CODE(value) == INIT_EXPR || TREE_CODE(value) == MODIFY_EXPR)) {
                value = TREE_OPERAND(value, 1);
            }
            if (value != NULL_TREE) {
                scan.returns->push_back(value);
            }
            return NULL_TREE;
        }
        case DECL_EXPR: {
            tree decl = DECL_EXPR_DECL(node);
            if (is_local_variable(decl, scan.function) && DECL_INITIAL(decl) != NULL_TREE) {
                scan.taint->bindings[decl].push_back(DECL_INITIAL(decl));
            }
            return NULL_TREE;
        }
        case INIT_EXPR:
        case MODIFY_EXPR: {
            tree target = stripped_value(TREE_OPERAND(node, 0));
            if (is_local_variable(target, scan.function)) {
                scan.taint->bindings[target].push_back(TREE_OPERAND(node, 1));
            }
            return NULL_TREE;
        }
        case RANGE_FOR_STMT:
            if (is_local_variable(RANGE_FOR_DECL(node), scan.function)) {
                scan.taint->bindings[RANGE_FOR_DECL(node)].push_back(RANGE_FOR_EXPR(node));
            }
            return NULL_TREE;
        case TEMPLATE_FOR_STMT:
            if (is_local_variable(TEMPLATE_FOR_DECL(node), scan.function)) {
                scan.taint->bindings[TEMPLATE_FOR_DECL(node)].push_back(TEMPLATE_FOR_EXPR(node));
            }
            return NULL_TREE;
        case COMPONENT_REF:
            if (TREE_OPERAND(node, 1) == name_rules.unchecked_name) {
                scan.uses_unchecked = true;
            }
            return NULL_TREE;
        default:
            return NULL_TREE;
    }
}

// True when FUNCTION, or a function that holds the lambda FUNCTION, uses the
// unchecked function by a mark of an event or of a walk.
bool is_marked_unchecked(tree function) {
    bool is_marked = false;
    for_each_enclosing(function,
                       [&is_marked](tree enclosing) { is_marked = is_marked || is_unchecked_user(enclosing); });
    return is_marked;
}

// A function that uses the unchecked function and returns a member of a class
// that a walk found: the kind walked-return at the function.  The analysis
// reads a function that an event marks (note_walk_marks).  A pattern can use
// unchecked through a member of a dependent object, which no event gives, so
// a pattern whose chain has a mark of a walk (check_named_node) is scanned,
// and the scan decides.  Complexity: O(nodes of the body) for the scan, and
// O(rounds * bindings) for the taint, kTraceDepth rounds at most.
void check_walked_return(tree function, bool is_pattern) {
    if (name_rules.unchecked_row == nullptr || function == NULL_TREE || DECL_SAVED_TREE(function) == NULL_TREE) {
        return;
    }
    bool is_unchecked = is_unchecked_user(function);
    if (!is_unchecked && !(is_pattern && is_marked_unchecked(function))) {
        return;
    }
    Place place = owned_place(DECL_SOURCE_LOCATION(function));
    if (place.file == nullptr || !counts_in_unit(place)) {
        return;
    }
    Taint taint;
    std::vector<tree> returns;
    BodyScan scan;
    scan.function = function;
    scan.taint = &taint;
    scan.returns = &returns;
    cp_walk_tree_without_duplicates(&DECL_SAVED_TREE(function), scan_body, &scan);
    if (!is_unchecked && !scan.uses_unchecked) {
        return;
    }
    for (int round = 0; round < kTraceDepth; ++round) {
        bool is_grown = false;
        for (const auto& [variable, values] : taint.bindings) {
            if (taint.tainted.count(variable) != 0) {
                continue;
            }
            for (tree value : values) {
                if (is_walk_derived(value, taint, 0)) {
                    taint.tainted.insert(variable);
                    is_grown = true;
                    break;
                }
            }
        }
        if (!is_grown) {
            break;
        }
    }
    for (tree value : returns) {
        if (is_walk_derived(value, taint, 0)) {
            tree holder = pattern_of(function);
            std::string entity = LAMBDA_FUNCTION_P(function)
                                   ? "lambda of " + name_holder_text(function, DECL_SOURCE_LOCATION(function), false)
                                   : holder_signature(holder);
            add_finding(Kind::walked_return, nullptr, place, entity + ": " + kWalkedShape);
            return;
        }
    }
}

// ── The declarations of the names (THE NAMES OF THE SOURCE) ─────────────

// The place of the declaration at LOCATION: its expansion point, so a
// declaration that a macro writes counts where the macro expands.
Place declaration_place(location_t location) {
    return owned_place(linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr));
}

// One finding of a declaration at LOCATION, held by HOLDER or by no function.
void record_declared_name(Kind kind, const OwnerRow* row, const std::string& shape, location_t location, tree holder) {
    Place place = owned_place(location);
    if (place.file == nullptr || !counts_in_unit(place) || (row != nullptr && is_at_site(*row, place, holder))) {
        return;
    }
    tree pattern = pattern_of(holder_of(holder));
    add_finding(kind, nullptr, place,
                (pattern != NULL_TREE ? holder_signature(pattern) : std::string(kNoHolder)) + ": " + shape);
}

// A friend of the class TYPE that is the head of a door rule, or that its
// namespace holds: `friend struct InitOwner;` names the door with no lookup
// that an event gives.  The front end keeps no place of a friend
// declaration, so the finding stands at the class.  A head that an event
// named while the parser read the class, as a qualified friend does, has its
// finding at the name, and the friend gives no second one.
void check_door_friends(tree type) {
    tree decl = TYPE_MAIN_DECL(type);
    for (tree entry = CLASSTYPE_FRIEND_CLASSES(type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree befriended = TREE_VALUE(entry);
        if (befriended != NULL_TREE && TREE_CODE(befriended) == TEMPLATE_DECL) {
            befriended = TREE_TYPE(befriended);
        }
        tree friend_decl = befriended != NULL_TREE && TYPE_P(befriended) ? TYPE_MAIN_DECL(befriended) : NULL_TREE;
        Held held = friend_decl != NULL_TREE ? held_by(friend_decl) : Held{};
        if (held.row != nullptr && is_door_rule(held.row->rule)
            && name_rules.class_heads.count(class_head_key(type, held.head)) == 0) {
            record_declared_name(held.row->kind, held.row, kFriendPrefix + shape_of(held), DECL_SOURCE_LOCATION(decl),
                                 NULL_TREE);
        }
    }
}

// A declaration of DECL at namespace scope inside a detail namespace of a row,
// in a file outside the sites of the row: code that a file outside the layer
// adds to a detail namespace of the layer.
void check_detail_declaration(tree decl) {
    if (!name_rules.has_detail_rows || decl == NULL_TREE || DECL_ARTIFICIAL(decl)) {
        return;
    }
    tree head = detail_head_of(CP_DECL_CONTEXT(decl));
    Held held = head != NULL_TREE ? held_by(head) : Held{};
    if (held.row != nullptr) {
        record_declared_name(Kind::detail_use, held.row, shape_of(held), DECL_SOURCE_LOCATION(decl), NULL_TREE);
    }
}

// True when the static data member DECL can change: it is not constexpr, and
// its type is not const.  A reference can change its referent when the
// referent is an object that is not const.
bool is_writable_member(tree decl) {
    if (DECL_DECLARED_CONSTEXPR_P(decl)) {
        return false;
    }
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE) {
        return true;
    }
    if (TYPE_REF_P(type)) {
        tree referent = TREE_TYPE(type);
        return referent != NULL_TREE && !FUNC_OR_METHOD_TYPE_P(referent) && !TYPE_READONLY(referent);
    }
    return !TYPE_READONLY(type);
}

// A writable private or protected static data member of a class of a file
// under kMemberRoots: the kind open-static-member, with no ledger row.  A
// splice of a static data member needs no object and no door token, so the
// rule refuses the member that a splice can write.
void check_open_static_member(tree decl) {
    if (name_rules.unchecked_row == nullptr || !VAR_P(decl) || !DECL_CLASS_SCOPE_P(decl)
        || !(TREE_PRIVATE(decl) || TREE_PROTECTED(decl)) || !is_writable_member(decl)) {
        return;
    }
    Place place = declaration_place(DECL_SOURCE_LOCATION(decl));
    if (place.file == nullptr || !counts_in_unit(place)) {
        return;
    }
    bool is_member_root = false;
    for (const char* root : kMemberRoots) {
        is_member_root = is_member_root || has_prefix(place.file->relative, root);
    }
    if (is_member_root) {
        add_finding(Kind::open_static_member, nullptr, place, qualified_name(decl));
    }
}

// The shapes of a pattern that no event gives: a member of a dependent object
// named unchecked, a dependent splice of a member or of its address, and a
// local alias or typedef of the access class.
void check_named_node(tree node, location_t location, tree function) {
    if (name_rules.unchecked_row == nullptr) {
        return;
    }
    switch (TREE_CODE(node)) {
        case COMPONENT_REF:
            if (TREE_OPERAND(node, 1) == name_rules.unchecked_name) {
                record_named(Kind::unchecked_use, name_rules.unchecked_row, name_rules.unchecked_row->entity, location,
                             function, false);
                mark_unchecked(function);
            }
            break;
        case SPLICE_EXPR:
            if (SPLICE_EXPR_MEMBER_ACCESS_P(node) || SPLICE_EXPR_ADDRESS_P(node)) {
                record_named(Kind::member_splice, name_rules.unchecked_row, kSpliceShape, location, function, false);
            }
            break;
        case DECL_EXPR: {
            tree decl = DECL_EXPR_DECL(node);
            if (decl != NULL_TREE && TREE_CODE(decl) == TYPE_DECL && DECL_ORIGINAL_TYPE(decl) != NULL_TREE) {
                std::string access = access_name_of(DECL_ORIGINAL_TYPE(decl));
                if (!access.empty()) {
                    record_named(Kind::access_alias, name_rules.unchecked_row, access, DECL_SOURCE_LOCATION(decl),
                                 function, false);
                }
            }
            break;
        }
        default:
            break;
    }
}

// Keeps each pattern, which the end of the unit reads, after the walks of the
// patterns marked their uses (THE WALKED RETURN).  check_walked_return reads
// the place of a pattern only when the pattern has a mark.
void on_finish_parse_function_names(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || processing_template_decl == 0) {
        return;
    }
    build_name_rules();
    if (name_rules.unchecked_row != nullptr) {
        name_rules.patterns.push_back(function);
    }
}

// THE WALKED RETURN of each function that is not a template, before the front
// end lowers its body.
void on_pre_genericize_names(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL && !is_in_instantiation(function)) {
        build_name_rules();
        check_walked_return(function, false);
    }
}

}  // namespace

// Each use of an entity of an owner row among the operands of NODE, and each
// call of a system call (THE OWNER ROWS, THE CLAIMS).  A list and a braced
// initializer hold operands too.  A unit with no owner row and no syscall row
// pays one test.  Complexity: O(operands of NODE).
void check_owned_node(tree node, location_t location, tree function) {
    if (node == NULL_TREE || !has_owner_rows()) {
        return;
    }
    build_owner_index();
    if (has_name_rows()) {
        build_name_rules();
        check_named_node(node, location, function);
    }
    if (TREE_CODE(node) == CALL_EXPR || TREE_CODE(node) == AGGR_INIT_EXPR) {
        check_owned_call(node, location, function);
    }
    if (EXPR_P(node)) {
        for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
            check_owned_operand(TREE_OPERAND(node, index), location, function);
        }
    } else if (TREE_CODE(node) == TREE_LIST) {
        check_owned_operand(TREE_VALUE(node), location, function);
    } else if (TREE_CODE(node) == CONSTRUCTOR) {
        vec<constructor_elt, va_gc>* elements = CONSTRUCTOR_ELTS(node);
        for (unsigned index = 0; index < vec_safe_length(elements); ++index) {
            check_owned_operand((*elements)[index].value, location, function);
        }
    }
}

// The declarations of THE NAMES OF THE SOURCE, from the walk of the family
// declaration property: a class at namespace scope in a detail namespace, the
// friends of a class that name a door, a variable or a static data member, a
// function, and an alias or a typedef.  Each one costs a test of a set when
// the table has no name rows.
void check_named_class(tree type) {
    if (!has_name_rows() || type == NULL_TREE || !CLASS_TYPE_P(type)) {
        return;
    }
    build_name_rules();
    check_detail_declaration(TYPE_MAIN_DECL(type));
    check_door_friends(TYPE_MAIN_VARIANT(type));
}

void check_named_variable(tree decl) {
    if (!has_name_rows() || decl == NULL_TREE) {
        return;
    }
    build_name_rules();
    check_detail_declaration(decl);
    check_open_static_member(decl);
}

void check_named_function(tree function) {
    if (!has_name_rows() || function == NULL_TREE || DECL_CLASS_SCOPE_P(function)) {
        return;
    }
    build_name_rules();
    check_detail_declaration(function);
}

void check_named_alias(tree decl) {
    if (!has_name_rows() || decl == NULL_TREE) {
        return;
    }
    build_name_rules();
    check_detail_declaration(decl);
    std::string access = DECL_ORIGINAL_TYPE(decl) != NULL_TREE ? access_name_of(DECL_ORIGINAL_TYPE(decl)) : "";
    if (!access.empty()) {
        record_declared_name(Kind::access_alias, name_rules.unchecked_row, access, DECL_SOURCE_LOCATION(decl),
                             decl_function_context(decl));
    }
}

// THE WALKED RETURN of each pattern of a counted file, after the walks of the
// patterns marked their uses.  A unit with no pattern pays two tests.
void walk_owner() {
    if (!has_owner_rows()) {
        return;
    }
    if (name_rules.has_unchecked_users) {
        // check_walked_return reads a pattern with a mark of its own or, for a
        // lambda, of a function that holds it (is_marked_unchecked).
        for (tree pattern : name_rules.patterns) {
            if (is_unchecked_user(pattern) || (LAMBDA_FUNCTION_P(pattern) && is_marked_unchecked(pattern))) {
                check_walked_return(pattern, true);
            }
        }
    }
}

// A library declaration, an overload set or a BASELINK that a walk reads at
// LOCATION: the walked names of THE FOLDED NAMES.
void note_walked_decl(tree named, location_t location) {
    tree functions = named != NULL_TREE && BASELINK_P(named) ? BASELINK_FUNCTIONS(named) : named;
    tree first = functions != NULL_TREE && TREE_CODE(functions) == OVERLOAD ? OVL_FIRST(functions) : functions;
    if (!is_library_decl(first) || !may_count_at(location)) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (!counts_folded_name(place)) {
        return;
    }
    for_each_held_name(
        first, [&place, location](tree identifier) { add_walked(place, location, identifier, Claim::nearest); });
}

// The library names that TYPE spells, at LOCATION: a declared type when
// IS_DECLARATION, and each other type that a walk reads.
void note_walked_type(tree type, location_t location, bool is_declaration) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return;
    }
    if (!may_count_at(location)) {
        return;
    }
    const std::vector<tree>& names = library_names_of_type(type, 0);
    if (names.empty()) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    for (tree identifier : names) {
        add_walked(place, location, identifier, is_declaration ? Claim::before : Claim::around);
    }
}

// The last name of a qualified name that a walk reads at LOCATION, when the
// name resolves to no declaration yet, as in a template.
void note_walked_identifier(tree identifier, location_t location) {
    if (identifier == NULL_TREE || TREE_CODE(identifier) != IDENTIFIER_NODE || !may_count_at(location)) {
        return;
    }
    add_walked(place_of(location, Scope::quarantine), location, identifier, Claim::nearest);
}

void judge_folded_names() { judge_folded(); }

// The front end calls the callback of a direct event with the facts of the
// event as arguments (plugin.h), so register_callback takes it cast to
// plugin_callback_func.  The parameter of type Direct makes a callback with a
// different signature a compile error.
template <typename Direct>
plugin_callback_func direct_callback(Direct callback) {
    return reinterpret_cast<plugin_callback_func>(reinterpret_cast<void (*)()>(callback));
}

// Registers the events of THE FOLDED NAMES, and the other events of THE NAMES
// OF THE SOURCE when the table has a row of a rule that reads them.  A unit
// with no such row pays no other event.
void register_owner(const char* plugin_name) {
    for (int code = 0; code < MAX_TREE_CODES; ++code) {
        first_test.is_decl_code[static_cast<std::size_t>(code)] =
            TREE_CODE_CLASS(static_cast<tree_code>(code)) == tcc_declaration ? 1 : 0;
    }
    register_callback(plugin_name, PLUGIN_NAME_USE, direct_callback<plugin_name_use_func>(on_name_use), nullptr);
    register_callback(plugin_name, PLUGIN_MEMBER_ACCESS, direct_callback<plugin_member_access_func>(on_member_access),
                      nullptr);
    register_callback(plugin_name, PLUGIN_USING_FORM, on_using_form, nullptr);
    register_callback(plugin_name, PLUGIN_REFLECTION, on_reflection, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl_names, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type_names, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function_folded, nullptr);
    for (const OwnerRow& row : state.table.owners) {
        name_rules.has_rows = name_rules.has_rows || reads_spelled_names(row.rule);
    }
    if (!name_rules.has_rows) {
        return;
    }
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function_names, nullptr);
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_names, nullptr);
}

}  // namespace crucible_plugin
