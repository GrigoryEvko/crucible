// The family door gate, the kind ungated-body of the rule must-consume: each
// public function template of the base that calls a body of its caller states
// the gate HoldsNoLinearValue of include/foundation/Linear.h.
//
// Why a gate
//     A door that calls a body ends no linear value that the body holds by
//     value, because a call is no consume.  The body ends after the call, in
//     the frame of the door or as a temporary of the caller, where no path of
//     the rule must-consume reads it.  So each such door states the concept
//     HoldsNoLinearValue on the type of the body, and a deleted twin gives the
//     reason of the refusal.  This family finds a door that states no gate.
//
// Doors
//     A door is a primary function template of a base file: a template at
//     namespace scope, a member template of a class or of a class template,
//     or a friend template that a class defines.  The family reads each door
//     of the header of the argument sentinel, in the sentinel unit of that
//     header (the sentinel of plugin.h), so each header costs one walk in each
//     build.  A base source file and a header with no sentinel give no line.
//     A door that a caller outside the base cannot name gets no line: a
//     private or protected member, a member of a private or protected nested
//     class, a declaration of a namespace detail or of an anonymous
//     namespace, and a function with a parameter of a class of a namespace
//     detail, a key that only the base can make.  A consteval function gets no
//     line, because it runs only in constant evaluation and ends no resource
//     at run time.  A function with the annotation once_door gets no line,
//     because it owns its closure and the kind capture-unconsumed reads the
//     call operator of that closure.
//
// Bodies
//     A body is a parameter whose type is a template type parameter of the
//     door itself, by value, by a reference, or as a pack, that the door uses
//     as a callable:
//       a call          the door calls the parameter, through a cast,
//                       std::forward, std::move, a reference, an index of a
//                       pack, std::invoke, or a capture in a lambda of its
//                       body
//       a pass          the door gives the parameter to a function of the base
//                       that calls its own parameter at that place.  Each
//                       candidate of the call that can take that count of
//                       arguments calls it, so an overload set with one other
//                       reading gives no line
//       a test          the constraints of the door or of a class template
//                       that holds it, or its return type, test the invocation
//                       of the type: the callable argument of a name with
//                       invocable or invoke_result in it (the first, and the
//                       second of a form with invocable_r, whose first
//                       argument is the result), a concept that
//                       reaches such a name, or a requires-expression that
//                       calls a parameter of the type.  A door that keeps its
//                       body in an object and calls it later, such as an
//                       adapter of a walk, states such a test
//     A temporary that the body makes from a type, such as Less{}, and a
//     function pointer of a template argument are no parameters, so they give
//     no line.  A parameter of an lvalue reference that is not const, such as
//     Body&, binds only an object that the caller names, and the rule
//     must-consume tracks that object in the caller, so it gives no line
//     either.  An explicit template argument const Body can bind a temporary
//     to it, and the rule does not read that route.
//
// Gate
//     The constraints of the door, or of a class template that holds it, hold
//     the gate when a conjunction reaches HoldsNoLinearValue with an argument
//     that names the type of the body, through the definition of each concept
//     and each fold of &&.  A disjunction holds the gate when each of its arms
//     does, and a negation holds none.  A concept with more than one argument
//     counts when the gate of its definition names the place of an argument
//     that names the type.
//
// Kinds
//     ungated-body    a door with a body that no gate holds.  The line stands
//                     at the name of the door, one line for each door, and the
//                     entity names the door and each such body.  The count
//                     ledger counts the lines of each header (is_file_keyed_kind
//                     of plugin.h), so a header whose count is zero refuses a
//                     new door with no gate at the compile, in the enforce mode
//                     error (the counted lines of plugin.cpp)
//
// Limits
//     A door that keeps its body in an object and tests no invocation of its
//     type gives no line.  A door that gives its body to a lambda by an
//     init-capture and calls the lambda gives no line, because the capture is
//     no parameter.  A body that a door reads through a type that erases it
//     gives no line.  The names of the test are names of the standard library
//     and of the concepts that reach them.
//
// Activation
//     The argument must-consume=on turns on the family with the rule
//     must-consume (plugin.cpp).

