// The family declaration property: the walk of the declarations.  The type of
// each variable, data member, parameter and return type, each base, alias and
// default template argument, each foreign symbol and each allocation function
// gets its finding where the code declares it.  One walk of every namespace
// reads the declarations at PLUGIN_FINISH_UNIT, and the types that an
// instantiation deduces get their finding at PLUGIN_PRE_GENERICIZE.
//
// THE FAMILY
//     The rules of the family read one property of a declaration, and each
//     rule is one predicate of this file.  Their kinds are in plugin.h (THE
//     RULES AND THE KINDS), and the module utils/assay/guard/engines/plugin/
//     RULE.py of each rule judges its findings against utils/ledgers/RULE.txt.
//
// THE SCOPE
//     A rule of the family reads a declaration of a quarantined file in each
//     unit that includes the file.  It reads a declaration of a base file only
//     in one unit: the unit that compiles the file, or the unit whose argument
//     sentinel names the file.  test/layer gives each header of the base one
//     such unit, so each base header costs one walk in each build.  The place
//     of a finding is the expansion point of the declaration
//     (property_place), so a declaration that a macro writes counts in the
//     file that expands the macro.  The rules no-raw-array-member and
//     escape-doors hold the base, and they read only a declaration of a base
//     file (base_place).  The module of a rule keeps the findings in the scope
//     of its row of utils/registry.txt.
//
// THE WALK
//     PropertyWalk reads each namespace that no library file opens, at
//     PLUGIN_FINISH_UNIT: each class, the pattern of each template and each
//     explicit specialization.  At that point the body of a function that is
//     not a template is gone, so PLUGIN_PRE_GENERICIZE keeps the facts of each
//     body that a rule reads, by DECL_UID.  A template pattern keeps its body.
//
// THE RULE mint-pattern (CLAUDE.md section XXI)
//     A mint is a function whose name starts with mint_ and does not end with
//     an underscore, which is not deleted.  A function that only a friend
//     declaration declares is no mint: its definition is the mint.  Each mint
//     gets one record line of the kind mint, and one finding for each axis
//     that it does not meet.  The entity of a record is the entity of the mint
//     and then its facts:
//
//       ENTITY | name=NAME static=0|1 shape=ctx|token|member template=0|1
//                nodiscard=0|1 constexpr=0|1 noexcept=0|1 constrained=0|1
//                gated=0|1 projection=0|1 defined=0|1 class=NAME|-
//                owner=CLASS|-
//
//     defined is 1 when the unit holds the body of the mint.  A declaration in
//     one header and the definition in a different header give two records,
//     and the module of the rule keeps the record with the body.  class is
//     the name of the class of a member with no namespace, and owner is its
//     qualified name.  The owner is the last fact, because the name of a class
//     can hold a space.  Each alias and each typedef of a class with a member
//     mint, also of a specialization of a class template, gets one record of
//     the kind mint-alias:
//
//       ALIAS | alias=NAME owner=CLASS
//
//     A fixture can name a member mint through such an alias.  The axes:
//       nodiscard     the attribute [[nodiscard]] on a declaration
//       constexpr     constexpr or consteval, unless the body is a borrow
//                     projection: one return that calls mint_view with this
//       noexcept      a noexcept specification that is not noexcept(false)
//       constraint    a mint that is a template has a constraint: a concept
//                     on a template parameter, or a requires-clause
//       context fit   a mint that takes a context first, `Ctx const&`, has a
//                     constraint that names that context.  The concept
//                     IsExecCtx on the parameter is its shape, and no gate
//     The shape of a mint is member for a member function that is not static,
//     ctx when its first parameter is a reference to a const context (a type
//     named Ctx, or a template parameter that IsExecCtx constrains), and token
//     otherwise.  The entity of each line is the qualified name of the mint
//     with the types of its parameters (function_signature).
//
// THE RULE no-raw-array-member (CLAUDE.md section II)
//     A data member of a C array type, and an alias or a typedef of a C array
//     type, in a base file.  A namespace whose name ends with _self_test holds
//     a probe type, and its declarations are none.  A static data member is
//     no part of the layout of the object, and it is none.  The entity is the
//     qualified name of the member or of the alias.
//
// THE RULE permission-storage (CLAUDE.md section IX)
//     A data member whose type names the class template Permission, also
//     through a template argument, with no [[no_unique_address]], and each
//     static data member whose type names it.  A pointer or a reference member
//     holds no token, and it is none.  The entity is the qualified name of
//     the member.
//
// THE RULE escape-doors (CLAUDE.md section XVI)
//     A public function of a base file that returns a reference or a raw
//     pointer, or can through decltype(auto), is a door.  An operator other
//     than a conversion and a getter of kAccessors are accessors, and a mint
//     is a door of its own rule.  A hatch of kHatches is the kind hatch, and
//     each other door is the kind bare-door.  A deleted function, a friend
//     with no body, a private or protected member and a name that ends with an
//     underscore are no door.  The entity is the function_signature of the
//     door.
//
// THE RULE stub-discipline (CLAUDE.md section XVIII)
//     An honesty marker is a constexpr bool with the value false whose name
//     ends with _implemented, _attached or _ready.  A stub deprecation is the
//     attribute deprecated with a reason that starts with CRUCIBLE_STUB:.  A
//     file with a marker and no deprecation gets one finding at its first
//     marker, and a file with a deprecation and no marker gets one finding at
//     its first deprecation.  The walk reads each declaration of a file, so
//     the pair holds in the file.
//
// THE RULE process-wide-state (CLAUDE.md section XV)
//     An object is a variable with static or thread storage duration that
//     can change: not constexpr, not const and no reference, and a definition.
//     The macro CRUCIBLE_PROCESS_WIDE gives the attribute
//     quarantine::process_wide, which this plugin registers.  The marker is on
//     the variable, or on the function that holds the object as a static
//     local.  An object with no marker is the kind unmarked-static.  A marked
//     object of a template is marker-in-template, one with internal linkage is
//     marker-internal-linkage, and one whose variable or function is not
//     inline is marker-not-inline.  A marked declaration that holds no object
//     is marker-without-object.  The rule reads the files of include/, src/
//     and vessel/, whose code goes into the libraries (kLibraryDirectories).
//     A test or a bench program holds one copy of each of its objects.  The
//     walk reads each variable at namespace scope and each static data
//     member, also of a template.  The static
//     locals of a function are in the blocks of its body, and
//     PLUGIN_FINISH_PARSE_FUNCTION reads them, for a template too.  The entity
//     is the qualified name of the variable, and FUNCTION()::NAME for a static
//     local.  The function of a static local of a lambda is the function that
//     holds the lambda.
//
// THE RULE lifetime-twin (CLAUDE.md section XVI)
//     The macro CRUCIBLE_LIFETIMEBOUND gives the attribute
//     quarantine::lifetimebound, which this plugin registers on a parameter.
//     Its handler notes the place of each marked parameter, and a marker on
//     each other declaration is an error.  A parameter at index I of a
//     function F with N parameters needs a deleted overload of F, in the
//     scope of F and in the file of the parameter, that takes N parameters or
//     ends with a pack, and that takes at index I an rvalue reference to the
//     type of the parameter, or to a template parameter.  A definition keeps
//     the attributes of an earlier declaration of its parameters, so only the
//     noted places count.  A declaration that is no template gives its
//     parameters at PLUGIN_FINISH_DECL.  The definition of a template gives
//     the parameters of an earlier declaration at PLUGIN_START_PARSE_FUNCTION,
//     before GCC merges the two.  A definition gives its own parameters at
//     PLUGIN_FINISH_PARSE_FUNCTION, and the walk gives the parameters of the
//     pattern of each template.  The entity is the function_signature of F
//     and the number of the parameter.
//
// THE RULE witness-roster (CLAUDE.md section XXI)
//     A class of a base file with a door shape attests to a fact that it
//     cannot see, and a witness row of the rule table names it.  A class with
//     a door shape and no witness row is the kind unrostered-witness.  A
//     strong shape makes a door:
//       - the class befriends a function whose name starts with mint_
//       - the name of the class ends with _key or Key, as a passkey does
//       - the first parameter of a constructor has a passkey type, which is
//         not the class: lowercase letters and underscores that end with
//         _key, or letters and underscores that end with Key
//       - a static member function returns expected<CLASS, ...>, also as a
//         template argument of its return type
//     A weak shape makes a door only with a closed constructor: a private or
//     protected constructor that the code declares, which is no copy, no move
//     and not deleted.  The weak shapes:
//       - the class befriends a named function that is no operator and no
//         swap
//       - the class befriends a type with a different name.  A template that
//         befriends its other specializations befriends its own name
//     The name of a type is the name that the code spells, through a pointer,
//     a reference and an alias.  A union is no door.  The walk of the family
//     reads each class, and PLUGIN_FINISH_TYPE gives each local class.  The
//     entity is the full name of the class with no template arguments, and
//     the function of a local class is no part of it.  A class in a
//     preprocessor arm that no build selects gives no finding (decision 5 of
//     misc/01_10_2026_quarantine.md).
//
// THE RULE padded-list (CLAUDE.md section XV, "Compile time", rule 7)
//     With -ftrivial-auto-var-init=zero, GCC writes one store for each
//     padding hole of each element of a fixed list in an automatic object,
//     with no loop.  A fixed list is an array of a class or a union, or such
//     an array that a specialization of a class template or a class with no
//     name holds, at each depth.  The array of std::array is one.  The count
//     of a list of lists is the product of the counts.  The value bits of a
//     type are the bit ranges of its scalar members, its named bit-fields, its
//     bases and its pointer to the virtual table.  The other bits are padding.
//     A floating-point type holds the bits of its precision, and the 80-bit
//     long double of x86-64 then has padding.  A union holds the union of the
//     bit ranges of its members.  When the members have more than kMostRanges
//     ranges, the union holds the value bits of its largest member, and the
//     rule then fails closed.  The rule reads the type of each alias,
//     variable and data member, and the return type and each parameter that a
//     function takes by value, in code that no template holds.  It reads the
//     files of include/ and src/ (kPaddedListDirectories).  For each element
//     type that has a padding bit, the longest list of two or more elements
//     in the declared type is a line of the kind padded-alias, padded-member,
//     padded-return or padded-parameter.  The module of the rule keeps one
//     finding for each declaration and element type, and the longest list
//     then decides it.  The entity is DECLARATION: ELEMENT, and then
//     " | count=N holes=H": the element count and the padding bits of one
//     element.  DECLARATION is the
//     qualified name of an alias, a variable or a member, the
//     function_signature of a function, and for a parameter the
//     function_signature and " parameter I", the index from 1.  The module of
//     the rule keeps the lists of more elements than the threshold of its row
//     of utils/registry.txt.  The rule reads a list only in a type that the
//     unit completes.  An alias of a specialization that the unit only names,
//     and a function that the unit only declares, give no line, because GCC
//     does not instantiate the class.  A unit with no object of the list
//     writes no store for it (decision 5 of misc/01_10_2026_quarantine.md).
//     A quarantined file gives its line in each unit that completes the
//     type.  A base file gives its line only in its sentinel unit (THE
//     SCOPE).

#include "plugin.h"

namespace crucible_plugin {

namespace {

void walk_template(tree template_decl);
void walk_namespace(tree ns);

// The type that an instantiation deduces for a declaration whose template
// spells a placeholder, such as `auto held = make(value);` with a dependent
// initializer.  The code holds an object of that type, as it holds one that a
// placeholder declares outside a template.  The entity names the class or the
// kind of the pointer, and not the template arguments of one instantiation,
// so each instantiation gives the same finding.  The walk of a quarantined
// file only calls it.
void check_deduced_object(tree decl, Role role) {
    tree type = role == Role::return_value ? TREE_TYPE(TREE_TYPE(decl)) : TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || type_uses_auto(type) != NULL_TREE) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    if (role != Role::return_value && TREE_CODE(type) == POINTER_TYPE) {
        bool is_function = FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type));
        record(is_function ? Kind::raw_function_pointer : Kind::raw_pointer_object, location,
               is_function ? "deduced function pointer" : "deduced pointer");
    } else if (role != Role::return_value && TYPE_PTRMEM_P(type)) {
        record(Kind::raw_function_pointer, location, "deduced member pointer");
    }
    LibraryClasses classes = library_class_in(type, 0, Use::other);
    if (classes.refused != NULL_TREE && !classes.is_refused_name) {
        bool is_c_library = library_of(TYPE_MAIN_DECL(classes.refused)) == Library::c_library;
        record(is_c_library ? Kind::c_library_object : Kind::std_object, location,
               qualified_name(classes.refused) + " (deduced)");
    }
    record_pending_class(classes, location);
}

bool has_lang_template_info(tree decl) {
    return DECL_LANG_SPECIFIC(decl) != nullptr && DECL_TEMPLATE_INFO(decl) != NULL_TREE;
}

// A declaration of an allocation function in a quarantined file, such as a
// replacement of the global operator new.  Each new-expression of the program
// can then call it, so it is a raw-new-delete.  A deleted allocation function
// allocates nothing.
void check_allocation_function(tree decl) {
    if (TREE_CODE(decl) != FUNCTION_DECL || DECL_ARTIFICIAL(decl)
        || !(DECL_IS_OPERATOR_NEW_P(decl) || DECL_IS_OPERATOR_DELETE_P(decl))
        || (DECL_LANG_SPECIFIC(decl) != nullptr && DECL_DELETED_FN(decl))) {
        return;
    }
    record(Kind::raw_new_delete, DECL_SOURCE_LOCATION(decl),
           std::string("declaration of ") + (DECL_IS_OPERATOR_NEW_P(decl) ? "operator new" : "operator delete"));
}

// DECL, a function definition or any declaration, when it defines a function
// or a variable of C language linkage.  PLUGIN_FINISH_DECL also sees a
// function that the code only declares, so a function counts only from
// PLUGIN_FINISH_PARSE_FUNCTION, which IS_FUNCTION_DEFINITION marks.
void note_c_definition(tree decl, bool is_function_definition) {
    if (decl == NULL_TREE) {
        return;
    }
    bool is_definition =
        is_function_definition ? TREE_CODE(decl) == FUNCTION_DECL : VAR_P(decl) && !DECL_EXTERNAL(decl);
    if (is_definition && has_c_linkage(decl)) {
        state.defined_c_symbols.insert(DECL_UID(decl));
    }
}

