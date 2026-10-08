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
//     The rule table names the system calls:
//         syscall NAME... | REASON
//     A call of a function NAME of the C library, and a call of syscall(), is
//     the kind raw-syscall in a file under include/, src/ or vessel/, outside
//     each directory named test, bench, examples, third_party, external or
//     vendor, and each directory whose name starts with build.  These are the
//     claim trees.  The ledger of the rule admits each claim with its reason.
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
//     the name of the system call, or syscall(SYS_NAME) for a call of
//     syscall().
//
// THE WALKS
//     The walk of a body of expression.cpp gives each node of a quarantined
//     file and of a file of the language to check_owned_node, and each node of
//     the function of the dynamic initializers of the unit.  A base file gets
//     a walk of its own only in its sentinel unit (THE SENTINEL of plugin.h):
//     PLUGIN_PRE_GENERICIZE gives the body of each function that is not a
//     template, and walk_owner at PLUGIN_FINISH_UNIT reads each pattern, each
//     default argument, each initializer of a variable and each default member
//     initializer of the file, also a partial specialization of a template of
//     a different file.  That walk does not read a namespace that a library
//     file opens, such as std, so PLUGIN_FINISH_TYPE keeps each class of the
//     file that such a namespace holds, such as a specialization of
//     std::hash, and walk_owner reads it too.
//
// WHAT THE FAMILY DOES NOT READ
//     Code that no unit of a counted build compiles: a macro body that no unit
//     expands, a preprocessor arm that no build selects, and vessel/torch with
//     no TORCH_DIR (decision 5 of section 18.23 of
//     misc/01_10_2026_quarantine.md).  A name that the front end folds while
//     it parses, such as an operand of sizeof or a condition of a
//     static_assert outside a template, holds no call that runs.

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
    note_project_reference(scope, location);
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
    note_project_reference(scope, location);
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
    // The name NAME of each macro SYS_NAME, by its number.  The unit reads the
    // macros one time, at the first call of syscall().
    bool are_numbers_read = false;
    std::unordered_map<long long, std::string> numbers;
    // 1 when the unit has a sentinel base file, 0 when it has none, -1 before
    // the first test.
    int has_sentinel = -1;
};

OwnerIndex owner_index;

bool has_owner_rows() { return !state.table.owners.empty() || !state.table.syscalls.empty(); }

void build_owner_index() {
    if (owner_index.is_ready) {
        return;
    }
    owner_index.is_ready = true;
    for (const OwnerRow& row : state.table.owners) {
        std::size_t last = row.entity.rfind("::");
        std::string name = last == std::string::npos ? row.entity : row.entity.substr(last + 2);
        owner_index.last_names.insert(get_identifier(name.c_str()));
    }
    for (const std::string& name : state.table.syscalls) {
        owner_index.syscall_names.insert(get_identifier(name.c_str()));
    }
    owner_index.raw_syscall = get_identifier("syscall");
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
        if (row.entity != entity) {
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
            if ((last == std::string::npos ? row.entity : row.entity.substr(last + 2)) == IDENTIFIER_POINTER(name)) {
                check_owner_rows(row.entity, row.entity, location, function);
            }
        }
    }
    bool is_raw = false;
    std::string called;
    for_each_function(callee, [&](tree candidate) {
        tree candidate_name = DECL_NAME(candidate);
        if (candidate_name == NULL_TREE || library_of(candidate) != Library::c_library) {
            return;
        }
        if (candidate_name == owner_index.raw_syscall) {
            is_raw = true;
        } else if (called.empty() && owner_index.syscall_names.count(candidate_name) != 0) {
            called = IDENTIFIER_POINTER(candidate_name);
        }
    });
    if (!is_raw && called.empty()) {
        return;
    }
    std::string shape = called;
    if (is_raw) {
        std::string name;
        shape = raw_syscall_shape(call, name);
        if (!name.empty()) {
            check_owner_rows(name, shape, location, function);
        }
    }
    Place place = owned_place(location);
    if (place.file != nullptr && is_claim_file(place.file->relative)) {
        add_finding(Kind::raw_syscall, nullptr, place, holder_text(function, location) + ": " + shape);
    }
}

// ── The walk of a sentinel base file (THE WALKS) ────────────────────────

