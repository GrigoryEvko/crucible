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
//     file that expands the macro.  The module of a rule keeps the findings in
//     the scope of its row of utils/registry.txt.
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
//                gated=0|1 projection=0|1 defined=0|1 owner=CLASS|-
//
//     defined is 1 when the unit holds the body of the mint.  A declaration in
//     one header and the definition in a different header give two records,
//     and the module of the rule keeps the record with the body.  The owner is
//     the last fact, because the name of a class can hold a space.  The axes:
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
    if (file == nullptr) {
        return false;
    }
    if (file->file_class == FileClass::quarantined) {
        return true;
    }
    if (file->file_class != FileClass::base) {
        return false;
    }
    if (!state.sentinel.empty() && file->relative == state.sentinel) {
        return true;
    }
    return main_input_filename != nullptr && &classify_file(main_input_filename) == file;
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
    tree callee = TREE_CODE(node) == CALL_EXPR ? CALL_EXPR_FN(node)
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
    bool is_defined = is_pattern ? DECL_SAVED_TREE(function) != NULL_TREE
                                 : state.defined_functions.count(DECL_UID(function)) != 0;
    tree owner = DECL_CLASS_SCOPE_P(function) ? DECL_CONTEXT(function) : NULL_TREE;
    std::string entity = function_signature(function);
    auto flag = [](bool value) { return value ? "1" : "0"; };
    std::string facts = std::string(" | name=") + identifier_text(DECL_NAME(function))
                      + " static=" + flag(owner != NULL_TREE && !is_member) + " shape=" + shape
                      + " template=" + flag(is_template) + " nodiscard=" + flag(is_nodiscard)
                      + " constexpr=" + flag(is_constexpr) + " noexcept=" + flag(is_noexcept)
                      + " constrained=" + flag(is_constrained) + " gated=" + flag(is_gated)
                      + " projection=" + flag(is_projection) + " defined=" + flag(is_defined)
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

// ── The walk of the family ──────────────────────────────────────────────

struct PropertyWalk {
    std::unordered_set<tree> visited;

    // A pattern has template information, so the test of an instantiation
    // reads the scope of a pattern and not the pattern itself.
    void function(tree function, bool is_pattern) {
        if (!is_mint(function)) {
            return;
        }
        tree tested = is_pattern ? CP_DECL_CONTEXT(function) : function;
        if (!visited.insert(function).second || is_in_instantiation(tested)) {
            return;
        }
        check_mint(function, is_pattern);
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
        if (TREE_CODE(result) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(result) || !CLASS_TYPE_P(TREE_TYPE(result))) {
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
        for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
            switch (TREE_CODE(member)) {
                case FIELD_DECL:
                    if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                        class_type(TREE_TYPE(member), is_pattern);
                    }
                    break;
                case FUNCTION_DECL:
                    function(member, is_pattern);
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
        // function asks only when its name is the name of a mint (check_mint).
        // The walk then reads no place for each declaration of a file that the
        // family does not read.
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
                }
                break;
            case FUNCTION_DECL:
                if (is_mint(decl) && (!has_lang_template_info(decl) || DECL_TEMPLATE_SPECIALIZATION(decl))) {
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
        if (!visited.insert(ns).second
            || (ns != global_namespace && in_library_file_at(DECL_SOURCE_LOCATION(ns)))) {
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
}

// Registers the hooks of the foreign symbols and of the facts of a body.
void register_declarations(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_variable, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_property, nullptr);
}

}  // namespace crucible_plugin