// Each definition of a function or a variable of C language linkage, for the
// foreign symbols.  PLUGIN_FINISH_DECL sees each variable definition, and
// PLUGIN_FINISH_PARSE_FUNCTION sees each function definition.
void on_finish_variable(void* gcc_data, void*) { note_c_definition(static_cast<tree>(gcc_data), false); }

void on_finish_function(void* gcc_data, void*) { note_c_definition(static_cast<tree>(gcc_data), true); }

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
        check_signature(function, false);
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
        const FileEntry* language = language_file_of(location);
        LibraryClasses classes = library_class_in_argument(default_argument, 0, language);
        if (classes.refused != NULL_TREE) {
            if (is_standard_class(classes.refused)) {
                record_declared(Kind::std_entity, location, qualified_name(classes.refused), language);
            }
        } else if (tree named =
                       TYPE_P(default_argument) ? library_type_name_in(default_argument, 0, language) : NULL_TREE) {
            record_standard_use(named, location, qualified_name(named), Use::other, NULL_TREE, language);
        }
        record_pending_class(classes, location);
        // The default of a non-type parameter is an expression, such as
        // &std::strlen, and the walk of a body reads it.
        if (!TYPE_P(default_argument) && TREE_CODE(default_argument) != TEMPLATE_DECL
            && (language != nullptr || is_quarantined(location))) {
            walk_root(&TREE_PURPOSE(parameter), location, true);
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
            walk_constraints(result);
            if (is_class_template) {
                walk_class(TREE_TYPE(result), true);
            } else {
                check_alias(result);
            }
            break;
        // walk_body reads the constraints of a function.
        case FUNCTION_DECL:
            walk_body(result, true);
            break;
        case VAR_DECL:
            walk_constraints(result);
            check_object(result, Role::variable);
            walk_initializer(result, true);
            break;
        // The definition of a concept is a constraint, an unevaluated operand.
        case CONCEPT_DECL:
            walk_root(&DECL_INITIAL(result), DECL_SOURCE_LOCATION(result), true);
            break;
        default:
            break;
    }
}

void walk_namespace_member(tree decl) {
    switch (TREE_CODE(decl)) {
        // A library namespace too: a file under the root can open std again.
        case NAMESPACE_DECL:
            if (DECL_NAMESPACE_ALIAS(decl) != NULL_TREE) {
                tree target = ORIGINAL_NAMESPACE(decl);
                if (target != NULL_TREE && top_namespace(target) == std_node) {
                    record(Kind::std_entity, DECL_SOURCE_LOCATION(decl), qualified_name(target));
                }
            } else {
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
    if (!DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return;
    }
    if (in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        // A file under the root can specialize a class template of the
        // library, such as std::hash, and the specialization is no member of
        // its namespace.
        tree result = TREE_CODE(decl) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(decl) : NULL_TREE;
        if (result != NULL_TREE && TREE_CODE(result) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(result)
            && CLASS_TYPE_P(TREE_TYPE(result))) {
            walk_specializations(decl);
        }
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
                check_foreign_declaration(decl);
                check_allocation_function(decl);
            }
            break;
        case VAR_DECL:
            if (!has_lang_template_info(decl) && !DECL_ARTIFICIAL(decl)) {
                check_object(decl, Role::variable);
                check_foreign_declaration(decl);
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

// ── The types that an instantiation deduces ─────────────────────────────

tree find_placeholder_local(tree* node_pointer, int*, void* data) {
    tree node = *node_pointer;
    if (TREE_CODE(node) == DECL_EXPR && VAR_P(DECL_EXPR_DECL(node))
        && type_uses_auto(TREE_TYPE(DECL_EXPR_DECL(node))) != NULL_TREE) {
        static_cast<std::unordered_set<location_t>*>(data)->insert(DECL_SOURCE_LOCATION(DECL_EXPR_DECL(node)));
    }
    return NULL_TREE;
}

// The locations of the local variables of the function pattern PATTERN whose
// declared type uses a placeholder.  A pattern keeps the placeholder when the
// initializer is dependent, and an instantiation gives its local the location
// of the local of the pattern.  The answer is kept for each pattern.
// Complexity: O(nodes of the body of the pattern), one time for each pattern.
std::unordered_set<location_t>& placeholder_locals_of(tree pattern) {
    auto cached = state.placeholder_locals.find(pattern);
    if (cached != state.placeholder_locals.end()) {
        return cached->second;
    }
    std::unordered_set<location_t>& locals = state.placeholder_locals[pattern];
    if (tree body = DECL_SAVED_TREE(pattern)) {
        cp_walk_tree_without_duplicates(&body, find_placeholder_local, &locals);
    }
    return locals;
}

tree check_deduced_local(tree* node_pointer, int*, void* data) {
    tree node = *node_pointer;
    tree decl = TREE_CODE(node) == DECL_EXPR ? DECL_EXPR_DECL(node) : NULL_TREE;
    const auto& placeholders = *static_cast<const std::unordered_set<location_t>*>(data);
    if (decl != NULL_TREE && VAR_P(decl) && placeholders.count(DECL_SOURCE_LOCATION(decl)) != 0
        && !(DECL_DECOMPOSITION_P(decl) && !DECL_DECOMP_IS_BASE(decl))) {
        check_deduced_object(decl, Role::variable);
    }
    return NULL_TREE;
}

}  // namespace

// The expression of a decltype that TYPE spells at its outer level, through
// pointers, references and arrays, as a template keeps it.  The expression
// is an unevaluated operand, and the walk reads its names as a pattern.
void walk_decltype(tree type, location_t location) {
    for (int depth = 0; type != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(type) != POINTER_TYPE && TREE_CODE(type) != REFERENCE_TYPE && TREE_CODE(type) != ARRAY_TYPE) {
            break;
        }
        type = TREE_TYPE(type);
    }
    if (type != NULL_TREE && TREE_CODE(type) == DECLTYPE_TYPE) {
        walk_root(&DECLTYPE_TYPE_EXPR(type), location, true);
    }
}

// The type of a declaration.  In a quarantined file, each rule of the type
// applies.  In a file of the language, only the rule of the library types
// applies: the base holds the raw pointers and the arrays of its families.
void check_object(tree decl, Role role) {
    tree type = role == Role::return_value ? TREE_TYPE(TREE_TYPE(decl)) : TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    const FileEntry* language = language_file_of(location);
    if (language == nullptr && !is_quarantined(location)) {
        return;
    }
    walk_decltype(type, location);
    if (language == nullptr && role != Role::return_value) {
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
    Use use = role == Role::parameter ? Use::parameter : Use::other;
    LibraryClasses classes = library_class_in(type, 0, use, language);
    if (tree found = classes.refused) {
        // A class of a function type is a name: std-entity for a class of the
        // standard library, and nothing for a struct of the C library, whose
        // objects get their findings where the code declares them.
        if (!classes.is_refused_name) {
            Kind kind =
                library_of(TYPE_MAIN_DECL(found)) == Library::c_library ? Kind::c_library_object : Kind::std_object;
            record_declared(kind, location, qualified_name(found) + " (" + type_text(type) + ")", language);
        } else if (is_standard_class(found)) {
            record_declared(Kind::std_entity, location, qualified_name(found), language);
        }
    } else if (tree named = library_type_name_in(type, 0, language)) {
        record_standard_use(named, location, qualified_name(named), use, NULL_TREE, language);
    }
    record_pending_class(classes, location);
}

void check_alias(tree decl) {
    check_route_alias(decl);
    tree aliased = DECL_ORIGINAL_TYPE(decl);
    location_t location = DECL_SOURCE_LOCATION(decl);
    const FileEntry* language = language_file_of(location);
    if (aliased == NULL_TREE || (language == nullptr && !is_quarantined(location))) {
        return;
    }
    walk_decltype(aliased, location);
    record_named_entities(aliased, location, language);
}

// True when DECL, or a function or class that holds it, comes from a template
// instantiation.
bool is_in_instantiation(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
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

// The declaration DECL of a foreign symbol in a quarantined file: an asm
// label, the attribute weakref, or C language linkage with no definition in
// the unit.  The walk of declarations runs after the last definition of the
// unit.  A variable in a register that an asm label names is inline-asm.
void check_foreign_declaration(tree decl) {
    location_t location = DECL_SOURCE_LOCATION(decl);
    if (!is_quarantined(location)) {
        return;
    }
    if (VAR_P(decl) && DECL_HARD_REGISTER(decl)) {
        record(Kind::inline_asm, location, "asm register " + simple_name(decl));
        return;
    }
    std::string binding = foreign_binding_of(decl);
    if (binding.empty() && has_c_linkage(decl) && state.defined_c_symbols.count(DECL_UID(decl)) == 0) {
        binding = "C linkage";
    }
    if (!binding.empty()) {
        record(Kind::foreign_symbol, location, qualified_name(decl) + " (" + binding + ")");
    }
}

// The return type, each parameter and each default argument of FUNCTION.  A
// default argument that a quarantined file or a file of the language spells is
// a use there, also when no call uses it.  A call copies a default with its
// own place, so the place of a finding stays the same.  IS_PATTERN is true for
// a template pattern.
void check_signature(tree function, bool is_pattern) {
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function)) {
        check_object(function, Role::return_value);
    }
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        if (!DECL_ARTIFICIAL(parameter)) {
            check_object(parameter, Role::parameter);
        }
    }
    location_t location = DECL_SOURCE_LOCATION(function);
    Place place = place_of(location, Scope::quarantine);
    if (place.file_class != FileClass::quarantined && language_of(place) == nullptr) {
        return;
    }
    for (tree type = TYPE_ARG_TYPES(TREE_TYPE(function)); type != NULL_TREE; type = TREE_CHAIN(type)) {
        if (TREE_PURPOSE(type) != NULL_TREE && TREE_CODE(TREE_PURPOSE(type)) != DEFERRED_PARSE) {
            walk_root(&TREE_PURPOSE(type), location, is_pattern);
        }
    }
}

// The constraints of the templated entity DECL: the requires-clause of its
// template head, with each constraint of a template parameter, and its
// trailing requires-clause.  A constraint is an unevaluated operand, and the
// walk reads its names as a pattern.
void walk_constraints(tree decl) {
    tree info = get_constraints(decl);
    if (info == NULL_TREE || TREE_CODE(info) != CONSTRAINT_INFO) {
        return;
    }
    walk_root(&CI_TEMPLATE_REQS(info), DECL_SOURCE_LOCATION(decl), true);
    walk_root(&CI_DECLARATOR_REQS(info), DECL_SOURCE_LOCATION(decl), true);
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
    const FileEntry* language = language_file_of(class_location);
    if (tree binfo = TYPE_BINFO(main_type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            tree base = BINFO_TYPE(BINFO_BASE_BINFO(binfo, index));
            LibraryClasses classes = library_class_in(base, 0, Use::other, language);
            if (is_standard_class(classes.refused)) {
                record_declared(Kind::std_entity, class_location, qualified_name(classes.refused), language);
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
                check_foreign_declaration(member);
                walk_initializer(member, is_pattern);
                break;
            case FUNCTION_DECL:
                check_member_function(member, is_pattern);
                check_foreign_declaration(member);
                check_allocation_function(member);
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
            case USING_DECL:
                check_using(member);
                break;
            default:
                break;
        }
    }
    // A friend that a class template defines is not a member, and the list of
    // a class template also holds each of its static_asserts.  The condition
    // of a static_assert is an unevaluated operand.
    if (is_pattern) {
        for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
            tree befriended = TREE_VALUE(entry);
            if (befriended != NULL_TREE && TREE_CODE(befriended) == STATIC_ASSERT) {
                walk_root(&STATIC_ASSERT_CONDITION(befriended), STATIC_ASSERT_SOURCE_LOCATION(befriended), true);
                continue;
            }
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

// The instantiation FUNCTION of a template of a quarantined file: its return
// type and each local variable whose declaration in the template spells a
// placeholder get the type that the instantiation deduces.  The walk of the
// pattern reads each other part, so the rest of the instantiation stays
// unread: a type that a template parameter gives is spelled where the code
// names the template.
void check_instantiation(tree function) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE
        || TREE_CODE(DECL_TI_TEMPLATE(function)) != TEMPLATE_DECL || !is_quarantined(DECL_SOURCE_LOCATION(function))) {
        return;
    }
    tree template_decl = template_for_substitution(function);
    tree pattern = template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
                     ? DECL_TEMPLATE_RESULT(template_decl)
                     : NULL_TREE;
    if (pattern == NULL_TREE || TREE_CODE(pattern) != FUNCTION_DECL) {
        return;
    }
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function)
        && type_uses_auto(TREE_TYPE(TREE_TYPE(pattern))) != NULL_TREE) {
        check_deduced_object(function, Role::return_value);
    }
    std::unordered_set<location_t>& placeholders = placeholder_locals_of(pattern);
    if (!placeholders.empty() && DECL_SAVED_TREE(function) != NULL_TREE) {
        tree body = DECL_SAVED_TREE(function);
        cp_walk_tree_without_duplicates(&body, check_deduced_local, &placeholders);
    }
}

// The qualified name of FUNCTION with the types of its parameters and its
// qualifiers, such as fixy::Holder::mint_view(const Carrier&) const.  The
// types are the spellings of GCC, so each unit gives one text for one
// function.  Complexity: O(parameters).
std::string function_signature(tree function) {
    std::string text = qualified_name(function) + '(';
    bool is_first = true;
    tree types = FUNCTION_FIRST_USER_PARMTYPE(function);
    for (; types != NULL_TREE && types != void_list_node; types = TREE_CHAIN(types)) {
        if (!is_first) {
            text += ", ";
        }
        text += type_text(TREE_VALUE(types));
        is_first = false;
    }
    if (types == NULL_TREE && !is_first) {
        text += ", ...";
    }
    text += ')';
    tree type = TREE_TYPE(function);
    if (TREE_CODE(type) == METHOD_TYPE) {
        int qualifiers = type_memfn_quals(type);
        if ((qualifiers & TYPE_QUAL_CONST) != 0) {
            text += " const";
        }
        if ((qualifiers & TYPE_QUAL_VOLATILE) != 0) {
            text += " volatile";
        }
    }
    if (FUNCTION_REF_QUALIFIED(type)) {
        text += FUNCTION_RVALUE_QUALIFIED(type) ? " &&" : " &";
    }
    return text;
}