// The file that expands the macro of LOCATION, or the file of LOCATION, when
// it is a sentinel base file that is no file of the language: the main walk
// reads each file of the language.  Null for each other file.
const FileEntry* owner_base_file_at(location_t location) {
    location_t point = linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
    expanded_location where = expand_location(point);
    if (where.file == nullptr) {
        return nullptr;
    }
    const FileEntry& entry = classify_file(where.file);
    return entry.file_class == FileClass::base && entry.is_sentinel && entry.language_layer < 0 ? &entry : nullptr;
}

struct OwnerWalk {
    tree function = NULL_TREE;
    bool is_pattern = false;
    location_t last = UNKNOWN_LOCATION;
    hash_set<tree>* visited = nullptr;
};

tree visit_owner_node(tree* node_pointer, int* walk_subtrees, void* data);

void walk_owner_pattern(tree function);

// A local declaration of a pattern.  A template keeps the initializer of a
// local in DECL_INITIAL, and cp_walk_tree does not reach it outside template
// processing.  A member function of a local class of a template is a pattern
// that no PLUGIN_PRE_GENERICIZE gives.
void walk_owner_local(tree decl, OwnerWalk& walk) {
    if (decl == NULL_TREE || !walk.is_pattern) {
        return;
    }
    if (VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
        cp_walk_tree(&DECL_INITIAL(decl), visit_owner_node, &walk, walk.visited);
        return;
    }
    if (TREE_CODE(decl) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(decl) || !CLASS_TYPE_P(TREE_TYPE(decl))) {
        return;
    }
    for (tree member = TYPE_FIELDS(TREE_TYPE(decl)); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
        if (member_function != NULL_TREE && TREE_CODE(member_function) == FUNCTION_DECL) {
            walk_owner_pattern(member_function);
        }
    }
}

tree visit_owner_node(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    OwnerWalk& walk = *static_cast<OwnerWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        // A query that the front end folds gives a reflection of an entity
        // that the source does not spell, as the main walk reads it.
        case REFLECT_EXPR:
        case FUNCTION_DECL:
        case VAR_DECL:
        case TEMPLATE_DECL:
        case OVERLOAD:
        case BASELINK:
            *walk_subtrees = 0;
            return NULL_TREE;
        case DECL_EXPR:
            walk_owner_local(DECL_EXPR_DECL(node), walk);
            return NULL_TREE;
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                walk_owner_local(variable, walk);
            }
            return NULL_TREE;
        // A lambda of a function that is not a template has a body of its own,
        // which PLUGIN_PRE_GENERICIZE gives.  A lambda of a pattern does not.
        case LAMBDA_EXPR:
            if (walk.is_pattern) {
                cp_walk_tree(&LAMBDA_EXPR_CAPTURE_LIST(node), visit_owner_node, &walk, walk.visited);
                tree body_function = lambda_function(node);
                if (body_function != NULL_TREE && DECL_SAVED_TREE(body_function) != NULL_TREE) {
                    cp_walk_tree(&DECL_SAVED_TREE(body_function), visit_owner_node, &walk, walk.visited);
                }
            }
            *walk_subtrees = 0;
            return NULL_TREE;
        default:
            break;
    }
    check_owned_node(node, walk.last, walk.function);
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
    }
    return NULL_TREE;
}

// One walk of ROOT, the body of FUNCTION or an initializer when FUNCTION is
// null.  Complexity: O(nodes of ROOT).
void walk_owner_root(tree* root, tree function, location_t location, bool is_pattern) {
    if (root == nullptr || *root == NULL_TREE || *root == error_mark_node || TREE_CODE(*root) == DEFERRED_PARSE) {
        return;
    }
    hash_set<tree> visited;
    OwnerWalk walk;
    walk.function = function;
    walk.is_pattern = is_pattern;
    walk.last = location;
    walk.visited = &visited;
    cp_walk_tree(root, visit_owner_node, &walk, &visited);
}

// Each default argument of FUNCTION, which the code of a call copies.  A
// default argument lies in no body, so its uses have no holder (THE PLACE AND
// THE ENTITY).
void walk_owner_defaults(tree function, bool is_pattern) {
    for (tree type = TYPE_ARG_TYPES(TREE_TYPE(function)); type != NULL_TREE; type = TREE_CHAIN(type)) {
        if (TREE_PURPOSE(type) != NULL_TREE) {
            walk_owner_root(&TREE_PURPOSE(type), NULL_TREE, DECL_SOURCE_LOCATION(function), is_pattern);
        }
    }
}

