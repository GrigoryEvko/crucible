// The family expression shape: the walk of a body.  The walk reads each
// expression of a function body, of an initializer, of a default argument and
// of an unevaluated operand.  It gives the shapes of an expression their
// findings, such as a new-expression, an explicit temporary and an asm
// statement, and it gives each declaration that an expression names to the
// family owner.
//
// PLUGIN_PRE_GENERICIZE gives the body of each function that is not a
// template.  The body of a template instantiation is not read, because its
// pattern is read instead: a dependent name that only the instantiation
// resolves is not a named reference in the source.  An instantiation of a
// template of a quarantined file gives only the types that it deduces for the
// placeholders of its template (check_instantiation).  The function of the
// dynamic initializers of the unit is read too.
//
// An unevaluated operand makes no object, and each library name in it is a
// use: the operand of a sizeof, an alignof, a noexcept, a decltype, a type
// trait and a requires-expression, the condition of a static_assert, the
// definition of a concept and each requires-clause.  The walk does not read
// the operand of a reflection, because a query that the front end folds gives
// a reflection of an entity that the source does not spell.
//
// The plugin cannot see what the front end keeps as no tree, or as a tree with
// no source location: a dependent member of a template, and a
// using-declaration and a using-directive of namespace scope.  Outside a
// template, the front end folds a constant expression while it parses: an
// alignof, a noexcept, a type trait, a requires-expression, a static_assert,
// and the initializer of a variable when a call in it gives a constant, also a
// call of a builtin such as strlen.  Such a use is no finding.  The libstdc++
// assertions of the Debug, TSan and UBSan-strict presets stop some of these
// folds, so a call such as std::optional::operator-> in an initializer can be
// a finding in those presets and not in Release.  A constructor, a destructor
// and a conversion function of a library class are not findings: the compiler
// calls them for an object that the plugin reports where the code declares
// it.  The controls of the plugin witness in utils/plants/quarantine/ pin each
// such gap.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The walk of one body visits each node one time, so it stays linear when
// subtrees are shared.  A declaration is one shared node for all of its uses,
// so each expression reads the declarations that are its operands: each use
// then counts, at the location of the expression that holds it.
struct BodyWalk {
    location_t last = UNKNOWN_LOCATION;
    bool is_pattern = false;
    hash_set<tree>* visited = nullptr;
};

void walk_expression(tree* expression, BodyWalk& walk);

void check_new_expression(tree expression, location_t location) {
    bool is_placement = TREE_OPERAND(expression, 0) != NULL_TREE;
    tree type = TREE_OPERAND(expression, 1);
    std::string what = is_placement ? "placement new" : "new";
    if (type != NULL_TREE && TYPE_P(type)) {
        what += " " + type_text(type);
    }
    record(Kind::raw_new_delete, location, what);
}

// An explicit temporary, such as one that the code passes to a template: a
// TARGET_EXPR that a constructor initializes, whose range starts at the name
// of its class (spells_class_at).  The source names the class there, and each
// library class that the class holds is a std-entity, as a functional cast in
// a template is.  An implicit list initialization has the flag
// TARGET_EXPR_LIST_INIT_P, and a call that returns a class calls no
// constructor, so neither is a finding.  A TARGET_EXPR with no location of
// its own is no finding either.
void check_temporary(tree target) {
    if (TARGET_EXPR_LIST_INIT_P(target) || TARGET_EXPR_INITIAL(target) == NULL_TREE || !EXPR_HAS_LOCATION(target)) {
        return;
    }
    location_t location = EXPR_LOCATION(target);
    tree callee = cp_get_callee_fndecl_nofold(TARGET_EXPR_INITIAL(target));
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || !DECL_CONSTRUCTOR_P(callee)) {
        return;
    }
    const FileEntry* language = nullptr;
    LibraryClasses classes = library_classes_at(TREE_TYPE(target), location, Use::other, language);
    bool is_refused = is_standard_class(classes.refused);
    if ((!is_refused && classes.pending == NULL_TREE) || (language == nullptr && !is_quarantined(location))
        || !spells_class_at(location, TREE_TYPE(target))) {
        return;
    }
    if (is_refused) {
        record_library(Kind::std_entity, location, qualified_name(TYPE_MAIN_DECL(classes.refused)));
    }
    record_pending_class(classes, location);
}

