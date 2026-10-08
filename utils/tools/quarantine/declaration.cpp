// The family declaration property: the walk of the declarations.  The type of
// each variable, data member, parameter and return type, each base, alias and
// default template argument, each foreign symbol and each allocation function
// gets its finding where the code declares it.  One walk of every namespace
// reads the declarations at PLUGIN_FINISH_UNIT, and the types that an
// instantiation deduces get their finding at PLUGIN_PRE_GENERICIZE.

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

// One walk of every namespace that a system header does not own, for the
// contract rule and for the quarantine rule.
void walk_declarations() {
    walk_contract_namespace(global_namespace);
    walk_namespace(global_namespace);
}

// Registers the hooks of the foreign symbols.
void register_declarations(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_variable, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
}

}  // namespace crucible_plugin