#include "plugin.h"

namespace crucible_plugin {

namespace {

bool is_active = false;

// The template of each definition that the unit parsed in a sentinel unit,
// in the order of the parse (on_finish_parse_function).
std::vector<tree> parsed_doors;

// The bodies of each function that the passes read, and the functions whose
// walk is open, so a cycle of passes ends.  The maps live for one walk at
// the end of the unit, after the last collection of the front end.
std::unordered_map<tree, std::unordered_set<tree>> called_bodies;
std::unordered_set<tree> open_walks;

bool is_named(tree identifier, const char* expected) {
    return identifier != NULL_TREE && TREE_CODE(identifier) == IDENTIFIER_NODE
        && std::strcmp(IDENTIFIER_POINTER(identifier), expected) == 0;
}

// The type with each reference and each pack expansion taken off, as its
// main variant.
tree bare_type(tree type) {
    for (int depth = 0; type != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(type) == REFERENCE_TYPE) {
            type = TREE_TYPE(type);
        } else if (TREE_CODE(type) == TYPE_PACK_EXPANSION) {
            type = PACK_EXPANSION_PATTERN(type);
        } else {
            return TYPE_MAIN_VARIANT(type);
        }
    }
    return type;
}

// The template type parameter of TYPE, or null.
tree type_parameter_of(tree type) {
    type = bare_type(type);
    return type != NULL_TREE && TREE_CODE(type) == TEMPLATE_TYPE_PARM ? type : NULL_TREE;
}

bool is_same_parameter(tree type, int level, int index) {
    return TEMPLATE_TYPE_LEVEL(type) == level && TEMPLATE_TYPE_IDX(type) == index;
}

// The identifier that the callee of CALL names, or null.
tree callee_name(tree call) {
    tree callee = CALL_EXPR_FN(call);
    for (int depth = 0; callee != NULL_TREE && depth < kTypeDepth; ++depth) {
        switch (TREE_CODE(callee)) {
            case TEMPLATE_ID_EXPR:
            case ADDR_EXPR:
                callee = TREE_OPERAND(callee, 0);
                continue;
            case OVERLOAD:
                callee = OVL_FIRST(callee);
                continue;
            case BASELINK:
                callee = BASELINK_FUNCTIONS(callee);
                continue;
            case SCOPE_REF:
            case COMPONENT_REF:
                callee = TREE_OPERAND(callee, 1);
                continue;
            case IDENTIFIER_NODE:
                return callee;
            case FUNCTION_DECL:
            case TEMPLATE_DECL:
                return DECL_NAME(callee);
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

bool is_cast_call(tree call) {
    tree name = callee_name(call);
    return call_expr_nargs(call) == 1 && (is_named(name, "forward") || is_named(name, "move"));
}

bool is_invoke_call(tree call) {
    tree name = callee_name(call);
    return call_expr_nargs(call) >= 1 && (is_named(name, "invoke") || is_named(name, "invoke_r"));
}

// The parameter of FUNCTION that OPERAND names, through each cast, a
// reference, std::forward, std::move, an index of a pack, an expansion and a
// capture of a lambda in the body, or null.
tree body_parameter(tree operand, tree function, int depth = 0) {
    if (operand == NULL_TREE || depth > kTypeDepth) {
        return NULL_TREE;
    }
    operand = tree_strip_any_location_wrapper(operand);
    switch (TREE_CODE(operand)) {
        case PARM_DECL:
            return DECL_CONTEXT(operand) == function ? operand : NULL_TREE;
        case VAR_DECL: {
            tree context = DECL_CONTEXT(operand);
            if (!DECL_HAS_VALUE_EXPR_P(operand) || context == NULL_TREE || TREE_CODE(context) != FUNCTION_DECL
                || DECL_LANG_SPECIFIC(context) == nullptr || !is_capture_proxy(operand)) {
                return NULL_TREE;
            }
            if (DECL_LANG_SPECIFIC(operand) != nullptr && DECL_CAPTURED_VARIABLE(operand) != NULL_TREE) {
                return body_parameter(DECL_CAPTURED_VARIABLE(operand), function, depth + 1);
            }
            return NULL_TREE;
        }
        case INDIRECT_REF:
        case NOP_EXPR:
        case CONVERT_EXPR:
        case VIEW_CONVERT_EXPR:
        case NON_LVALUE_EXPR:
        case STATIC_CAST_EXPR:
        case IMPLICIT_CONV_EXPR:
        case PAREN_EXPR:
        case CONST_CAST_EXPR:
        case ADDR_EXPR:
            return body_parameter(TREE_OPERAND(operand, 0), function, depth + 1);
        case EXPR_PACK_EXPANSION:
            return body_parameter(PACK_EXPANSION_PATTERN(operand), function, depth + 1);
        case PACK_INDEX_EXPR:
            return body_parameter(PACK_INDEX_PACK(operand), function, depth + 1);
        case CALL_EXPR:
            return is_cast_call(operand) ? body_parameter(CALL_EXPR_ARG(operand, 0), function, depth + 1) : NULL_TREE;
        default:
            return NULL_TREE;
    }
}

// The functions of a base file with a body that the callee of a call names.
void collect_candidates(tree callee, std::vector<tree>& candidates, int depth = 0) {
    if (callee == NULL_TREE || depth > kTypeDepth) {
        return;
    }
    switch (TREE_CODE(callee)) {
        case TEMPLATE_ID_EXPR:
        case ADDR_EXPR:
            collect_candidates(TREE_OPERAND(callee, 0), candidates, depth + 1);
            return;
        case BASELINK:
            collect_candidates(BASELINK_FUNCTIONS(callee), candidates, depth + 1);
            return;
        case COMPONENT_REF:
        case SCOPE_REF:
            collect_candidates(TREE_OPERAND(callee, 1), candidates, depth + 1);
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(callee, true); candidate; ++candidate) {
                collect_candidates(*candidate, candidates, depth + 1);
            }
            return;
        case TEMPLATE_DECL:
            collect_candidates(DECL_TEMPLATE_RESULT(callee), candidates, depth + 1);
            return;
        case FUNCTION_DECL:
            if (DECL_SAVED_TREE(callee) != NULL_TREE
                && place_of(DECL_SOURCE_LOCATION(callee), Scope::quarantine).file_class == FileClass::base) {
                candidates.push_back(callee);
            }
            return;
        default:
            return;
    }
}

// True when FUNCTION can take COUNT arguments: no fewer than its parameters
// with no default, and no more than its parameters unless one is a pack.
bool takes_count(tree function, int count) {
    int required = 0;
    for (tree types = FUNCTION_FIRST_USER_PARMTYPE(function); types != NULL_TREE && types != void_list_node;
         types = TREE_CHAIN(types)) {
        required += TREE_PURPOSE(types) == NULL_TREE ? 1 : 0;
    }
    int total = 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter)) {
        if (DECL_PACK_P(parameter)) {
            return count >= total;
        }
        ++total;
    }
    return count >= required && count <= total;
}

// The parameter of FUNCTION that takes the argument at POSITION.
tree parameter_at(tree function, int position) {
    int index = 0;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter), ++index) {
        if (DECL_PACK_P(parameter) || index == position) {
            return parameter;
        }
    }
    return NULL_TREE;
}