namespace {

// ── The family: the scope and the place ─────────────────────────────────

// True when the family reads FILE in this unit (THE SCOPE).
bool is_property_file(const FileEntry* file) {
    return file != nullptr && (file->file_class == FileClass::quarantined || file->is_sentinel);
}

// The place of the declaration at LOCATION (THE SCOPE), or an empty place
// when the family does not read its file in this unit.
Place property_place(location_t location) {
    Place place;
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION) {
        return place;
    }
    location_t point = linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
    expanded_location where = expand_location(point);
    if (where.file == nullptr) {
        return place;
    }
    const FileEntry& entry = classify_file(where.file);
    if (!is_property_file(&entry)) {
        return place;
    }
    place.file_class = entry.file_class;
    place.file = &entry;
    place.line = where.line;
    place.column = where.column;
    place.spelling = location;
    return place;
}

// A finding of a rule of the family at PLACE, when the family reads its file.
void record_property(Kind kind, const Place& place, const std::string& entity) {
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// The place of a declaration of a base file at LOCATION, or an empty place
// (THE SCOPE): the rules no-raw-array-member and escape-doors hold the base.
Place base_place(location_t location) {
    Place place = property_place(location);
    return place.file_class == FileClass::base ? place : Place{};
}

// ── The facts of a body ─────────────────────────────────────────────────

// The name of the function that the callee of a call names, in GENERIC or in
// a template pattern, or an empty text.
std::string callee_name(tree callee) {
    for (int depth = 0; callee != NULL_TREE && depth < kTypeDepth; ++depth) {
        switch (TREE_CODE(callee)) {
            case ADDR_EXPR:
            case TEMPLATE_ID_EXPR:
            case COMPONENT_REF:
                callee = TREE_OPERAND(callee, TREE_CODE(callee) == COMPONENT_REF ? 1 : 0);
                continue;
            case BASELINK:
                callee = BASELINK_FUNCTIONS(callee);
                continue;
            case OVERLOAD:
                callee = OVL_FIRST(callee);
                continue;
            case IDENTIFIER_NODE:
                return identifier_text(callee);
            default:
                return DECL_P(callee) ? identifier_text(DECL_NAME(callee)) : std::string{};
        }
    }
    return {};
}

// True when ARGUMENT is the object of the function itself: this or *this, under
// conversions and address operations.  A member of the object, such as
// this->value, is no such argument.
bool is_own_object(tree argument) {
    for (int depth = 0; argument != NULL_TREE && depth < kTypeDepth; ++depth) {
        switch (TREE_CODE(argument)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case ADDR_EXPR:
            case INDIRECT_REF:
                argument = TREE_OPERAND(argument, 0);
                continue;
            case PARM_DECL:
                return is_this_parameter(argument);
            default:
                return false;
        }
    }
    return false;
}

tree find_view_of_this(tree* node_pointer, int*, void*) {
    tree node = *node_pointer;
    bool is_call = TREE_CODE(node) == CALL_EXPR || TREE_CODE(node) == AGGR_INIT_EXPR;
    tree callee = TREE_CODE(node) == CALL_EXPR      ? CALL_EXPR_FN(node)
                : TREE_CODE(node) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_FN(node)
                                                    : NULL_TREE;
    if (!is_call || callee_name(callee) != "mint_view") {
        return NULL_TREE;
    }
    // The arguments of a CALL_EXPR and of an AGGR_INIT_EXPR start at operand
    // 3, after the count, the callee and the chain or the slot.
    for (int index = 3; index < TREE_OPERAND_LENGTH(node); ++index) {
        if (is_own_object(TREE_OPERAND(node, index))) {
            return node;
        }
    }
    return NULL_TREE;
}

struct ReturnScan {
    int returns = 0;
    bool calls_view_of_this = false;
};

tree scan_return(tree* node_pointer, int*, void* data) {
    tree node = *node_pointer;
    if (TREE_CODE(node) != RETURN_EXPR) {
        return NULL_TREE;
    }
    auto& scan = *static_cast<ReturnScan*>(data);
    ++scan.returns;
    tree value = TREE_OPERAND(node, 0);
    if (value != NULL_TREE && cp_walk_tree_without_duplicates(&value, find_view_of_this, nullptr) != NULL_TREE) {
        scan.calls_view_of_this = true;
    }
    return NULL_TREE;
}

// True when BODY is a borrow projection: one return, and that return calls
// mint_view with the object of the function as an argument.  Such a mint hands
// out a view of its own object and can never be constant evaluated.
// Complexity: O(nodes of the body).
bool is_borrow_projection(tree body) {
    if (body == NULL_TREE) {
        return false;
    }
    ReturnScan scan;
    cp_walk_tree_without_duplicates(&body, scan_return, &scan);
    return scan.returns == 1 && scan.calls_view_of_this;
}

// ── The rule mint-pattern ───────────────────────────────────────────────

constexpr const char* kExecCtxConcept = "IsExecCtx";

// True when FUNCTION is a mint: a function named mint_ and more, with no
// trailing underscore, that is not deleted.  A function that only a friend
// declaration declares is no mint, because the definition elsewhere is the
// mint.
bool is_mint(tree function) {
    if (TREE_CODE(function) != FUNCTION_DECL || DECL_ARTIFICIAL(function) || DECL_CONSTRUCTOR_P(function)) {
        return false;
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr && (DECL_DELETED_FN(function) || DECL_UNIQUE_FRIEND_P(function))) {
        return false;
    }
    tree name = DECL_NAME(function);
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE) {
        return false;
    }
    // The walk asks each function of the unit, so the test reads the bytes of
    // the identifier and makes no string.
    const char* text = IDENTIFIER_POINTER(name);
    std::size_t length = IDENTIFIER_LENGTH(name);
    return length > 5 && std::memcmp(text, "mint_", 5) == 0 && text[length - 1] != '_';
}

// The template parameter lists of the function template whose pattern is
// FUNCTION, or NULL when FUNCTION has no template head of its own.
tree own_template_parameters(tree function) {
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return NULL_TREE;
    }
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (TREE_CODE(template_decl) != TEMPLATE_DECL || !PRIMARY_TEMPLATE_P(template_decl)
        || DECL_TEMPLATE_RESULT(template_decl) != function) {
        return NULL_TREE;
    }
    return DECL_TEMPLATE_PARMS(template_decl);
}

// True when the type of FUNCTION has a noexcept specification that is not
// noexcept(false).  A pattern can keep a dependent noexcept(expression), and
// that is a noexcept specification too.
bool has_noexcept(tree function) {
    tree spec = TYPE_RAISES_EXCEPTIONS(TREE_TYPE(function));
    if (spec == NULL_TREE || spec == noexcept_false_spec) {
        return false;
    }
    if (spec == noexcept_true_spec || spec == empty_except_spec || DEFERRED_NOEXCEPT_SPEC_P(spec)) {
        return true;
    }
    tree purpose = TREE_PURPOSE(spec);
    return purpose != NULL_TREE && purpose != boolean_false_node;
}

// The name of the concept of the constraint CONSTRAINT, such as IsExecCtx for
// the check IsExecCtx<Ctx>, or an empty text.
std::string concept_name(tree constraint) {
    if (constraint == NULL_TREE || TREE_CODE(constraint) != TEMPLATE_ID_EXPR) {
        return {};
    }
    tree target = TREE_OPERAND(constraint, 0);
    if (target != NULL_TREE && TREE_CODE(target) == OVERLOAD) {
        target = OVL_FIRST(target);
    }
    return target != NULL_TREE && DECL_P(target) ? identifier_text(DECL_NAME(target)) : std::string{};
}

// The entry of the innermost list PARAMETERS whose type is the template type
// parameter TYPE, or NULL.
tree parameter_entry(tree parameters, tree type) {
    if (parameters == NULL_TREE || type == NULL_TREE || TREE_CODE(type) != TEMPLATE_TYPE_PARM) {
        return NULL_TREE;
    }
    tree innermost = INNERMOST_TEMPLATE_PARMS(parameters);
    for (int index = 0; innermost != NULL_TREE && index < TREE_VEC_LENGTH(innermost); ++index) {
        tree entry = TREE_VEC_ELT(innermost, index);
        tree parameter = entry != NULL_TREE && entry != error_mark_node ? TREE_VALUE(entry) : NULL_TREE;
        if (parameter != NULL_TREE && TREE_CODE(parameter) == TYPE_DECL && TREE_TYPE(parameter) != NULL_TREE
            && TREE_CODE(TREE_TYPE(parameter)) == TEMPLATE_TYPE_PARM
            && TEMPLATE_TYPE_IDX(TREE_TYPE(parameter)) == TEMPLATE_TYPE_IDX(type)
            && TEMPLATE_TYPE_LEVEL(TREE_TYPE(parameter)) == TEMPLATE_TYPE_LEVEL(type)) {
            return entry;
        }
    }
    return NULL_TREE;
}

bool is_named(tree type, const char* name) {
    for (tree candidate : {type, TYPE_MAIN_VARIANT(type)}) {
        tree type_decl = TYPE_NAME(candidate);
        if (type_decl != NULL_TREE && TREE_CODE(type_decl) == TYPE_DECL
            && identifier_text(DECL_NAME(type_decl)) == name) {
            return true;
        }
    }
    return false;
}

// The context type of a mint whose first parameter is a reference to a const
// context, or NULL (THE RULE mint-pattern).  The type of the function gives the
// parameter, because a unit that compiles to code can free the parameters of
// a body before PLUGIN_FINISH_UNIT.
tree context_type(tree function, tree parameters) {
    tree first = FUNCTION_FIRST_USER_PARMTYPE(function);
    tree type = first != NULL_TREE && first != void_list_node ? TREE_VALUE(first) : NULL_TREE;
    if (type == NULL_TREE || TREE_CODE(type) != REFERENCE_TYPE || TYPE_REF_IS_RVALUE(type)
        || !CP_TYPE_CONST_P(TREE_TYPE(type))) {
        return NULL_TREE;
    }
    tree context = TYPE_MAIN_VARIANT(TREE_TYPE(type));
    tree entry = parameter_entry(parameters, context);
    bool is_shaped = entry != NULL_TREE && concept_name(TEMPLATE_PARM_CONSTRAINTS(entry)) == kExecCtxConcept;
    return is_shaped || is_named(TREE_TYPE(type), "Ctx") ? context : NULL_TREE;
}

tree find_type(tree* node_pointer, int*, void* data) {
    tree node = *node_pointer;
    tree wanted = static_cast<tree>(data);
    if (!TYPE_P(node)) {
        return NULL_TREE;
    }
    tree main_type = TYPE_MAIN_VARIANT(node);
    if (main_type == wanted) {
        return node;
    }
    bool is_same_parameter = TREE_CODE(main_type) == TEMPLATE_TYPE_PARM && TREE_CODE(wanted) == TEMPLATE_TYPE_PARM
                          && TEMPLATE_TYPE_IDX(main_type) == TEMPLATE_TYPE_IDX(wanted)
                          && TEMPLATE_TYPE_LEVEL(main_type) == TEMPLATE_TYPE_LEVEL(wanted);
    return is_same_parameter ? node : NULL_TREE;
}

bool mentions_type(tree expression, tree type) {
    return expression != NULL_TREE && cp_walk_tree_without_duplicates(&expression, find_type, type) != NULL_TREE;
}

// Each conjunct of CONSTRAINT that is no shorthand constraint of a template
// parameter.  GCC joins the shorthand constraints and the requires-clause of a
// template head with an && of no location, and each shorthand constraint
// stays the tree of its parameter.
void clause_conjuncts(tree constraint, const std::unordered_set<tree>& shorthands, std::vector<tree>& clauses) {
    if (constraint == NULL_TREE || shorthands.count(constraint) != 0) {
        return;
    }
    if (TREE_CODE(constraint) == TRUTH_ANDIF_EXPR && EXPR_LOCATION(constraint) == UNKNOWN_LOCATION) {
        clause_conjuncts(TREE_OPERAND(constraint, 0), shorthands, clauses);
        clause_conjuncts(TREE_OPERAND(constraint, 1), shorthands, clauses);
        return;
    }
    clauses.push_back(constraint);
}

// True when a constraint of the mint FUNCTION gates its CONTEXT: a concept
// other than IsExecCtx on the context parameter, or a requires-clause that
// names the context.
bool is_context_gated(tree function, tree parameters, tree context) {
    tree entry = parameter_entry(parameters, context);
    if (entry != NULL_TREE) {
        std::string constraint_concept = concept_name(TEMPLATE_PARM_CONSTRAINTS(entry));
        if (!constraint_concept.empty() && constraint_concept != kExecCtxConcept) {
            return true;
        }
    }
    std::vector<tree> clauses;
    if (parameters != NULL_TREE) {
        std::unordered_set<tree> shorthands;
        tree innermost = INNERMOST_TEMPLATE_PARMS(parameters);
        for (int index = 0; innermost != NULL_TREE && index < TREE_VEC_LENGTH(innermost); ++index) {
            tree item = TREE_VEC_ELT(innermost, index);
            if (item != NULL_TREE && item != error_mark_node && TEMPLATE_PARM_CONSTRAINTS(item) != NULL_TREE) {
                shorthands.insert(TEMPLATE_PARM_CONSTRAINTS(item));
            }
        }
        clause_conjuncts(TEMPLATE_PARMS_CONSTRAINTS(parameters), shorthands, clauses);
    }
    tree info = get_constraints(function);
    if (info != NULL_TREE && TREE_CODE(info) == CONSTRAINT_INFO) {
        clauses.push_back(CI_DECLARATOR_REQS(info));
    }
    for (tree clause : clauses) {
        if (mentions_type(clause, context)) {
            return true;
        }
    }
    return false;
}