// The pattern of the function template FUNCTION: its body and its defaults.
void walk_owner_pattern(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return;
    }
    walk_owner_defaults(function, true);
    if (DECL_SAVED_TREE(function) != NULL_TREE) {
        walk_owner_root(&DECL_SAVED_TREE(function), function, DECL_SOURCE_LOCATION(function), true);
    }
}

void on_pre_genericize_owner(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || !has_owner_rows()
        || DECL_SAVED_TREE(function) == NULL_TREE || (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function))
        || owner_base_file_at(DECL_SOURCE_LOCATION(function)) == nullptr || is_in_instantiation(function)) {
        return;
    }
    build_owner_index();
    walk_owner_root(&DECL_SAVED_TREE(function), function, DECL_SOURCE_LOCATION(function), false);
}

// The walk of the declarations of a sentinel base file at the end of a unit:
// each pattern, each default argument, each initializer and each default member
// initializer.  The body of a function that is not a template is gone at this
// point, and PLUGIN_PRE_GENERICIZE read it.
struct OwnerDeclarations {
    std::unordered_set<tree> visited;

    void variable(tree decl, bool is_pattern) {
        if (DECL_INITIAL(decl) != NULL_TREE) {
            walk_owner_root(&DECL_INITIAL(decl), NULL_TREE, DECL_SOURCE_LOCATION(decl), is_pattern);
        }
    }

    void class_type(tree type, bool is_pattern) {
        tree main_type = TYPE_MAIN_VARIANT(type);
        if (!CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type) || !visited.insert(main_type).second) {
            return;
        }
        for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
            switch (TREE_CODE(member)) {
                case FIELD_DECL:
                    if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                        class_type(TREE_TYPE(member), is_pattern);
                    } else if (!DECL_ARTIFICIAL(member)) {
                        variable(member, is_pattern);
                    }
                    break;
                case VAR_DECL:
                    variable(member, is_pattern);
                    break;
                case FUNCTION_DECL:
                    if (!DECL_ARTIFICIAL(member) || LAMBDA_FUNCTION_P(member)) {
                        if (is_pattern) {
                            walk_owner_pattern(member);
                        } else {
                            walk_owner_defaults(member, false);
                        }
                    }
                    break;
                case TYPE_DECL:
                    if (!DECL_SELF_REFERENCE_P(member) && DECL_IMPLICIT_TYPEDEF_P(member)
                        && CLASS_TYPE_P(TREE_TYPE(member))) {
                        class_type(TREE_TYPE(member), is_pattern);
                    }
                    break;
                case TEMPLATE_DECL:
                    template_decl(member);
                    break;
                default:
                    break;
            }
        }
        // A friend that a class template defines is no member of a namespace.
        if (is_pattern) {
            for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
                tree befriended = TREE_PURPOSE(entry) == NULL_TREE ? TREE_VALUE(entry) : NULL_TREE;
                if (befriended != NULL_TREE && TREE_CODE(befriended) == TEMPLATE_DECL) {
                    befriended = DECL_TEMPLATE_RESULT(befriended);
                }
                if (befriended != NULL_TREE && TREE_CODE(befriended) == FUNCTION_DECL) {
                    walk_owner_pattern(befriended);
                }
            }
        }
    }

    // A template of any file: a sentinel base file can hold a partial
    // specialization of a template of a different file.
    void template_decl(tree template_tree) {
        if (!visited.insert(template_tree).second) {
            return;
        }
        tree result = DECL_TEMPLATE_RESULT(template_tree);
        if (result == NULL_TREE) {
            return;
        }
        bool is_here = owner_base_file_at(DECL_SOURCE_LOCATION(result)) != nullptr;
        switch (TREE_CODE(result)) {
            case FUNCTION_DECL:
                if (is_here) {
                    walk_owner_pattern(result);
                }
                break;
            case VAR_DECL:
                if (is_here) {
                    variable(result, true);
                }
                break;
            case TYPE_DECL:
                if (DECL_IMPLICIT_TYPEDEF_P(result) && CLASS_TYPE_P(TREE_TYPE(result))) {
                    if (is_here) {
                        class_type(TREE_TYPE(result), true);
                    }
                    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_tree); entry != NULL_TREE;
                         entry = TREE_CHAIN(entry)) {
                        if (TREE_VALUE(entry) != NULL_TREE && TREE_CODE(TREE_VALUE(entry)) == TEMPLATE_DECL) {
                            template_decl(TREE_VALUE(entry));
                        }
                    }
                }
                break;
            default:
                break;
        }
    }

    void member(tree decl) {
        switch (TREE_CODE(decl)) {
            case NAMESPACE_DECL:
                if (DECL_NAMESPACE_ALIAS(decl) == NULL_TREE) {
                    namespace_decl(decl);
                }
                return;
            case TREE_LIST:
                if (TREE_VALUE(decl) != NULL_TREE && TREE_CODE(TREE_VALUE(decl)) != TREE_LIST) {
                    member(TREE_VALUE(decl));
                }
                return;
            case OVERLOAD:
                for (ovl_iterator candidate(decl, true); candidate; ++candidate) {
                    if (TREE_CODE(*candidate) != OVERLOAD) {
                        member(*candidate);
                    }
                }
                return;
            default:
                break;
        }
        if (!DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
            return;
        }
        if (TREE_CODE(decl) == TEMPLATE_DECL) {
            template_decl(decl);
            return;
        }
        if (owner_base_file_at(DECL_SOURCE_LOCATION(decl)) == nullptr) {
            return;
        }
        switch (TREE_CODE(decl)) {
            case TYPE_DECL:
                if (DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                    class_type(TREE_TYPE(decl), false);
                }
                break;
            case FUNCTION_DECL:
                walk_owner_defaults(decl, false);
                break;
            case VAR_DECL:
                variable(decl, false);
                break;
            default:
                break;
        }
    }

    // A namespace that a library file opens holds no declaration of a base
    // file.
    void namespace_decl(tree ns) {
        if (!visited.insert(ns).second || (ns != global_namespace && in_library_file_at(DECL_SOURCE_LOCATION(ns)))) {
            return;
        }
        for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
            member(decl);
        }
    }
};