const std::unordered_set<tree>& bodies_of(tree function);

// True when each candidate of CALL that takes its count of arguments calls
// its parameter at POSITION, and one does (the pass of the bodies).
bool passes_to_caller(tree call, int position, tree function) {
    std::vector<tree> candidates;
    collect_candidates(CALL_EXPR_FN(call), candidates);
    int count = call_expr_nargs(call);
    bool is_called = false;
    for (tree candidate : candidates) {
        if (candidate == function || !takes_count(candidate, count)) {
            continue;
        }
        tree parameter = parameter_at(candidate, position);
        if (parameter == NULL_TREE || bodies_of(candidate).count(parameter) == 0) {
            return false;
        }
        is_called = true;
    }
    return is_called;
}

struct BodyWalk {
    tree function;
    std::unordered_set<tree>* bodies;
    hash_set<tree>* visited;
};

tree walk_body_node(tree* node, int* walk_subtrees, void* data);

void walk_tree_of(tree root, BodyWalk* walk) {
    if (root != NULL_TREE) {
        cp_walk_tree(&root, walk_body_node, walk, walk->visited);
    }
}

void note_call(tree call, BodyWalk* walk) {
    if (CALL_EXPR_FN(call) == NULL_TREE || is_cast_call(call)) {
        return;
    }
    bool is_invoke = is_invoke_call(call);
    tree called = body_parameter(is_invoke ? CALL_EXPR_ARG(call, 0) : CALL_EXPR_FN(call), walk->function);
    if (called != NULL_TREE && type_parameter_of(TREE_TYPE(called)) != NULL_TREE) {
        walk->bodies->insert(called);
    }
    if (is_invoke) {
        return;
    }
    for (int position = 0; position < call_expr_nargs(call); ++position) {
        tree passed = body_parameter(CALL_EXPR_ARG(call, position), walk->function);
        if (passed != NULL_TREE && walk->bodies->count(passed) == 0 && type_parameter_of(TREE_TYPE(passed)) != NULL_TREE
            && passes_to_caller(call, position, walk->function)) {
            walk->bodies->insert(passed);
        }
    }
}