// The record and the findings of the mint FUNCTION (THE RULE mint-pattern).
// IS_PATTERN is true for the pattern of a template, whose body the unit
// keeps.
void check_mint(tree function, bool is_pattern) {
    if (!is_mint(function)) {
        return;
    }
    Place place = property_place(DECL_SOURCE_LOCATION(function));
    if (place.file == nullptr) {
        return;
    }
    tree parameters = own_template_parameters(function);
    bool is_template = parameters != NULL_TREE;
    bool is_member = DECL_OBJECT_MEMBER_FUNCTION_P(function);
    tree context = is_member ? NULL_TREE : context_type(function, parameters);
    const char* shape = is_member ? "member" : context != NULL_TREE ? "ctx" : "token";
    bool is_nodiscard = lookup_attribute("nodiscard", DECL_ATTRIBUTES(function)) != NULL_TREE;
    bool is_constexpr = DECL_DECLARED_CONSTEXPR_P(function) || DECL_IMMEDIATE_FUNCTION_P(function);
    bool is_noexcept = has_noexcept(function);
    tree info = get_constraints(function);
    bool is_constrained = info != NULL_TREE && TREE_CODE(info) == CONSTRAINT_INFO
                       && (CI_TEMPLATE_REQS(info) != NULL_TREE || CI_DECLARATOR_REQS(info) != NULL_TREE);
    bool is_gated = context != NULL_TREE && is_context_gated(function, parameters, context);
    bool is_projection = is_pattern ? is_borrow_projection(DECL_SAVED_TREE(function))
                                    : state.projection_bodies.count(DECL_UID(function)) != 0;
    bool is_defined =
        is_pattern ? DECL_SAVED_TREE(function) != NULL_TREE : state.defined_functions.count(DECL_UID(function)) != 0;
    tree owner = DECL_CLASS_SCOPE_P(function) ? DECL_CONTEXT(function) : NULL_TREE;
    tree owner_decl = owner != NULL_TREE ? TYPE_NAME(TYPE_MAIN_VARIANT(owner)) : NULL_TREE;
    std::string entity = function_signature(function);
    auto flag = [](bool value) { return value ? "1" : "0"; };
    std::string facts = std::string(" | name=") + identifier_text(DECL_NAME(function)) + " static="
                      + flag(owner != NULL_TREE && !is_member) + " shape=" + shape + " template=" + flag(is_template)
                      + " nodiscard=" + flag(is_nodiscard) + " constexpr=" + flag(is_constexpr) + " noexcept="
                      + flag(is_noexcept) + " constrained=" + flag(is_constrained) + " gated=" + flag(is_gated)
                      + " projection=" + flag(is_projection) + " defined=" + flag(is_defined) + " class="
                      + (owner_decl != NULL_TREE && TREE_CODE(owner_decl) == TYPE_DECL ? simple_name(owner_decl) : "-")
                      + " owner=" + (owner != NULL_TREE ? qualified_name(owner) : std::string("-"));
    record_property(Kind::mint_record, place, entity + facts);
    if (!is_nodiscard) {
        record_property(Kind::mint_not_nodiscard, place, entity);
    }
    if (!is_constexpr && !is_projection) {
        record_property(Kind::mint_not_constexpr, place, entity);
    }
    if (!is_noexcept) {
        record_property(Kind::mint_not_noexcept, place, entity);
    }
    if (is_template && !is_constrained) {
        record_property(Kind::mint_unconstrained, place, entity);
    }
    if (context != NULL_TREE && !is_gated) {
        record_property(Kind::mint_unfit_context, place, entity);
    }
}

// The class that the alias or the typedef DECL names, when a member function
// of the class is a mint, or NULL (THE RULE mint-pattern).  The class of a
// specialization of a class template is the class of the template, which
// holds the member declarations.  MINT_CLASSES keeps the answer of each class.
tree aliased_mint_class(tree decl, std::unordered_map<tree, bool>& mint_classes) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || !CLASS_TYPE_P(type)) {
        return NULL_TREE;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree info = CLASSTYPE_TEMPLATE_INFO(main_type);
    if (info != NULL_TREE && TI_TEMPLATE(info) != NULL_TREE && TREE_CODE(TI_TEMPLATE(info)) == TEMPLATE_DECL) {
        tree pattern = TREE_TYPE(DECL_TEMPLATE_RESULT(TI_TEMPLATE(info)));
        if (pattern != NULL_TREE && CLASS_TYPE_P(pattern)) {
            main_type = TYPE_MAIN_VARIANT(pattern);
        }
    }
    auto [entry, is_new] = mint_classes.try_emplace(main_type, false);
    if (is_new) {
        for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
            tree function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
            if (function != NULL_TREE && is_mint(function)) {
                entry->second = true;
                break;
            }
        }
    }
    return entry->second ? main_type : NULL_TREE;
}

// The record of the alias or the typedef DECL of a class with a member mint
// (THE RULE mint-pattern).  A fixture can name the mint through the alias.
void check_mint_alias(tree decl, std::unordered_map<tree, bool>& mint_classes) {
    tree owner = aliased_mint_class(decl, mint_classes);
    if (owner == NULL_TREE) {
        return;
    }
    Place place = property_place(DECL_SOURCE_LOCATION(decl));
    if (place.file == nullptr) {
        return;
    }
    record_property(Kind::mint_alias, place,
                    qualified_name(decl) + " | alias=" + simple_name(decl) + " owner=" + qualified_name(owner));
}

// ── The rule no-raw-array-member ────────────────────────────────────────

// True when a namespace that holds DECL has a name that ends with _self_test:
// the probe types of a header self-test use a C array on purpose.
bool is_in_probe_namespace(tree decl) {
    constexpr const char kProbeSuffix[] = "_self_test";
    constexpr std::size_t kProbeLength = sizeof(kProbeSuffix) - 1;
    tree current = CP_DECL_CONTEXT(decl);
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL) {
            tree name = DECL_NAME(current);
            if (name != NULL_TREE && IDENTIFIER_LENGTH(name) >= kProbeLength
                && std::memcmp(IDENTIFIER_POINTER(name) + IDENTIFIER_LENGTH(name) - kProbeLength, kProbeSuffix,
                               kProbeLength)
                       == 0) {
                return true;
            }
            current = CP_DECL_CONTEXT(current);
        } else if (TYPE_P(current)) {
            current = CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current));
        } else if (DECL_P(current)) {
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    return false;
}

// A data member or an alias of a C array type (THE RULE no-raw-array-member).
void check_array_declaration(tree decl, Kind kind) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || TREE_CODE(type) != ARRAY_TYPE) {
        return;
    }
    Place place = base_place(DECL_SOURCE_LOCATION(decl));
    if (place.file != nullptr && !is_in_probe_namespace(decl)) {
        record_property(kind, place, qualified_name(decl));
    }
}

// ── The rule permission-storage ─────────────────────────────────────────

// True when TYPE names a class template named Permission, also through a
// template argument, a pointer, a reference or an array in an argument.
bool names_permission(tree type, int depth) {
    if (type == NULL_TREE || depth > kTypeDepth) {
        return false;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
        case ARRAY_TYPE:
            return names_permission(TREE_TYPE(type), depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE:
            break;
        default:
            return false;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree info = CLASS_TYPE_P(main_type) ? CLASSTYPE_TEMPLATE_INFO(main_type) : NULL_TREE;
    if (info == NULL_TREE || TI_TEMPLATE(info) == NULL_TREE || !DECL_P(TI_TEMPLATE(info))) {
        return false;
    }
    if (identifier_text(DECL_NAME(TI_TEMPLATE(info))) == "Permission") {
        return true;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(info));
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument) && names_permission(argument, depth + 1)) {
            return true;
        }
    }
    return false;
}

// A member whose type names a Permission (THE RULE permission-storage).  A
// pointer or a reference member holds no token of its own.  A data member
// with [[no_unique_address]] is the handle shape, and a static member never
// is.
void check_permission_member(tree decl) {
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == REFERENCE_TYPE
        || !names_permission(type, 0)) {
        return;
    }
    bool is_static = VAR_P(decl);
    if (!is_static && lookup_attribute("no_unique_address", DECL_ATTRIBUTES(decl)) != NULL_TREE) {
        return;
    }
    record_property(is_static ? Kind::static_permission : Kind::permission_member,
                    property_place(DECL_SOURCE_LOCATION(decl)), qualified_name(decl));
}

// ── The rule escape-doors ───────────────────────────────────────────────

// The names of a getter whose provenance is the object that it reads.
constexpr const char* kAccessors[] = {
    "data",      "data_mut", "begin",   "end",      "cbegin",      "cend",
    "front",     "back",     "at",      "get",      "get_or_init", "get_assuming_set",
    "peek",      "peek_mut", "value",   "resource", "ctx",         "in",
    "out",       "input",    "output",  "stage",    "machine",     "state",
    "state_mut", "current",  "carrier", "handle",   "pin",         "c_str",
    "sq_ring",   "cq_ring",  "sqes",    "observe",  "consume",     "try_get",
    "raw_ptr",   "graded",   "cap",     "span",     "cspan",
};

// The explicit doors of the C boundary and of the hand-off of ownership.
constexpr const char* kHatches[] = {"from_raw", "from_raw_nonnull", "into_raw", "release", "declassify"};

bool is_one_of(const std::string& name, const char* const* names, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (name == names[index]) {
            return true;
        }
    }
    return false;
}

// True when FUNCTION returns a reference or a raw pointer, or can through
// decltype(auto).
bool returns_raw(tree function) {
    tree type = TREE_TYPE(TREE_TYPE(function));
    if (type == NULL_TREE) {
        return false;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == POINTER_TYPE) {
        return true;
    }
    if (is_auto(type) && AUTO_IS_DECLTYPE(type)) {
        return true;
    }
    tree saved = DECL_LANG_SPECIFIC(function) != nullptr && FNDECL_USED_AUTO(function)
                   ? DECL_SAVED_AUTO_RETURN_TYPE(function)
                   : NULL_TREE;
    return saved != NULL_TREE && is_auto(saved) && AUTO_IS_DECLTYPE(saved);
}

// A public function that hands out a raw reference or pointer (THE RULE
// escape-doors).  A private or protected member, a deleted function, a
// friend with no body and a name that ends with an underscore are no door.
// An operator other than a conversion is an accessor, and so is a getter
// whose name kAccessors holds.  A mint is a door of its own rule.
void check_escape(tree function, bool is_defined) {
    if (DECL_ARTIFICIAL(function) || DECL_CONSTRUCTOR_P(function) || DECL_DESTRUCTOR_P(function)
        || !returns_raw(function) || TREE_PRIVATE(function) || TREE_PROTECTED(function)) {
        return;
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr
        && (DECL_DELETED_FN(function) || (DECL_UNIQUE_FRIEND_P(function) && !is_defined))) {
        return;
    }
    Place place = base_place(DECL_SOURCE_LOCATION(function));
    if (place.file == nullptr) {
        return;
    }
    Kind kind = Kind::bare_door;
    std::string entity;
    if (DECL_CONV_FN_P(function)) {
        tree owner = DECL_CONTEXT(function);
        entity = (owner != NULL_TREE ? qualified_name(owner) + "::" : std::string{}) + "operator "
               + type_text(TREE_TYPE(TREE_TYPE(function))) + "()";
    } else {
        if (DECL_OVERLOADED_OPERATOR_P(function) || is_mint(function)) {
            return;
        }
        std::string name = identifier_text(DECL_NAME(function));
        if (name.empty() || name.back() == '_'
            || is_one_of(name, kAccessors, sizeof(kAccessors) / sizeof(kAccessors[0]))) {
            return;
        }
        if (is_one_of(name, kHatches, sizeof(kHatches) / sizeof(kHatches[0]))) {
            kind = Kind::hatch;
        }
        entity = function_signature(function);
    }
    record_property(kind, place, entity);
}

// ── The rule witness-roster ─────────────────────────────────────────────

bool has_suffix(const std::string& text, const char* suffix) {
    std::size_t length = std::strlen(suffix);
    return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
}

// True when each character of TEXT before END is a letter or an underscore,
// and a lowercase letter when IS_LOWERCASE_ONLY is true.
bool has_only_name_letters(const std::string& text, std::size_t end, bool is_lowercase_only) {
    for (std::size_t index = 0; index < end; ++index) {
        char letter = text[index];
        bool is_lowercase = letter >= 'a' && letter <= 'z';
        bool is_uppercase = letter >= 'A' && letter <= 'Z';
        if (!(letter == '_' || is_lowercase || (is_uppercase && !is_lowercase_only))) {
            return false;
        }
    }
    return true;
}

// True when NAME is a passkey type (THE RULE witness-roster): lowercase
// letters and underscores that end with _key, or letters and underscores that
// end with Key.
bool is_passkey_type_name(const std::string& name) {
    if (has_suffix(name, "_key")) {
        return has_only_name_letters(name, name.size() - 4, true);
    }
    return has_suffix(name, "Key") && has_only_name_letters(name, name.size() - 3, false);
}

// The name that the code spells for TYPE, through each pointer, reference and
// array, with no template arguments, or an empty text.  An alias keeps its own
// name, as the source spells it.
std::string spelled_type_name(tree type) {
    for (int depth = 0; type != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == REFERENCE_TYPE || TREE_CODE(type) == ARRAY_TYPE) {
            type = TREE_TYPE(type);
            continue;
        }
        if (TREE_CODE(type) == TEMPLATE_DECL) {
            return identifier_text(DECL_NAME(type));
        }
        if (!TYPE_P(type)) {
            return {};
        }
        tree name = TYPE_NAME(type);
        if (name != NULL_TREE && TREE_CODE(name) == TYPE_DECL) {
            name = DECL_NAME(name);
        }
        return name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE ? identifier_text(name) : std::string{};
    }
    return {};
}