// The closure type TYPE of a generic lambda: the pattern of its call operator.
// Outside a template, the body holds the closure object and no LAMBDA_EXPR at
// PLUGIN_PRE_GENERICIZE.  The call operator of a generic lambda is a template,
// and no PLUGIN_PRE_GENERICIZE reads a template, so the walk reads its pattern
// from the type of the closure.  The call operator of a lambda that is not
// generic has its own PLUGIN_PRE_GENERICIZE.  walk_body reads each function
// one time, also when many nodes have the type of one closure.
void check_closure(tree type) {
    if (type == NULL_TREE || !LAMBDA_TYPE_P(type)) {
        return;
    }
    tree call_operator = lambda_function(type);
    if (call_operator != NULL_TREE && generic_lambda_fn_p(call_operator)) {
        walk_body(call_operator, true);
    }
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
            check_foreign_declaration(decl);
        }
        check_closure(TREE_TYPE(decl));
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
    if (TREE_CODE(decl) == USING_DECL) {
        check_using(decl);
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
    if (EXPR_P(node) || TREE_CODE(node) == CONSTRUCTOR) {
        check_closure(TREE_TYPE(node));
    }
    switch (TREE_CODE(node)) {
        // An unevaluated operand makes no object and calls nothing, but each
        // library name in it is a use of that name.  cp_walk_tree walks the
        // operand of a sizeof, an alignof, a noexcept, a type trait, a
        // requires-expression and a static_assert, so the walk continues.  A
        // sizeof of a type outside a template keeps the type only as the type
        // of its operand.
        case SIZEOF_EXPR:
            if (SIZEOF_EXPR_TYPE_P(node)) {
                check_type_use(TREE_TYPE(TREE_OPERAND(node, 0)), walk.last);
                *walk_subtrees = 0;
                return NULL_TREE;
            }
            break;
        // The operand of a decltype is an expression, and cp_walk_tree walks it.
        case DECLTYPE_TYPE:
            return NULL_TREE;
        // A reflection is the operand of a query of <meta>.  A query that the
        // front end folds also gives a reflection, of an entity that the
        // source does not spell, so the walk reads the query and not its
        // operand.
        case REFLECT_EXPR:
            *walk_subtrees = 0;
            return NULL_TREE;
        // The type of an implicit conversion is not a name in the source.
        case IMPLICIT_CONV_EXPR:
            check_named_operands(node, walk.last);
            walk_expression(&TREE_OPERAND(node, 0), walk);
            *walk_subtrees = 0;
            return NULL_TREE;
        // A using-directive in a function body names its namespace.  The front
        // end also keeps a USING_STMT for each target of a using-declaration
        // that is no template, for the debug information, and check_using
        // reads the using-declaration itself.
        case USING_STMT:
            if (tree used = USING_STMT_NAMESPACE(node); used != NULL_TREE && TREE_CODE(used) == NAMESPACE_DECL) {
                record_namespace_name(used, std::string{}, walk.last);
            }
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
        case TARGET_EXPR:
            check_temporary(node);
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

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return;
    }
    if (is_in_instantiation(function)) {
        check_instantiation(function);
        return;
    }
    walk_body(function, false);
}

}  // namespace

// One walk of a body or an initializer, with its own set of visited nodes.
void walk_root(tree* root, location_t location, bool is_pattern) {
    hash_set<tree> visited;
    BodyWalk walk;
    walk.last = location;
    walk.is_pattern = is_pattern;
    walk.visited = &visited;
    walk_expression(root, walk);
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
    check_signature(function, is_pattern);
    if (is_pattern) {
        walk_constraints(function);
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function)) {
        return;
    }
    tree body = DECL_SAVED_TREE(function);
    if (body != NULL_TREE) {
        walk_root(&body, DECL_SOURCE_LOCATION(function), is_pattern);
    }
}

// Registers the walk of each function body.
void register_expressions(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
}

}  // namespace crucible_plugin