// Each call of the body, also in a lambda of the body and in the initializer
// of a local, which a walk of the tree does not enter on its own.
tree walk_body_node(tree* node, int* /*walk_subtrees*/, void* data) {
    BodyWalk* walk = static_cast<BodyWalk*>(data);
    tree expression = *node;
    switch (TREE_CODE(expression)) {
        case CALL_EXPR:
            note_call(expression, walk);
            break;
        case LAMBDA_EXPR: {
            tree call_operator = lambda_function(expression);
            if (call_operator != NULL_TREE) {
                walk_tree_of(DECL_SAVED_TREE(call_operator), walk);
            }
            break;
        }
        case DECL_EXPR: {
            tree decl = DECL_EXPR_DECL(expression);
            if (decl != NULL_TREE && VAR_P(decl)) {
                walk_tree_of(DECL_INITIAL(decl), walk);
            }
            break;
        }
        default:
            break;
    }
    return NULL_TREE;
}

// The parameters of FUNCTION that its body calls or passes to a caller (the
// bodies).  Complexity: one walk of each function that a pass reaches.
const std::unordered_set<tree>& bodies_of(tree function) {
    static const std::unordered_set<tree> none;
    auto found = called_bodies.find(function);
    if (found != called_bodies.end()) {
        return found->second;
    }
    if (!open_walks.insert(function).second) {
        return none;
    }
    std::unordered_set<tree> bodies;
    hash_set<tree> visited;
    BodyWalk walk{function, &bodies, &visited};
    walk_tree_of(DECL_SAVED_TREE(function), &walk);
    open_walks.erase(function);
    return called_bodies.emplace(function, std::move(bodies)).first->second;
}