// True when TYPE holds expected<OWNER, ...>: the type itself, the type that a
// pointer, a reference or an array holds, or a template argument of a class.
// The first argument of expected names OWNER, or a class with its NAME, and it
// is no pointer and no reference.  Complexity: O(nodes of the type).
bool holds_expected_of(tree type, tree owner, const std::string& name, int depth) {
    if (type == NULL_TREE || depth > kTypeDepth) {
        return false;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
        case ARRAY_TYPE:
            return holds_expected_of(TREE_TYPE(type), owner, name, depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE:
            break;
        default:
            return false;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree info = CLASS_TYPE_P(main_type) ? CLASSTYPE_TEMPLATE_INFO(main_type) : NULL_TREE;
    if (info == NULL_TREE || TI_TEMPLATE(info) == NULL_TREE || !DECL_P(TI_TEMPLATE(info))) {
        return false;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(info));
    int count = arguments != NULL_TREE ? TREE_VEC_LENGTH(arguments) : 0;
    if (count > 0 && identifier_text(DECL_NAME(TI_TEMPLATE(info))) == "expected") {
        tree first = TREE_VEC_ELT(arguments, 0);
        if (first != NULL_TREE && TYPE_P(first) && !INDIRECT_TYPE_P(first)
            && (TYPE_MAIN_VARIANT(first) == owner || spelled_type_name(first) == name)) {
            return true;
        }
    }
    for (int index = 0; index < count; ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument) && holds_expected_of(argument, owner, name, depth + 1)) {
            return true;
        }
    }
    return false;
}

// The door shapes of one class (THE RULE witness-roster).
struct DoorShapes {
    bool is_strong = false;
    bool is_weak = false;
    bool has_closed_constructor = false;
};

// The shapes of one constructor that the code declares.  ACCESS_HOLDER is the
// declaration that holds the access of the constructor: the template of a
// constructor template, and else the constructor.
void read_constructor(tree function, tree access_holder, const std::string& name, DoorShapes& shapes) {
    if (DECL_ARTIFICIAL(function) || DECL_CLONED_FUNCTION_P(function)) {
        return;
    }
    // The walk runs at PLUGIN_FINISH_UNIT, after GCC drops the parameters of
    // a defined function, so the rule reads the types of the function type.
    tree types = TYPE_ARG_TYPES(TREE_TYPE(function));
    tree parameters = types != NULL_TREE ? FUNCTION_FIRST_USER_PARMTYPE(function) : NULL_TREE;
    tree parameter_type = parameters != NULL_TREE && parameters != void_list_node ? TREE_VALUE(parameters) : NULL_TREE;
    std::string parameter_name = spelled_type_name(parameter_type);
    if (parameter_name != name && is_passkey_type_name(parameter_name)) {
        shapes.is_strong = true;
    }
    bool is_copy = parameter_type != NULL_TREE && TREE_CODE(parameter_type) == REFERENCE_TYPE && parameter_name == name;
    bool is_deleted = DECL_LANG_SPECIFIC(function) != nullptr && DECL_DELETED_FN(function);
    bool is_closed = TREE_PRIVATE(access_holder) || TREE_PROTECTED(access_holder);
    if (is_closed && !is_copy && !is_deleted) {
        shapes.has_closed_constructor = true;
    }
}

// The shapes of the members and the friends of the class MAIN_TYPE, whose
// name is NAME.  Complexity: O(members + friends).
DoorShapes door_shapes(tree main_type, const std::string& name) {
    DoorShapes shapes;
    shapes.is_strong = has_suffix(name, "_key") || has_suffix(name, "Key");
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
            continue;
        }
        if (DECL_CONSTRUCTOR_P(function)) {
            read_constructor(function, member, name, shapes);
        } else if (DECL_STATIC_FUNCTION_P(function) && !DECL_ARTIFICIAL(function)
                   && holds_expected_of(TREE_TYPE(TREE_TYPE(function)), main_type, name, 0)) {
            shapes.is_strong = true;
        }
    }
    for (tree entry = DECL_FRIENDLIST(TYPE_MAIN_DECL(main_type)); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree friend_name = FRIEND_NAME(entry);
        if (friend_name == NULL_TREE || TREE_CODE(friend_name) != IDENTIFIER_NODE) {
            continue;
        }
        std::string text = identifier_text(friend_name);
        if (has_prefix(text, "mint_")) {
            shapes.is_strong = true;
        } else if (!IDENTIFIER_ANY_OP_P(friend_name) && text != "swap" && !has_prefix(text, "operator")) {
            shapes.is_weak = true;
        }
    }
    for (tree entry = CLASSTYPE_FRIEND_CLASSES(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        if (TREE_VALUE(entry) != NULL_TREE && spelled_type_name(TREE_VALUE(entry)) != name) {
            shapes.is_weak = true;
        }
    }
    return shapes;
}

// The full name of the class TYPE with no template arguments: its namespaces
// and its enclosing classes.  An anonymous namespace and the function of a
// local class are no part of it.
std::string witness_class_name(tree type) {
    std::vector<std::string> parts;
    tree current = type;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        if (TYPE_P(current)) {
            tree main_type = TYPE_MAIN_VARIANT(current);
            tree decl = TYPE_NAME(main_type);
            if (decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL) {
                break;
            }
            parts.push_back(identifier_text(DECL_NAME(decl)));
            current = CP_TYPE_CONTEXT(main_type);
        } else if (TREE_CODE(current) == NAMESPACE_DECL) {
            if (DECL_NAME(current) != NULL_TREE) {
                parts.push_back(identifier_text(DECL_NAME(current)));
            }
            current = CP_DECL_CONTEXT(current);
        } else if (DECL_P(current)) {
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    std::string joined;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        joined += joined.empty() ? *part : "::" + *part;
    }
    return joined;
}

// The name of each witness row of the rule table.
const std::unordered_set<std::string>& witness_names() {
    static const std::unordered_set<std::string> names = [] {
        std::unordered_set<std::string> found;
        for (const WitnessRow& row : state.table.witnesses) {
            found.insert(row.name);
        }
        return found;
    }();
    return names;
}

// A class of a base file with a door shape and no witness row (THE RULE
// witness-roster).
void check_witness(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree decl = TYPE_MAIN_DECL(main_type);
    bool is_named_class = TREE_CODE(main_type) == RECORD_TYPE && CLASS_TYPE_P(main_type) && !LAMBDA_TYPE_P(main_type)
                       && decl != NULL_TREE && DECL_NAME(decl) != NULL_TREE && !IDENTIFIER_ANON_P(DECL_NAME(decl));
    if (!is_named_class || !COMPLETE_TYPE_P(main_type)) {
        return;
    }
    Place place = base_place(DECL_SOURCE_LOCATION(decl));
    if (place.file == nullptr) {
        return;
    }
    DoorShapes shapes = door_shapes(main_type, identifier_text(DECL_NAME(decl)));
    if (!shapes.is_strong && !(shapes.is_weak && shapes.has_closed_constructor)) {
        return;
    }
    std::string name = witness_class_name(main_type);
    if (witness_names().count(name) == 0) {
        record_property(Kind::unrostered_witness, place, name);
    }
}

// PLUGIN_FINISH_TYPE: a local class, which no namespace walk reaches.
void on_finish_witness_type(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type != NULL_TREE && type != error_mark_node && CLASS_TYPE_P(type) && LOCAL_CLASS_P(type)) {
        check_witness(type);
    }
}

// ── The rule stub-discipline ────────────────────────────────────────────

constexpr const char kStubPrefix[] = "CRUCIBLE_STUB:";

// True when DECL is an honesty marker: a constexpr bool whose name ends with
// _implemented, _attached or _ready, with the value false.
bool is_honesty_marker(tree decl) {
    if (!VAR_P(decl) || !DECL_DECLARED_CONSTEXPR_P(decl) || DECL_NAME(decl) == NULL_TREE
        || TYPE_MAIN_VARIANT(TREE_TYPE(decl)) != boolean_type_node) {
        return false;
    }
    tree initial = DECL_INITIAL(decl);
    if (initial == NULL_TREE || TREE_CODE(initial) != INTEGER_CST || !integer_zerop(initial)) {
        return false;
    }
    std::string name = identifier_text(DECL_NAME(decl));
    for (const char* suffix : {"_implemented", "_attached", "_ready"}) {
        std::size_t length = std::strlen(suffix);
        if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0) {
            return true;
        }
    }
    return false;
}

// True when DECL has the attribute deprecated with a reason that starts with
// CRUCIBLE_STUB:.
bool has_stub_deprecation(tree decl) {
    tree attribute = lookup_attribute("deprecated", DECL_ATTRIBUTES(decl));
    tree arguments = attribute != NULL_TREE ? TREE_VALUE(attribute) : NULL_TREE;
    tree reason = arguments != NULL_TREE ? TREE_VALUE(arguments) : NULL_TREE;
    return reason != NULL_TREE && TREE_CODE(reason) == STRING_CST
        && static_cast<std::size_t>(TREE_STRING_LENGTH(reason)) > sizeof(kStubPrefix) - 1
        && std::memcmp(TREE_STRING_POINTER(reason), kStubPrefix, sizeof(kStubPrefix) - 1) == 0;
}

// The first honesty marker and the first stub deprecation of one file.
struct StubFile {
    Place marker;
    std::string marker_entity;
    Place deprecation;
    std::string deprecation_entity;
};

bool is_before(const Place& place, const Place& than) {
    return than.file == nullptr || place.line < than.line || (place.line == than.line && place.column < than.column);
}

// Keeps PLACE with ENTITY as the first marker or the first deprecation of its
// file.
using StubFiles = std::unordered_map<const FileEntry*, StubFile>;

void note_stub(StubFiles& stubs, const Place& place, const std::string& entity, bool is_marker) {
    if (place.file == nullptr) {
        return;
    }
    StubFile& file = stubs[place.file];
    Place& held = is_marker ? file.marker : file.deprecation;
    if (is_before(place, held)) {
        held = place;
        (is_marker ? file.marker_entity : file.deprecation_entity) = entity;
    }
}

// The stub deprecations of the unit, by file, and each declaration that
// PLUGIN_FINISH_DECL noted.  A definition of a function in a different file,
// such as a source file that defines a function of a header, moves the
// location of the function there.  The deprecation then counts at the place
// of the declaration that PLUGIN_FINISH_DECL saw, and the walk does not note
// the function again.
StubFiles stub_files;
std::unordered_set<unsigned> noted_stubs;

void on_finish_stub_declaration(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || (TREE_CODE(decl) != FUNCTION_DECL && !VAR_P(decl)) || !has_stub_deprecation(decl)) {
        return;
    }
    Place place = property_place(DECL_SOURCE_LOCATION(decl));
    if (place.file != nullptr && noted_stubs.insert(DECL_UID(decl)).second) {
        note_stub(stub_files, place, TREE_CODE(decl) == FUNCTION_DECL ? function_signature(decl) : qualified_name(decl),
                  false);
    }
}

// The finding of each file with a marker and no deprecation, and of each file
// with a deprecation and no marker (THE RULE stub-discipline), in the
// sequence of the paths.
void report_stubs(const StubFiles& stubs) {
    std::vector<const StubFile*> files;
    for (const auto& entry : stubs) {
        files.push_back(&entry.second);
    }
    std::sort(files.begin(), files.end(), [](const StubFile* left, const StubFile* right) {
        const Place& left_place = left->marker.file != nullptr ? left->marker : left->deprecation;
        const Place& right_place = right->marker.file != nullptr ? right->marker : right->deprecation;
        return left_place.file->relative < right_place.file->relative;
    });
    for (const StubFile* stub : files) {
        if (stub->marker.file != nullptr && stub->deprecation.file == nullptr) {
            record_property(Kind::marker_without_stub, stub->marker, stub->marker_entity);
        }
        if (stub->deprecation.file != nullptr && stub->marker.file == nullptr) {
            record_property(Kind::stub_without_marker, stub->deprecation, stub->deprecation_entity);
        }
    }
}

// ── The rule process-wide-state ─────────────────────────────────────────

// The deepest nest of blocks that the scan of a body reads.
constexpr int kBlockDepth = 256;

// True when DECL, or a function or a class that holds it, is an implicit or
// an explicit instantiation.  A pattern, a member of a class template and a
// lambda in a template are no instantiation, although their template
// information makes is_in_instantiation say so.
bool is_from_instantiation(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
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
        if (VAR_OR_FUNCTION_DECL_P(current) && DECL_LANG_SPECIFIC(current) != nullptr
            && DECL_TEMPLATE_INSTANTIATION(current)) {
            return true;
        }
        current = CP_DECL_CONTEXT(current);
    }
    return false;
}

bool has_process_wide_marker(tree decl) {
    return lookup_attribute("quarantine", "process_wide", DECL_ATTRIBUTES(decl)) != NULL_TREE;
}

// The directories whose code goes into the libraries: the scope of the rule
// process-wide-state.  A test or a bench program has one copy of each object.
constexpr const char* kLibraryDirectories[] = {"include/", "src/", "vessel/"};

// The place at LOCATION when the family reads its file and one of DIRECTORIES
// holds the file, or an empty place.
template <std::size_t Count>
Place place_under(location_t location, const char* const (&directories)[Count]) {
    Place place = property_place(location);
    if (place.file == nullptr) {
        return place;
    }
    for (const char* directory : directories) {
        if (place.file->relative.compare(0, std::strlen(directory), directory) == 0) {
            return place;
        }
    }
    return Place{};
}

// The place at LOCATION when the rule process-wide-state reads its file, or
// an empty place.
Place library_place(location_t location) { return place_under(location, kLibraryDirectories); }

// True when DECL is a variable with static or thread storage duration that
// can change: not constexpr, not const and no reference.
bool is_changeable_static(tree decl) {
    if (!VAR_P(decl) || DECL_ARTIFICIAL(decl) || DECL_DECLARED_CONSTEXPR_P(decl)) {
        return false;
    }
    tree type = TREE_TYPE(decl);
    if (type == NULL_TREE || type == error_mark_node || TYPE_REF_P(type) || CP_TYPE_CONST_P(type)) {
        return false;
    }
    duration_kind duration = decl_storage_duration(decl);
    return duration == dk_static || duration == dk_thread;
}

// True when the variable DECL is a definition: a static data member that is
// inline or that the unit defines out of its class, and each other variable
// that is no extern declaration with no initializer.
bool is_object_definition(tree decl) {
    if (DECL_VAR_DECLARED_INLINE_P(decl)) {
        return true;
    }
    if (DECL_CLASS_SCOPE_P(decl)) {
        return !DECL_IN_AGGR_P(decl);
    }
    return DECL_INITIAL(decl) != NULL_TREE || !DECL_THIS_EXTERN(decl);
}