// Each class of a sentinel base file that a namespace of a library file holds,
// and whether it is a pattern.  The walk of the declarations does not read such
// a namespace.  A specialization stays in the list of its template, so a
// collection does not free it.
std::vector<std::pair<tree, bool>> library_scope_classes;

// True when a namespace that a library file opens holds DECL.  Complexity:
// O(depth of the namespaces).
bool is_in_library_namespace(tree decl) {
    for (tree ns = decl_namespace_context(decl); ns != NULL_TREE && ns != global_namespace; ns = CP_DECL_CONTEXT(ns)) {
        if (in_library_file_at(DECL_SOURCE_LOCATION(ns))) {
            return true;
        }
    }
    return false;
}

void on_finish_type_owner(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !CLASS_TYPE_P(type) || !has_owner_rows()) {
        return;
    }
    tree decl = TYPE_MAIN_DECL(type);
    if (decl == NULL_TREE || decl_function_context(decl) != NULL_TREE
        || owner_base_file_at(DECL_SOURCE_LOCATION(decl)) == nullptr || !is_in_library_namespace(decl)) {
        return;
    }
    library_scope_classes.emplace_back(type, processing_template_decl != 0);
}

// True when the unit has a sentinel base file: the argument sentinel names a
// header, or the main file of the unit is a base file.
bool has_owner_sentinel() {
    if (owner_index.has_sentinel < 0) {
        bool is_base_main =
            main_input_filename != nullptr && classify_file(main_input_filename).file_class == FileClass::base;
        owner_index.has_sentinel = !state.sentinel.empty() || is_base_main ? 1 : 0;
    }
    return owner_index.has_sentinel == 1;
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

// The walk of the declarations of a sentinel base file (THE WALKS).  A unit
// with no sentinel base file pays one test.
void walk_owner() {
    if (!has_owner_rows() || !has_owner_sentinel()) {
        return;
    }
    build_owner_index();
    OwnerDeclarations declarations;
    declarations.namespace_decl(global_namespace);
    for (const auto& [type, is_pattern] : library_scope_classes) {
        declarations.class_type(type, is_pattern);
    }
}

// Registers the walk of each body of a sentinel base file, and the hook that
// keeps each class of such a file in a namespace of a library file.
void register_owner(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_owner, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type_owner, nullptr);
}

}  // namespace crucible_plugin