// True when NODE, a template argument or a type, names the template type
// parameter at LEVEL and INDEX.
bool names_parameter(tree node, int level, int index, int depth) {
    if (node == NULL_TREE || node == error_mark_node || depth > kTypeDepth) {
        return false;
    }
    if (TREE_CODE(node) == TREE_VEC) {
        for (int position = 0; position < TREE_VEC_LENGTH(node); ++position) {
            if (names_parameter(TREE_VEC_ELT(node, position), level, index, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (ARGUMENT_PACK_P(node)) {
        return names_parameter(ARGUMENT_PACK_ARGS(node), level, index, depth + 1);
    }
    if (PACK_EXPANSION_P(node)) {
        return names_parameter(PACK_EXPANSION_PATTERN(node), level, index, depth + 1);
    }
    if (!TYPE_P(node)) {
        return false;
    }
    if (TYPE_NAME(node) != NULL_TREE && TREE_CODE(TYPE_NAME(node)) == TYPE_DECL
        && TYPE_ALIAS_TEMPLATE_INFO(node) != NULL_TREE
        && names_parameter(TI_ARGS(TYPE_ALIAS_TEMPLATE_INFO(node)), level, index, depth + 1)) {
        return true;
    }
    switch (TREE_CODE(node)) {
        case TEMPLATE_TYPE_PARM:
            return is_same_parameter(node, level, index);
        case REFERENCE_TYPE:
        case POINTER_TYPE:
            return names_parameter(TREE_TYPE(node), level, index, depth + 1);
        case TYPENAME_TYPE:
            return names_parameter(TYPE_CONTEXT(node), level, index, depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE: {
            tree main_type = TYPE_MAIN_VARIANT(node);
            return CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE
                && names_parameter(CLASSTYPE_TI_ARGS(main_type), level, index, depth + 1);
        }
        default:
            return false;
    }
}

// The template argument at POSITION of ARGUMENTS, with a leading argument
// pack read as its elements, or null.
tree argument_at(tree arguments, int position) {
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC || TREE_VEC_LENGTH(arguments) == 0) {
        return NULL_TREE;
    }
    tree first = TREE_VEC_ELT(arguments, 0);
    if (ARGUMENT_PACK_P(first)) {
        return argument_at(ARGUMENT_PACK_ARGS(first), position);
    }
    return position < TREE_VEC_LENGTH(arguments) ? TREE_VEC_ELT(arguments, position) : NULL_TREE;
}

// True when DECL, a template or a concept, is a test of an invocation, a name
// with invocable or invoke_result in it, and its callable in ARGUMENTS names
// the template type parameter at LEVEL and INDEX.  The callable is the first
// argument, and the second of a form with invocable_r in its name, whose
// first argument is the result.
bool tests_callable(tree decl, tree arguments, int level, int index) {
    if (decl != NULL_TREE && TREE_CODE(decl) == OVERLOAD) {
        decl = OVL_FIRST(decl);
    }
    tree name = decl != NULL_TREE && DECL_P(decl) ? DECL_NAME(decl) : decl;
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE) {
        return false;
    }
    const char* text = IDENTIFIER_POINTER(name);
    if (std::strstr(text, "invocable") == nullptr && std::strstr(text, "invoke_result") == nullptr) {
        return false;
    }
    int position = std::strstr(text, "invocable_r") != nullptr ? 1 : 0;
    return names_parameter(argument_at(arguments, position), level, index, 0);
}

// The definition of the concept of a concept check, and the place in its
// parameter list of the argument at POSITION.
int concept_place(tree concept_template, int position) {
    int count = TREE_VEC_LENGTH(DECL_INNERMOST_TEMPLATE_PARMS(concept_template));
    return position < count ? position : count - 1;
}

bool tests_invocation(tree node, int level, int index, int depth);

bool type_tests_invocation(tree type, int level, int index) {
    if (TYPE_NAME(type) != NULL_TREE && TREE_CODE(TYPE_NAME(type)) == TYPE_DECL
        && TYPE_ALIAS_TEMPLATE_INFO(type) != NULL_TREE) {
        tree info = TYPE_ALIAS_TEMPLATE_INFO(type);
        if (tests_callable(TI_TEMPLATE(info), INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)), level, index)) {
            return true;
        }
    }
    tree context = TREE_CODE(type) == TYPENAME_TYPE ? TYPE_CONTEXT(type) : NULL_TREE;
    return context != NULL_TREE && CLASS_TYPE_P(context) && CLASSTYPE_TEMPLATE_INFO(context) != NULL_TREE
        && tests_callable(CLASSTYPE_TI_TEMPLATE(context), INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(context)), level,
                          index);
}