// True when the function FUNCTION is a part of a template: a pattern, a
// member of a class template, or a lambda in a template.  PROCESSING is the
// value of processing_template_decl when the parser completed the body.
bool is_templated_function(tree function, bool is_processing) {
    if (is_processing) {
        return true;
    }
    if (DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE
        || DECL_TEMPLOID_INSTANTIATION(function)) {
        return false;
    }
    return !DECL_TEMPLATE_SPECIALIZATION(function) || uses_template_parms(DECL_TI_ARGS(function));
}

// The marked object at PLACE with ENTITY, whose marker is on HOLDER: the
// variable, or the function that holds a static local.  The marker makes the
// object one for each process only on an inline holder with external linkage
// outside a template.
void check_marker_faults(tree holder, bool is_variable, bool is_templated, const Place& place,
                         const std::string& entity) {
    if (is_templated) {
        record_property(Kind::marker_in_template, place, entity);
    }
    if (decl_anon_ns_mem_p(holder) || (DECL_NAMESPACE_SCOPE_P(holder) && !TREE_PUBLIC(holder))) {
        record_property(Kind::marker_internal_linkage, place, entity);
    }
    bool is_inline = is_variable ? DECL_VAR_DECLARED_INLINE_P(holder)
                                 : DECL_DECLARED_INLINE_P(holder) || DECL_DECLARED_CONSTEXPR_P(holder)
                                       || DECL_IMMEDIATE_FUNCTION_P(holder);
    if (!is_inline) {
        record_property(Kind::marker_not_inline, place, entity);
    }
}

// A variable at namespace scope or a static data member (THE RULE
// process-wide-state).  IS_TEMPLATED is true for a variable of a template.
void check_process_wide_variable(tree decl, bool is_templated) {
    bool is_marked = has_process_wide_marker(decl);
    bool is_object = is_changeable_static(decl) && is_object_definition(decl);
    if (!is_marked && !is_object) {
        return;
    }
    Place place = library_place(DECL_SOURCE_LOCATION(decl));
    if (place.file == nullptr) {
        return;
    }
    std::string entity = qualified_name(decl);
    if (!is_object) {
        record_property(Kind::marker_without_object, place, entity);
    } else if (!is_marked) {
        record_property(Kind::unmarked_static, place, entity);
    } else {
        check_marker_faults(decl, true, is_templated, place, entity);
    }
}

// Each static local of the blocks from BLOCK on, and of their subblocks.
// Complexity: O(blocks and declarations of the body).
void collect_static_locals(tree block, std::vector<tree>& objects, int depth) {
    for (; block != NULL_TREE && depth < kBlockDepth; block = BLOCK_CHAIN(block)) {
        for (tree decl = BLOCK_VARS(block); decl != NULL_TREE; decl = DECL_CHAIN(decl)) {
            if (is_changeable_static(decl) && !DECL_THIS_EXTERN(decl)) {
                objects.push_back(decl);
            }
        }
        collect_static_locals(BLOCK_SUBBLOCKS(block), objects, depth + 1);
    }
}

// The name of the function that holds the static locals of FUNCTION: the
// function itself, or the function that holds a lambda.
std::string holder_name(tree function) {
    tree current = function;
    for (int depth = 0; current != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(current) != FUNCTION_DECL || !LAMBDA_FUNCTION_P(current)) {
            break;
        }
        tree closure = DECL_CONTEXT(current);
        current = closure != NULL_TREE && TYPE_P(closure) ? CP_TYPE_CONTEXT(closure) : NULL_TREE;
    }
    if (current == NULL_TREE || TREE_CODE(current) != FUNCTION_DECL) {
        return qualified_name(function) + "()";
    }
    return qualified_name(current) + "()";
}

// The functions that hold a static local in this unit, by DECL_UID.
std::unordered_set<unsigned> holders_with_object;

// The static locals of the body of FUNCTION, at the end of its parse (THE
// RULE process-wide-state).
void check_static_locals(tree function, bool is_processing) {
    tree body = DECL_INITIAL(function);
    if (body == NULL_TREE || TREE_CODE(body) != BLOCK) {
        return;
    }
    std::vector<tree> objects;
    collect_static_locals(body, objects, 0);
    if (objects.empty()) {
        return;
    }
    holders_with_object.insert(DECL_UID(function));
    bool is_marked = has_process_wide_marker(function);
    bool is_templated = is_marked && is_templated_function(function, is_processing);
    std::string prefix = holder_name(function);
    for (tree decl : objects) {
        Place place = library_place(DECL_SOURCE_LOCATION(decl));
        if (place.file == nullptr) {
            continue;
        }
        std::string entity = prefix + "::" + simple_name(decl);
        if (!is_marked) {
            record_property(Kind::unmarked_static, place, entity);
        } else {
            check_marker_faults(function, false, is_templated, place, entity);
        }
    }
}

// A marked function that holds no static local in this unit (THE RULE
// process-wide-state).  The walk asks at PLUGIN_FINISH_UNIT, after the parse
// of each body.
void check_marked_function(tree function) {
    if (holders_with_object.count(DECL_UID(function)) != 0) {
        return;
    }
    record_property(Kind::marker_without_object, library_place(DECL_SOURCE_LOCATION(function)),
                    qualified_name(function) + "()");
}

// ── The rule lifetime-twin ──────────────────────────────────────────────

// One parameter that CRUCIBLE_LIFETIMEBOUND marks, with its function.
struct LifetimeClaim {
    tree function = NULL_TREE;
    int index = 0;
    int arity = 0;
    tree core_type = NULL_TREE;  // the type of the parameter, with no reference
    Place place;
    std::string entity;
};

// The places of the parameters that the attribute marks, the first and the
// last of them, and the claim of each place.
std::unordered_set<location_t> lifetime_bound_parameters;
location_t first_lifetime_bound = UNKNOWN_LOCATION;
location_t last_lifetime_bound = UNKNOWN_LOCATION;
std::unordered_map<location_t, LifetimeClaim> lifetime_claims;

// The number of the parameters of FUNCTION that the code spells: no this
// and no other artificial parameter.  A pack counts as one.
int user_arity(tree function) {
    int count = 0;
    for (tree type = FUNCTION_FIRST_USER_PARMTYPE(function); type != NULL_TREE && type != void_list_node;
         type = TREE_CHAIN(type)) {
        ++count;
    }
    return count;
}

// The function_signature of FUNCTION, with the name of the class for a
// constructor, as the code spells it.
std::string lifetime_entity(tree function) {
    std::string signature = function_signature(function);
    tree owner = DECL_CONTEXT(function);
    if (!DECL_CONSTRUCTOR_P(function) || owner == NULL_TREE || !TYPE_P(owner) || TYPE_NAME(owner) == NULL_TREE) {
        return signature;
    }
    std::string name = qualified_name(function);
    return qualified_name(owner) + "::" + simple_name(TYPE_NAME(owner)) + signature.substr(name.size());
}

// The claim of each marked parameter of FUNCTION that has no claim (THE RULE
// lifetime-twin).  The places of the marked parameters bound the test of a
// place before the hash lookup.  Complexity: O(parameters).
void note_lifetime_claims(tree function) {
    if (lifetime_bound_parameters.empty() || function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL
        || is_from_instantiation(function)) {
        return;
    }
    int index = 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter), ++index) {
        location_t location = DECL_SOURCE_LOCATION(parameter);
        if (location < first_lifetime_bound || location > last_lifetime_bound
            || lifetime_bound_parameters.count(location) == 0 || lifetime_claims.count(location) != 0) {
            continue;
        }
        LifetimeClaim claim;
        claim.place = property_place(location);
        if (claim.place.file != nullptr) {
            tree type = TREE_TYPE(parameter);
            claim.function = function;
            claim.index = index;
            claim.arity = user_arity(function);
            claim.core_type = TYPE_REF_P(type) ? TREE_TYPE(type) : type;
            claim.entity = lifetime_entity(function) + " parameter " + std::to_string(index + 1);
        }
        lifetime_claims.emplace(location, claim);
    }
}

// The functions of the scope of FUNCTION with its name.  A friend is in the
// scope of its namespace.
tree overload_set(tree function) {
    tree scope = CP_DECL_CONTEXT(function);
    if (scope == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(scope)) {
        if (!CLASS_TYPE_P(scope)) {
            return NULL_TREE;
        }
        return DECL_CONSTRUCTOR_P(function) ? CLASSTYPE_CONSTRUCTORS(scope)
                                            : get_class_binding_direct(scope, DECL_NAME(function));
    }
    return TREE_CODE(scope) == NAMESPACE_DECL ? get_namespace_binding(scope, DECL_NAME(function)) : NULL_TREE;
}

// The file that holds the expansion point of LOCATION, or null.
const FileEntry* expansion_file(location_t location) {
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION) {
        return nullptr;
    }
    location_t point = linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, nullptr);
    expanded_location where = expand_location(point);
    return where.file != nullptr ? &classify_file(where.file) : nullptr;
}

// True when CANDIDATE is a deleted overload in the file of CLAIM that refuses
// a temporary at the index of the claim.
bool refuses_temporary(tree candidate, const LifetimeClaim& claim) {
    tree function = STRIP_TEMPLATE(candidate);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || !DECL_DELETED_FN(function) || expansion_file(DECL_SOURCE_LOCATION(function)) != claim.place.file) {
        return false;
    }
    std::vector<tree> types;
    for (tree type = FUNCTION_FIRST_USER_PARMTYPE(function); type != NULL_TREE && type != void_list_node;
         type = TREE_CHAIN(type)) {
        types.push_back(TREE_VALUE(type));
    }
    int count = static_cast<int>(types.size());
    bool is_variadic = count > 0 && TREE_CODE(types.back()) == TYPE_PACK_EXPANSION;
    if (!(count == claim.arity || (is_variadic && count <= claim.arity + 1)) || claim.index >= count) {
        return false;
    }
    tree type = types[static_cast<std::size_t>(claim.index)];
    if (TREE_CODE(type) != REFERENCE_TYPE || !TYPE_REF_IS_RVALUE(type)) {
        return false;
    }
    tree referred = TREE_TYPE(type);
    return TREE_CODE(referred) == TEMPLATE_TYPE_PARM
        || same_type_ignoring_top_level_qualifiers_p(referred, claim.core_type);
}

// One finding for each claim with no deleted overload that refuses a
// temporary, in the sequence of the places.  Complexity: O(claims times
// overloads).
void report_lifetime_claims() {
    std::vector<const LifetimeClaim*> claims;
    for (const auto& entry : lifetime_claims) {
        if (entry.second.function != NULL_TREE) {
            claims.push_back(&entry.second);
        }
    }
    std::sort(claims.begin(), claims.end(), [](const LifetimeClaim* left, const LifetimeClaim* right) {
        if (left->place.file != right->place.file) {
            return left->place.file->relative < right->place.file->relative;
        }
        return left->place.line != right->place.line ? left->place.line < right->place.line
                                                     : left->place.column < right->place.column;
    });
    for (const LifetimeClaim* claim : claims) {
        bool is_refused = false;
        for (ovl_iterator candidate(overload_set(claim->function), true); candidate && !is_refused; ++candidate) {
            is_refused = refuses_temporary(*candidate, *claim);
        }
        if (!is_refused) {
            record_property(Kind::missing_twin, claim->place, claim->entity);
        }
    }
}

// The handler of quarantine::lifetimebound: it notes the place of a parameter,
// and it refuses each other declaration.
tree handle_lifetime_bound(tree* node, tree, tree, int, bool* no_add_attrs) {
    if (TREE_CODE(*node) != PARM_DECL) {
        error_at(DECL_P(*node) ? DECL_SOURCE_LOCATION(*node) : input_location,
                 "quarantine: %<CRUCIBLE_LIFETIMEBOUND%> marks a parameter, and this declaration is no parameter; put "
                 "the marker after the name of the parameter whose argument the result borrows");
        *no_add_attrs = true;
        return NULL_TREE;
    }
    location_t location = DECL_SOURCE_LOCATION(*node);
    lifetime_bound_parameters.insert(location);
    if (first_lifetime_bound == UNKNOWN_LOCATION || location < first_lifetime_bound) {
        first_lifetime_bound = location;
    }
    if (location > last_lifetime_bound) {
        last_lifetime_bound = location;
    }
    return NULL_TREE;
}

// The handler of quarantine::process_wide: it refuses a declaration that is no
// variable and no function.
tree handle_process_wide(tree* node, tree, tree, int, bool* no_add_attrs) {
    if (!VAR_OR_FUNCTION_DECL_P(*node)) {
        error_at(DECL_P(*node) ? DECL_SOURCE_LOCATION(*node) : input_location,
                 "quarantine: %<CRUCIBLE_PROCESS_WIDE%> marks a variable or the function that holds a static local, "
                 "and this declaration is neither");
        *no_add_attrs = true;
    }
    return NULL_TREE;
}

const attribute_spec kPropertyAttributes[] = {
    {"process_wide", 0, 0, true, false, false, false, handle_process_wide, nullptr},
    {"lifetimebound", 0, 0, true, false, false, false, handle_lifetime_bound, nullptr},
};

// The namespace is the name of the plugin, and no layer of the tree has the
// name, so a base header can spell it (CLAUDE.md section XVI, the layer rule).
const scoped_attribute_specs kPropertyAttributeSpecs = {"quarantine", {kPropertyAttributes}};

void on_register_attributes(void*, void*) { register_scoped_attributes(kPropertyAttributeSpecs); }

// A declaration of a function that is no template (THE RULE lifetime-twin).
void on_finish_lifetime_declaration(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl != NULL_TREE && TREE_CODE(decl) == FUNCTION_DECL) {
        note_lifetime_claims(decl);
    }
}

// The definition of a template gives the parameters of each earlier
// declaration of its name before GCC merges the two (THE RULE lifetime-twin).
void on_start_function_property(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (lifetime_bound_parameters.empty() || processing_template_decl == 0 || function == NULL_TREE
        || TREE_CODE(function) != FUNCTION_DECL || DECL_NAME(function) == NULL_TREE) {
        return;
    }
    for (ovl_iterator candidate(overload_set(function), true); candidate; ++candidate) {
        tree earlier = STRIP_TEMPLATE(*candidate);
        if (earlier != NULL_TREE && earlier != function) {
            note_lifetime_claims(earlier);
        }
    }
}

// The end of the parse of a body: its static locals and its own marked
// parameters.
void on_finish_function_property(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || is_from_instantiation(function)) {
        return;
    }
    note_lifetime_claims(function);
    check_static_locals(function, processing_template_decl != 0);
}

// ── The rule padded-list ────────────────────────────────────────────────

// The headers and the sources of the rule padded-list.
constexpr const char* kPaddedListDirectories[] = {"include/", "src/"};
// The smallest element count of a list that gives a line.  The module of the
// rule applies the threshold of its row of utils/registry.txt.
constexpr std::uint64_t kSmallestList = 2;

// The size of TYPE in bits, or 0 when the size is no constant.
std::uint64_t size_in_bits(tree type) {
    tree size = TYPE_SIZE(type);
    return size != NULL_TREE && TREE_CODE(size) == INTEGER_CST && tree_fits_uhwi_p(size) ? tree_to_uhwi(size) : 0;
}

// The element count of the array type TYPE, or 0 when its bound is no
// constant.
std::uint64_t array_extent(tree type) {
    tree domain = TYPE_DOMAIN(type);
    tree maximum = domain != NULL_TREE ? TYPE_MAX_VALUE(domain) : NULL_TREE;
    if (maximum == NULL_TREE || TREE_CODE(maximum) != INTEGER_CST || !tree_fits_uhwi_p(maximum)) {
        return 0;
    }
    return tree_to_uhwi(maximum) + 1;
}

// The product of two counts, or the largest count when the product does not
// fit.  A list that large is a finding, and the overflow then fails closed.
std::uint64_t saturated_product(std::uint64_t left, std::uint64_t right) {
    std::uint64_t product = 0;
    return __builtin_mul_overflow(left, right, &product) ? UINT64_MAX : product;
}

// The sum of two counts, or the largest count when the sum does not fit.
std::uint64_t saturated_sum(std::uint64_t left, std::uint64_t right) {
    std::uint64_t sum = 0;
    return __builtin_add_overflow(left, right, &sum) ? UINT64_MAX : sum;
}

// A half-open range of bits of an object, [first, last).
using BitRange = std::pair<std::uint64_t, std::uint64_t>;

// The largest number of value ranges that the rule keeps for one type.  Only
// a union needs the ranges of its members.  When the members have more
// ranges, the union holds the value bits of its largest member, and the rule
// then fails closed.
constexpr std::size_t kMostRanges = 4096;

// The value ranges of a type, joined and sorted.  is_complete is false, and
// ranges is empty, when the type has more than kMostRanges ranges.
struct RangeFacts {
    std::vector<BitRange> ranges;
    bool is_complete = true;
};

// The longest fixed list of one element type that has a padding bit, and its
// element count.
struct PaddedList {
    tree element = NULL_TREE;
    std::uint64_t count = 0;
};

// The answers of the rule padded-list for each type, for one walk.  The walk
// runs at PLUGIN_FINISH_UNIT and causes no garbage collection, and each type
// then keeps its address while the answers live.  A reference to a value of
// an unordered_map stays valid when the map grows.
struct PaddingFacts {
    std::unordered_map<tree, std::uint64_t> value_bits;
    std::unordered_map<tree, RangeFacts> value_ranges;
    std::unordered_map<tree, std::vector<PaddedList>> lists;
};

// Sorts RANGES and joins each two that touch or overlap.  Complexity:
// O(r log r) for r ranges.
void join_ranges(std::vector<BitRange>& ranges) {
    std::sort(ranges.begin(), ranges.end());
    std::size_t kept = 0;
    for (const BitRange& range : ranges) {
        if (kept != 0 && range.first <= ranges[kept - 1].second) {
            ranges[kept - 1].second = std::max(ranges[kept - 1].second, range.second);
        } else {
            ranges[kept++] = range;
        }
    }
    ranges.resize(kept);
}

// True when FIELD of a class or a union has a constant size: a data member, a
// base or the pointer to the virtual table.
bool has_constant_size(tree field) {
    if (TREE_CODE(field) != FIELD_DECL || DECL_SIZE(field) == NULL_TREE) {
        return false;
    }
    return TREE_CODE(DECL_SIZE(field)) == INTEGER_CST && tree_fits_uhwi_p(DECL_SIZE(field));
}

const RangeFacts& value_ranges(tree type, PaddingFacts& facts, int depth);
std::uint64_t value_bits(tree type, PaddingFacts& facts, int depth);

// Appends the value ranges of TYPE, moved by OFFSET bits, to RESULT.  RESULT
// becomes incomplete when it would hold more than kMostRanges ranges.
void append_ranges(tree type, std::uint64_t offset, RangeFacts& result, PaddingFacts& facts, int depth) {
    const RangeFacts& inner = value_ranges(type, facts, depth);
    if (!inner.is_complete || inner.ranges.size() > kMostRanges - result.ranges.size()) {
        result.is_complete = false;
        return;
    }
    for (const BitRange& range : inner.ranges) {
        result.ranges.emplace_back(offset + range.first, offset + range.second);
    }
}

// The value ranges of the class or union TYPE: each field at its bit
// position.  A bit-field with no name is padding, and a field with no
// constant size or place holds no value bit.
void record_ranges(tree type, RangeFacts& result, PaddingFacts& facts, int depth) {
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE && result.is_complete; field = DECL_CHAIN(field)) {
        if (!has_constant_size(field)) {
            continue;
        }
        tree position = bit_position(field);
        if (TREE_CODE(position) != INTEGER_CST || !tree_fits_uhwi_p(position)) {
            continue;
        }
        std::uint64_t first = tree_to_uhwi(position);
        if (DECL_BIT_FIELD_TYPE(field) == NULL_TREE) {
            append_ranges(TREE_TYPE(field), first, result, facts, depth + 1);
        } else if (DECL_NAME(field) != NULL_TREE && result.ranges.size() < kMostRanges) {
            result.ranges.emplace_back(first, first + tree_to_uhwi(DECL_SIZE(field)));
        } else if (DECL_NAME(field) != NULL_TREE) {
            result.is_complete = false;
        }
    }
}

// The value ranges of the array type TYPE: one range when an element has no
// padding, and else the ranges of each element at its place.  Complexity:
// O(kMostRanges) at most.
void array_ranges(tree type, RangeFacts& result, PaddingFacts& facts, int depth) {
    tree element = TREE_TYPE(type);
    std::uint64_t count = array_extent(type);
    std::uint64_t element_bits = size_in_bits(element);
    if (count == 0 || element_bits == 0) {
        return;
    }
    const RangeFacts& inner = value_ranges(element, facts, depth + 1);
    if (!inner.is_complete) {
        result.is_complete = false;
        return;
    }
    if (inner.ranges.size() == 1 && inner.ranges[0] == BitRange{0, element_bits}) {
        result.ranges.emplace_back(0, saturated_product(count, element_bits));
        return;
    }
    if (inner.ranges.empty()) {
        return;
    }
    if (count > kMostRanges / inner.ranges.size()) {
        result.is_complete = false;
        return;
    }
    for (std::uint64_t index = 0; index < count; ++index) {
        std::uint64_t offset = index * element_bits;
        for (const BitRange& range : inner.ranges) {
            result.ranges.emplace_back(offset + range.first, offset + range.second);
        }
    }
}

// The bit ranges of an object of TYPE that hold a value, joined and sorted
// (THE RULE padded-list).  Only the value bits of a union read them.  A
// reference holds a pointer, and a floating-point type holds the bits of its
// precision.  A type deeper than kTypeDepth holds no value bit, and the walk
// then fails closed.  The walk keeps the answer of each type.
// Complexity: O(kMostRanges log kMostRanges) at most, one time for each type.
const RangeFacts& value_ranges(tree type, PaddingFacts& facts, int depth) {
    static const RangeFacts kNoRange;
    type = TYPE_MAIN_VARIANT(type);
    auto cached = facts.value_ranges.find(type);
    if (cached != facts.value_ranges.end()) {
        return cached->second;
    }
    if (depth > kTypeDepth) {
        return kNoRange;
    }
    RangeFacts result;
    if (TYPE_REF_P(type)) {
        result.ranges.emplace_back(0, size_in_bits(ptr_type_node));
    } else if (TREE_CODE(type) == ARRAY_TYPE) {
        array_ranges(type, result, facts, depth);
    } else if (RECORD_OR_UNION_TYPE_P(type)) {
        record_ranges(type, result, facts, depth);
    } else if (TREE_CODE(type) == REAL_TYPE) {
        result.ranges.emplace_back(0, static_cast<std::uint64_t>(TYPE_PRECISION(type)));
    } else if (std::uint64_t bits = size_in_bits(type); bits != 0) {
        result.ranges.emplace_back(0, bits);
    }
    if (result.is_complete) {
        join_ranges(result.ranges);
    } else {
        result.ranges.clear();
    }
    return facts.value_ranges.emplace(type, std::move(result)).first->second;
}

// The value bits of FIELD of a class or a union: the bits of a named
// bit-field, or the value bits of the type of the field.  A bit-field with no
// name, and a field with no constant size, hold no value bit.
std::uint64_t field_value_bits(tree field, PaddingFacts& facts, int depth) {
    if (!has_constant_size(field)) {
        return 0;
    }
    if (DECL_BIT_FIELD_TYPE(field) != NULL_TREE) {
        return DECL_NAME(field) != NULL_TREE ? tree_to_uhwi(DECL_SIZE(field)) : 0;
    }
    return value_bits(TREE_TYPE(field), facts, depth);
}

// The value bits of the union TYPE: the bits of the union of the value ranges
// of its members.  When the members have more than kMostRanges ranges, the
// union holds the value bits of its largest member, and the rule then fails
// closed.
std::uint64_t union_value_bits(tree type, PaddingFacts& facts, int depth) {
    const RangeFacts& ranges = value_ranges(type, facts, depth);
    std::uint64_t covered = 0;
    if (ranges.is_complete) {
        for (const BitRange& range : ranges.ranges) {
            covered = saturated_sum(covered, range.second - range.first);
        }
        return covered;
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        covered = std::max(covered, field_value_bits(field, facts, depth + 1));
    }
    return covered;
}

// The number of the bits of an object of TYPE that hold a value (THE RULE
// padded-list).  The fields of a class do not overlap in their value bits,
// and the value bits of a class are then the sum of the value bits of its
// fields.  An array holds the value bits of each element.  A type deeper
// than kTypeDepth holds no value bit, and the walk then fails closed.  The
// walk keeps the answer of each type.
// Complexity: linear in the fields of the type, one time for each type.
std::uint64_t value_bits(tree type, PaddingFacts& facts, int depth) {
    type = TYPE_MAIN_VARIANT(type);
    auto cached = facts.value_bits.find(type);
    if (cached != facts.value_bits.end()) {
        return cached->second;
    }
    if (depth > kTypeDepth) {
        return 0;
    }
    std::uint64_t bits = 0;
    if (TYPE_REF_P(type)) {
        bits = size_in_bits(ptr_type_node);
    } else if (TREE_CODE(type) == ARRAY_TYPE) {
        bits = saturated_product(array_extent(type), value_bits(TREE_TYPE(type), facts, depth + 1));
    } else if (TREE_CODE(type) == UNION_TYPE || TREE_CODE(type) == QUAL_UNION_TYPE) {
        bits = union_value_bits(type, facts, depth);
    } else if (RECORD_OR_UNION_TYPE_P(type)) {
        for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
            bits = saturated_sum(bits, field_value_bits(field, facts, depth + 1));
        }
    } else if (TREE_CODE(type) == REAL_TYPE) {
        bits = static_cast<std::uint64_t>(TYPE_PRECISION(type));
    } else {
        bits = size_in_bits(type);
    }
    facts.value_bits.emplace(type, bits);
    return bits;
}

// True when the search of a list enters the class TYPE: a specialization of
// a class template, such as std::array, or a class with no name.  A class
// with a name of its own is a list only where the code declares an array of
// it.
bool is_list_holder(tree type) {
    if (!CLASS_TYPE_P(type) || LAMBDA_TYPE_P(type)) {
        return false;
    }
    if (TYPE_UNNAMED_P(type)) {
        return true;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    return info != NULL_TREE && TREE_CODE(TI_TEMPLATE(info)) == TEMPLATE_DECL && PRIMARY_TEMPLATE_P(TI_TEMPLATE(info));
}

// Puts the list of COUNT elements of ELEMENT into LISTS.  LISTS keeps the
// longest list of each element type, in the order of the search.
// Complexity: linear in the lists of LISTS.
void merge_list(std::vector<PaddedList>& lists, tree element, std::uint64_t count) {
    for (PaddedList& list : lists) {
        if (list.element == element) {
            list.count = std::max(list.count, count);
            return;
        }
    }
    lists.push_back(PaddedList{element, count});
}

const std::vector<PaddedList>& lists_of(tree type, PaddingFacts& facts, int depth);

// The lists of the array type TYPE: the array of arrays to its first element
// that is no array, with the product of the counts.  The element is a list
// when it is a complete class or union with a padding bit.  A list in the
// element is a list of the product of the two counts.
void array_lists(tree type, std::vector<PaddedList>& found, PaddingFacts& facts, int depth) {
    std::uint64_t total = 1;
    tree leaf = type;
    for (; TREE_CODE(leaf) == ARRAY_TYPE; leaf = TYPE_MAIN_VARIANT(TREE_TYPE(leaf))) {
        std::uint64_t extent = array_extent(leaf);
        if (extent == 0) {
            return;
        }
        total = saturated_product(total, extent);
    }
    if (!RECORD_OR_UNION_TYPE_P(leaf) || !COMPLETE_TYPE_P(leaf)) {
        return;
    }
    std::uint64_t bits = size_in_bits(leaf);
    if (bits != 0 && value_bits(leaf, facts, depth + 1) < bits) {
        merge_list(found, leaf, total);
    }
    for (const PaddedList& inner : lists_of(leaf, facts, depth + 1)) {
        merge_list(found, inner.element, saturated_product(total, inner.count));
    }
}

// The lists of the class TYPE that is_list_holder enters: the lists of its
// bases and of its data members.
void holder_lists(tree type, std::vector<PaddedList>& found, PaddingFacts& facts, int depth) {
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            for (const PaddedList& inner : lists_of(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), facts, depth + 1)) {
                merge_list(found, inner.element, inner.count);
            }
        }
    }
    for (tree field = TYPE_FIELDS(type); field != NULL_TREE; field = DECL_CHAIN(field)) {
        if (TREE_CODE(field) == FIELD_DECL && !DECL_FIELD_IS_BASE(field) && DECL_BIT_FIELD_TYPE(field) == NULL_TREE) {
            for (const PaddedList& inner : lists_of(TREE_TYPE(field), facts, depth + 1)) {
                merge_list(found, inner.element, inner.count);
            }
        }
    }
}