// True when NODE, a constraint, a requirement or a type, tests the invocation
// of the template type parameter at LEVEL and INDEX (the test of the bodies).
bool tests_invocation(tree node, int level, int index, int depth) {
    if (node == NULL_TREE || node == error_mark_node || depth > kTypeDepth * 2) {
        return false;
    }
    if (TYPE_P(node)) {
        return type_tests_invocation(node, level, index);
    }
    if (TREE_CODE(node) == TREE_VEC || TREE_CODE(node) == TREE_LIST) {
        bool is_vector = TREE_CODE(node) == TREE_VEC;
        int length = is_vector ? TREE_VEC_LENGTH(node) : 0;
        for (int position = 0; is_vector && position < length; ++position) {
            if (tests_invocation(TREE_VEC_ELT(node, position), level, index, depth + 1)) {
                return true;
            }
        }
        for (tree item = is_vector ? NULL_TREE : node; item != NULL_TREE; item = TREE_CHAIN(item)) {
            if (tests_invocation(TREE_VALUE(item), level, index, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (concept_check_p(node)) {
        tree concept_template = TREE_OPERAND(node, 0);
        tree arguments = TREE_OPERAND(node, 1);
        if (tests_callable(concept_template, arguments, level, index)) {
            return true;
        }
        tree definition = DECL_INITIAL(DECL_TEMPLATE_RESULT(concept_template));
        for (int position = 0; arguments != NULL_TREE && position < TREE_VEC_LENGTH(arguments); ++position) {
            if (names_parameter(TREE_VEC_ELT(arguments, position), level, index, 0)
                && tests_invocation(definition, 1, concept_place(concept_template, position), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (TREE_CODE(node) == TEMPLATE_ID_EXPR) {
        return tests_callable(TREE_OPERAND(node, 0), TREE_OPERAND(node, 1), level, index);
    }
    if (TREE_CODE(node) == CALL_EXPR && CALL_EXPR_FN(node) != NULL_TREE) {
        tree callee = CALL_EXPR_FN(node);
        tree type = TREE_TYPE(callee) != NULL_TREE ? type_parameter_of(TREE_TYPE(callee)) : NULL_TREE;
        for (int depth_of_cast = 0; type == NULL_TREE && callee != NULL_TREE && depth_of_cast < kTypeDepth;
             ++depth_of_cast) {
            callee = tree_strip_any_location_wrapper(callee);
            if (TREE_CODE(callee) == PARM_DECL) {
                type = type_parameter_of(TREE_TYPE(callee));
                break;
            }
            bool is_cast = TREE_CODE(callee) == INDIRECT_REF || TREE_CODE(callee) == NOP_EXPR
                        || TREE_CODE(callee) == STATIC_CAST_EXPR || TREE_CODE(callee) == NON_LVALUE_EXPR;
            callee = is_cast ? TREE_OPERAND(callee, 0) : NULL_TREE;
        }
        if (type != NULL_TREE && is_same_parameter(type, level, index)) {
            return true;
        }
    }
    if (TREE_CODE(node) == REQUIRES_EXPR) {
        return tests_invocation(REQUIRES_EXPR_REQS(node), level, index, depth + 1);
    }
    if (EXPR_P(node) || TREE_CODE(node) == SIMPLE_REQ || TREE_CODE(node) == COMPOUND_REQ
        || TREE_CODE(node) == NESTED_REQ) {
        for (int operand = 0; operand < TREE_OPERAND_LENGTH(node); ++operand) {
            if (tests_invocation(TREE_OPERAND(node, operand), level, index, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}

// The constraints of TEMPLATE_DECL, or null.
tree constraints_of(tree template_decl) {
    tree info = get_constraints(template_decl);
    return info != NULL_TREE ? CI_ASSOCIATED_CONSTRAINTS(info) : NULL_TREE;
}

// The templates whose constraints bind the door: the door, and each class
// template that holds it.
std::vector<tree> binding_templates(tree function) {
    std::vector<tree> templates{DECL_TI_TEMPLATE(function)};
    for (tree context = DECL_CONTEXT(function); context != NULL_TREE && TYPE_P(context);
         context = TYPE_CONTEXT(context)) {
        if (CLASS_TYPE_P(context) && CLASSTYPE_TEMPLATE_INFO(context) != NULL_TREE) {
            templates.push_back(CLASSTYPE_TI_TEMPLATE(context));
        }
    }
    return templates;
}

bool is_invocation_tested(tree function, int level, int index) {
    for (tree template_decl : binding_templates(function)) {
        if (tests_invocation(constraints_of(template_decl), level, index, 0)) {
            return true;
        }
    }
    tree type = TREE_TYPE(function);
    return type != NULL_TREE && FUNC_OR_METHOD_TYPE_P(type) && tests_invocation(TREE_TYPE(type), level, index, 0);
}

// True when the constraint NODE, read as a requirement, holds the gate for
// the template type parameter at LEVEL and INDEX (the gate).
bool holds_gate(tree node, int level, int index, int depth) {
    if (node == NULL_TREE || depth > kTypeDepth * 2) {
        return false;
    }
    node = tree_strip_any_location_wrapper(node);
    switch (TREE_CODE(node)) {
        case TRUTH_ANDIF_EXPR:
        case TRUTH_AND_EXPR:
            return holds_gate(TREE_OPERAND(node, 0), level, index, depth + 1)
                || holds_gate(TREE_OPERAND(node, 1), level, index, depth + 1);
        case TRUTH_ORIF_EXPR:
        case TRUTH_OR_EXPR:
            return holds_gate(TREE_OPERAND(node, 0), level, index, depth + 1)
                && holds_gate(TREE_OPERAND(node, 1), level, index, depth + 1);
        case PAREN_EXPR:
        case NON_LVALUE_EXPR:
        case NOP_EXPR:
            return holds_gate(TREE_OPERAND(node, 0), level, index, depth + 1);
        case UNARY_LEFT_FOLD_EXPR:
        case UNARY_RIGHT_FOLD_EXPR:
        case BINARY_LEFT_FOLD_EXPR:
        case BINARY_RIGHT_FOLD_EXPR:
            return FOLD_EXPR_OP(node) == TRUTH_ANDIF_EXPR && holds_gate(FOLD_EXPR_PACK(node), level, index, depth + 1);
        case EXPR_PACK_EXPANSION:
            return holds_gate(PACK_EXPANSION_PATTERN(node), level, index, depth + 1);
        default:
            break;
    }
    if (!concept_check_p(node)) {
        return false;
    }
    tree concept_template = TREE_OPERAND(node, 0);
    tree arguments = TREE_OPERAND(node, 1);
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC) {
        return false;
    }
    if (is_named(DECL_NAME(concept_template), "HoldsNoLinearValue")
        && in_base_namespace(CP_DECL_CONTEXT(concept_template))) {
        return names_parameter(arguments, level, index, 0);
    }
    tree definition = DECL_INITIAL(DECL_TEMPLATE_RESULT(concept_template));
    for (int position = 0; position < TREE_VEC_LENGTH(arguments); ++position) {
        if (names_parameter(TREE_VEC_ELT(arguments, position), level, index, 0)
            && holds_gate(definition, 1, concept_place(concept_template, position), depth + 1)) {
            return true;
        }
    }
    return false;
}

bool is_gated(tree function, int level, int index) {
    for (tree template_decl : binding_templates(function)) {
        if (holds_gate(constraints_of(template_decl), level, index, 0)) {
            return true;
        }
    }
    return false;
}

bool is_detail_namespace(tree scope) {
    return scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL
        && (DECL_NAME(scope) == NULL_TREE || is_named(DECL_NAME(scope), "detail"));
}

// True when a caller outside the base can name FUNCTION (the doors).
bool is_public_door(tree function) {
    if (TREE_PRIVATE(function) || TREE_PROTECTED(function) || DECL_IMMEDIATE_FUNCTION_P(function)
        || function_carries_base_annotation(function, get_identifier("once_door"), get_identifier("linear"))) {
        return false;
    }
    for (tree context = DECL_CONTEXT(function); context != NULL_TREE && context != global_namespace;) {
        if (TREE_CODE(context) == NAMESPACE_DECL) {
            if (is_detail_namespace(context)) {
                return false;
            }
            context = DECL_CONTEXT(context);
        } else if (TYPE_P(context)) {
            tree type_decl = TYPE_NAME(context);
            if (type_decl == NULL_TREE || !DECL_P(type_decl) || TREE_PRIVATE(type_decl) || TREE_PROTECTED(type_decl)) {
                return false;
            }
            context = CP_DECL_CONTEXT(type_decl);
        } else if (TREE_CODE(context) == FUNCTION_DECL) {
            return false;
        } else {
            break;
        }
    }
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter)) {
        tree type = bare_type(TREE_TYPE(parameter));
        if (type == NULL_TREE || !CLASS_TYPE_P(type) || TYPE_NAME(type) == NULL_TREE) {
            continue;
        }
        for (tree scope = CP_DECL_CONTEXT(TYPE_NAME(type));
             scope != NULL_TREE && scope != global_namespace && TREE_CODE(scope) == NAMESPACE_DECL;
             scope = DECL_CONTEXT(scope)) {
            if (is_detail_namespace(scope)) {
                return false;
            }
        }
    }
    return true;
}

// True when TYPE, the type of a parameter, is an lvalue reference that is not
// const to a template type parameter: it binds only an object that the caller
// names, and the rule must-consume tracks that object in the caller (the
// bodies).
bool is_named_object_of_caller(tree type) {
    return TYPE_REF_P(type) && !TYPE_REF_IS_RVALUE(type) && !TYPE_READONLY(TREE_TYPE(type))
        && TREE_CODE(TREE_TYPE(type)) == TEMPLATE_TYPE_PARM;
}

// The kind ungated-body for one function template (the doors and the gate).
void check_door(tree template_decl) {
    tree function = DECL_TEMPLATE_RESULT(template_decl);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || !PRIMARY_TEMPLATE_P(template_decl) || LAMBDA_FUNCTION_P(function) || is_erroneous_decl(function)) {
        return;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(template_decl), Scope::quarantine);
    if (place.file == nullptr || place.file_class != FileClass::base || !place.file->is_sentinel
        || !is_public_door(function)) {
        return;
    }
    int level = static_cast<int>(TMPL_PARMS_DEPTH(DECL_TEMPLATE_PARMS(template_decl)));
    const std::unordered_set<tree>& called = bodies_of(function);
    std::string ungated;
    for (tree parameter = FUNCTION_FIRST_USER_PARM(function); parameter != NULL_TREE;
         parameter = DECL_CHAIN(parameter)) {
        tree type = type_parameter_of(TREE_TYPE(parameter));
        if (type == NULL_TREE || TEMPLATE_TYPE_LEVEL(type) != level
            || is_named_object_of_caller(TREE_TYPE(parameter))) {
            continue;
        }
        int index = TEMPLATE_TYPE_IDX(type);
        bool is_body = called.count(parameter) != 0 || is_invocation_tested(function, level, index);
        if (!is_body || is_gated(function, level, index)) {
            continue;
        }
        ungated += (ungated.empty() ? "" : ", ") + identifier_text(DECL_NAME(parameter));
    }
    if (!ungated.empty()) {
        add_finding(Kind::linear_ungated_body, nullptr, place, qualified_name(function) + ": " + ungated);
    }
}

// PLUGIN_FINISH_PARSE_FUNCTION: the template of each definition of the unit
// whose pattern the definition is, in a sentinel unit.  A member of a partial
// specialization and a friend that a class defines come here too.
void on_finish_parse_function(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (state.sentinel.empty() || function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL
        || DECL_LANG_SPECIFIC(function) == nullptr || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return;
    }
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
        && DECL_TEMPLATE_RESULT(template_decl) == function) {
        parsed_doors.push_back(template_decl);
    }
}

// PLUGIN_GGC_MARKING: a template with no mark of the collection is free after
// it, so its record goes.  Complexity: linear in the records.
void on_marking(void*, void*) {
    parsed_doors.erase(std::remove_if(parsed_doors.begin(), parsed_doors.end(),
                                      [](tree template_decl) { return !ggc_marked_p(template_decl); }),
                       parsed_doors.end());
}

}  // namespace

void finish_door_gates() {
    if (!is_active) {
        return;
    }
    for (tree template_decl : parsed_doors) {
        check_door(template_decl);
    }
    parsed_doors.clear();
    called_bodies.clear();
    open_walks.clear();
}

void register_door_gate(const char* plugin_name) {
    is_active = true;
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_MARKING, on_marking, nullptr);
}

}  // namespace crucible_plugin