// The longest fixed list of each element type with a padding bit in one
// object of TYPE (THE RULE padded-list).  The search reads an array, and the
// bases and the data members of a class that is_list_holder enters.  A type
// deeper than kTypeDepth holds no list.  The walk keeps the answer of each
// type, so a type that holds one class two times reads that class one time.
// Complexity: linear in the fields of the type times its lists, one time for
// each type.
const std::vector<PaddedList>& lists_of(tree type, PaddingFacts& facts, int depth) {
    static const std::vector<PaddedList> kNoList;
    type = TYPE_MAIN_VARIANT(type);
    auto cached = facts.lists.find(type);
    if (cached != facts.lists.end()) {
        return cached->second;
    }
    if (depth > kTypeDepth) {
        return kNoList;
    }
    std::vector<PaddedList> found;
    if (TREE_CODE(type) == ARRAY_TYPE) {
        array_lists(type, found, facts, depth);
    } else if (RECORD_OR_UNION_TYPE_P(type) && COMPLETE_TYPE_P(type) && is_list_holder(type)) {
        holder_lists(type, found, facts, depth);
    }
    return facts.lists.emplace(type, std::move(found)).first->second;
}

// True when the search can find a list in a declaration of TYPE: an array or
// a class, which no pointer and no reference holds.  The test reads no place.
bool can_hold_list(tree type) {
    if (type == NULL_TREE || type == error_mark_node || TYPE_REF_P(type) || TREE_CODE(type) == POINTER_TYPE) {
        return false;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    return TREE_CODE(main_type) == ARRAY_TYPE || RECORD_OR_UNION_TYPE_P(main_type);
}

// True when LISTS holds a list of kSmallestList or more elements.
bool has_line(const std::vector<PaddedList>& lists) {
    return std::any_of(lists.begin(), lists.end(), [](const PaddedList& list) { return list.count >= kSmallestList; });
}

// One line at PLACE for each list of kSmallestList or more elements of LISTS
// (THE RULE padded-list).  DECLARATION names the declaration in the entity.
void record_padded_lists(Kind kind, const Place& place, const std::vector<PaddedList>& lists,
                         const std::string& declaration, PaddingFacts& facts) {
    for (const PaddedList& list : lists) {
        if (list.count < kSmallestList) {
            continue;
        }
        std::uint64_t holes = size_in_bits(list.element) - value_bits(list.element, facts, 0);
        record_property(kind, place,
                        declaration + ": " + type_text(list.element) + " | count=" + std::to_string(list.count)
                            + " holes=" + std::to_string(holes));
    }
}

// The place of the declaration DECL when the rule padded-list reads its file,
// or an empty place.
Place padded_list_place(tree decl) { return place_under(DECL_SOURCE_LOCATION(decl), kPaddedListDirectories); }

// The type of an alias, a variable or a data member DECL (THE RULE
// padded-list).  The function reads the place first, and a declaration
// outside the scope of the rule then reads no type.
void check_padded_declaration(tree decl, Kind kind, PaddingFacts& facts) {
    if (!can_hold_list(TREE_TYPE(decl))) {
        return;
    }
    Place place = padded_list_place(decl);
    if (place.file == nullptr) {
        return;
    }
    const std::vector<PaddedList>& lists = lists_of(TREE_TYPE(decl), facts, 0);
    if (has_line(lists)) {
        record_padded_lists(kind, place, lists, qualified_name(decl), facts);
    }
}

// True when the function FUNCTION returns or takes by value a type that can
// hold a list.  The test reads no place.
bool can_take_list(tree function) {
    if (can_hold_list(TREE_TYPE(TREE_TYPE(function)))) {
        return true;
    }
    for (tree type = FUNCTION_FIRST_USER_PARMTYPE(function); type != NULL_TREE && type != void_list_node;
         type = TREE_CHAIN(type)) {
        if (can_hold_list(TREE_VALUE(type))) {
            return true;
        }
    }
    return false;
}

// The return type and each parameter by value of the function FUNCTION (THE
// RULE padded-list).  A constructor and a destructor have no return type of
// their own, and a function that the compiler declares is no declaration of
// the code.  The function reads the place first, and a function outside the
// scope of the rule then reads no type.
void check_padded_signature(tree function, PaddingFacts& facts) {
    if (DECL_ARTIFICIAL(function)) {
        return;
    }
    Place place = padded_list_place(function);
    if (place.file == nullptr) {
        return;
    }
    std::string signature;
    auto signature_of = [&signature, function]() -> const std::string& {
        if (signature.empty()) {
            signature = function_signature(function);
        }
        return signature;
    };
    tree result = TREE_TYPE(TREE_TYPE(function));
    if (!DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function) && can_hold_list(result)) {
        const std::vector<PaddedList>& lists = lists_of(result, facts, 0);
        if (has_line(lists)) {
            record_padded_lists(Kind::padded_return, place, lists, signature_of(), facts);
        }
    }
    int index = 0;
    for (tree type = FUNCTION_FIRST_USER_PARMTYPE(function); type != NULL_TREE && type != void_list_node;
         type = TREE_CHAIN(type), ++index) {
        if (!can_hold_list(TREE_VALUE(type))) {
            continue;
        }
        const std::vector<PaddedList>& lists = lists_of(TREE_VALUE(type), facts, 0);
        if (has_line(lists)) {
            record_padded_lists(Kind::padded_parameter, place, lists,
                                signature_of() + " parameter " + std::to_string(index + 1), facts);
        }
    }
}

// ── The walk of the family ──────────────────────────────────────────────

struct PropertyWalk {
    std::unordered_set<tree> visited;
    StubFiles& stubs = stub_files;
    PaddingFacts padding;
    // Each class that an alias names, and True when a member function of the
    // class is a mint (aliased_mint_class).
    std::unordered_map<tree, bool> mint_classes;

    // Each rule of a function first asks a test that reads no place: its name,
    // its types or its attributes.  A pattern has template information, so
    // the test of an instantiation reads the scope of a pattern and not the
    // pattern itself.
    void function(tree function, bool is_pattern) {
        if (is_pattern) {
            walk_family_pattern(function);
        }
        bool is_mint_name = is_mint(function);
        bool is_door = returns_raw(function);
        bool is_stub = has_stub_deprecation(function);
        bool is_marked = has_process_wide_marker(function);
        bool is_claimed = is_pattern && !lifetime_bound_parameters.empty();
        bool is_listed = !is_pattern && can_take_list(function);
        if (!is_mint_name && !is_door && !is_stub && !is_marked && !is_claimed && !is_listed) {
            return;
        }
        tree tested = is_pattern ? CP_DECL_CONTEXT(function) : function;
        if (!visited.insert(function).second || is_in_instantiation(tested)) {
            return;
        }
        if (is_mint_name) {
            check_mint(function, is_pattern);
        }
        if (is_door) {
            check_escape(function, DECL_INITIAL(function) != NULL_TREE || DECL_SAVED_TREE(function) != NULL_TREE);
        }
        if (is_stub && noted_stubs.insert(DECL_UID(function)).second) {
            note_stub(stubs, property_place(DECL_SOURCE_LOCATION(function)), function_signature(function), false);
        }
        if (is_marked) {
            check_marked_function(function);
        }
        if (is_claimed) {
            note_lifetime_claims(function);
        }
        if (is_listed) {
            check_padded_signature(function, padding);
        }
    }

    // A variable at namespace scope or a static data member.  IS_TEMPLATED
    // is true for a variable of a template.
    void variable(tree decl, bool is_templated) {
        if (DECL_CLASS_SCOPE_P(decl)) {
            check_permission_member(decl);
        }
        check_process_wide_variable(decl, is_templated);
        if (!is_templated) {
            check_padded_declaration(decl, Kind::padded_member, padding);
        }
        if (is_honesty_marker(decl)) {
            note_stub(stubs, property_place(DECL_SOURCE_LOCATION(decl)), qualified_name(decl), true);
        }
    }

    // An alias or a typedef, which is no class that it names.  IS_PATTERN is
    // true for an alias template and for an alias of a class template.
    void alias(tree decl, bool is_pattern) {
        if (!DECL_ARTIFICIAL(decl) && !DECL_IMPLICIT_TYPEDEF_P(decl)) {
            check_array_declaration(decl, Kind::array_alias);
            check_mint_alias(decl, mint_classes);
            if (!is_pattern) {
                check_padded_declaration(decl, Kind::padded_alias, padding);
            }
        }
    }

    void template_decl(tree template_decl) {
        if (!visited.insert(template_decl).second) {
            return;
        }
        tree result = DECL_TEMPLATE_RESULT(template_decl);
        if (result == NULL_TREE) {
            return;
        }
        if (TREE_CODE(result) == FUNCTION_DECL) {
            function(result, true);
            return;
        }
        if (VAR_P(result)) {
            variable(result, true);
            return;
        }
        if (TREE_CODE(result) == TYPE_DECL && !DECL_IMPLICIT_TYPEDEF_P(result)) {
            alias(result, true);
            return;
        }
        if (TREE_CODE(result) != TYPE_DECL || !CLASS_TYPE_P(TREE_TYPE(result))) {
            return;
        }
        class_type(TREE_TYPE(result), true);
        for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
            tree partial = TREE_VALUE(entry);
            if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
                this->template_decl(partial);
            }
        }
        for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
            tree specialization = TREE_VALUE(entry);
            if (specialization != NULL_TREE && CLASS_TYPE_P(specialization)
                && CLASSTYPE_TEMPLATE_SPECIALIZATION(specialization) && !uses_template_parms(specialization)) {
                class_type(specialization, false);
            }
        }
    }

    void class_type(tree type, bool is_pattern) {
        tree main_type = TYPE_MAIN_VARIANT(type);
        if (!CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type) || !visited.insert(main_type).second) {
            return;
        }
        // The walk reads the members of a class only when the family reads the
        // file of the class (THE SCOPE).
        tree decl = TYPE_MAIN_DECL(main_type);
        if (decl == NULL_TREE || property_place(DECL_SOURCE_LOCATION(decl)).file == nullptr) {
            return;
        }
        check_witness(main_type);
        for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
            switch (TREE_CODE(member)) {
                case FIELD_DECL:
                    if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                        class_type(TREE_TYPE(member), is_pattern);
                    } else if (!DECL_ARTIFICIAL(member)) {
                        check_array_declaration(member, Kind::array_member);
                        check_permission_member(member);
                        if (!is_pattern) {
                            check_padded_declaration(member, Kind::padded_member, padding);
                        }
                    }
                    break;
                case VAR_DECL:
                    variable(member, is_pattern);
                    break;
                case FUNCTION_DECL:
                    function(member, is_pattern);
                    break;
                case TYPE_DECL:
                    if (DECL_SELF_REFERENCE_P(member)) {
                        break;
                    }
                    if (DECL_IMPLICIT_TYPEDEF_P(member) && CLASS_TYPE_P(TREE_TYPE(member))) {
                        class_type(TREE_TYPE(member), is_pattern);
                    } else {
                        alias(member, is_pattern);
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
                    template_decl(befriended);
                } else if (befriended != NULL_TREE && TREE_CODE(befriended) == FUNCTION_DECL) {
                    function(befriended, true);
                }
            }
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
        // A class asks for the place of its file one time (class_type), and a
        // function, a variable or an alias asks only when a test that reads no
        // place admits it.  The walk then reads no place for each declaration
        // of a file that the family does not read.
        if (!DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
            return;
        }
        switch (TREE_CODE(decl)) {
            case TEMPLATE_DECL:
                template_decl(decl);
                break;
            case TYPE_DECL:
                if (DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                    class_type(TREE_TYPE(decl), false);
                } else {
                    alias(decl, false);
                }
                break;
            case VAR_DECL:
                if (!has_lang_template_info(decl)) {
                    variable(decl, false);
                }
                break;
            case FUNCTION_DECL:
                if (!has_lang_template_info(decl) || DECL_TEMPLATE_SPECIALIZATION(decl)) {
                    function(decl, false);
                }
                break;
            default:
                break;
        }
    }

    // A namespace that a library file opens, such as std, holds no
    // declaration that a rule of the family reads.
    void namespace_decl(tree ns) {
        if (!visited.insert(ns).second || (ns != global_namespace && in_library_file_at(DECL_SOURCE_LOCATION(ns)))) {
            return;
        }
        for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
            member(decl);
        }
    }
};

// The facts of the body of each function that is not a template, at
// PLUGIN_PRE_GENERICIZE (THE WALK).
void on_pre_genericize_property(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || !is_mint(function) || is_in_instantiation(function)
        || property_place(DECL_SOURCE_LOCATION(function)).file == nullptr) {
        return;
    }
    state.defined_functions.insert(DECL_UID(function));
    if (is_borrow_projection(DECL_SAVED_TREE(function))) {
        state.projection_bodies.insert(DECL_UID(function));
    }
}

}  // namespace

// One walk of every namespace that a system header does not own, for the
// contract rule and for the quarantine rule, and one walk for the family
// declaration property (THE WALK).
void walk_declarations() {
    walk_contract_namespace(global_namespace);
    walk_namespace(global_namespace);
    PropertyWalk properties;
    properties.namespace_decl(global_namespace);
    report_stubs(properties.stubs);
    report_lifetime_claims();
}

// Registers the attributes of the family, the hooks of the foreign symbols
// and of the facts of a body.
void register_declarations(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_ATTRIBUTES, on_register_attributes, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_variable, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_property, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_stub_declaration, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_lifetime_declaration, nullptr);
    register_callback(plugin_name, PLUGIN_START_PARSE_FUNCTION, on_start_function_property, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function_property, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_witness_type, nullptr);
}

}  // namespace crucible_plugin
