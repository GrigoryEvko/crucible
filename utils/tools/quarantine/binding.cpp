// The family binding hook: the rule binding-hook.  The language finds the
// hook get<I> of a structured binding through argument-dependent lookup, so
// each unit can also call the hook by name.  A direct call is a second form of
// the access to a part.  A direct call on an rvalue moves one part out of the
// product and drops the other parts with no report.  The rule refuses each
// call of a hook outside the initializer of a binding.
//
// THE HOOKS
//     A hook is a function that carries the P3394 annotation
//     foundation::linear::binding_hook of include/foundation/Linear.h, also an
//     instance of such a function template.  The rule reads the annotation,
//     and it names no function.
//
// THE PERMITTED CALL
//     The front end gives each name of a binding of a tuple-like type a
//     variable of its own, with the flag DECL_DECOMPOSITION_P, and a reference
//     type.  Its initializer is the call of the hook, through conversions
//     only.  That call is the one permitted call.  A hook call in the
//     initializer of the hidden variable of a binding, such as auto [part] =
//     wrap(get<0>(record)), is a finding, because that variable is no name of
//     the binding.
//
// THE BODIES
//     PLUGIN_PRE_GENERICIZE gives the body of each function that is not a
//     template, and of each instance of a template.  The rule reads the bodies
//     that the rule must-consume reads (linear.cpp, THE BODIES): each body of
//     a quarantined file, a body of a base file in its sentinel unit, and an
//     instance of a template of a base or a quarantined file in each unit that
//     makes it.  The place of a finding is the place of the call.
//
// THE FOLDS
//     The front end replaces an initializer whose value is a constant with
//     that value, and the call goes from the tree.  A call of the hook of a
//     Record of empty tokens folds in this way.  The event
//     PLUGIN_FOLDED_INITIALIZER of the fifth patch of GCC gives the
//     initializer before the fold (linear.cpp, THE FOLDS), and the rule reads
//     each hook call in it.  The event of the initializer of a name of a
//     binding permits its call.  The rule reads the fold of each initializer,
//     also of a variable of namespace scope.
//
// THE LIMITS
//     The front end folds a constant expression while it parses, with no
//     event of the initializer: the condition of a static_assert, a template
//     argument and a bound of an array.  The rule does not see a hook call in
//     these places.  The control control-known-gaps of
//     utils/plants/binding-hook/ pins each of them.
//
// THE ERRORS
//     Each finding is a compile error in each build and each mode
//     (is_each_mode_kind of plugin.h), and an opt-out region covers it.  The
//     message does not start with "quarantine: ", so the driver of the
//     negative fixtures keeps it.  A test of the hook itself puts the call in
//     an opt-out region of the reason class PROBE:, and the rule quarantine
//     counts the opted-out finding with the reason of the region.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The identifiers that the family compares, interned one time.
struct Names {
    tree linear = NULL_TREE;
    tree binding_hook = NULL_TREE;
};

Names names;

// The answer of is_hook for each function, keyed by the declaration.  Each
// collection clears it, because a collection can free a tree and reuse its
// address.
std::unordered_map<tree, bool> hook_answers;

// True when FUNCTION is a hook (THE HOOKS): it carries the annotation, or it
// is an instance of a template whose pattern carries it.  Complexity: linear
// in the attributes of the function and of its template, one time for each
// function.
bool is_hook(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    auto found = hook_answers.find(function);
    if (found != hook_answers.end()) {
        return found->second;
    }
    bool answer = function_carries_base_annotation(function, names.binding_hook, names.linear);
    hook_answers.emplace(function, answer);
    return answer;
}

// True when DECL is the variable of a name of a binding (THE PERMITTED CALL).
bool is_binding_name(tree decl) {
    return decl != NULL_TREE && VAR_P(decl) && DECL_LANG_SPECIFIC(decl) != nullptr && DECL_DECOMPOSITION_P(decl)
        && !DECL_DECOMP_IS_BASE(decl);
}

// The call that NODE is through conversions, or null.
tree call_through_conversions(tree node) {
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
                node = TREE_OPERAND(node, 0);
                continue;
            case TARGET_EXPR:
                node = TARGET_EXPR_INITIAL(node);
                continue;
            case CALL_EXPR:
                return node;
            default:
                return NULL_TREE;
        }
    }
    return NULL_TREE;
}

// The state of one walk: the permitted calls, and the location of a call with
// no location of its own.
struct HookWalk {
    std::unordered_set<tree> permitted;
    location_t fallback = UNKNOWN_LOCATION;
};

void permit_initializer(HookWalk& walk, tree decl, tree initializer) {
    if (!is_binding_name(decl) || initializer == NULL_TREE) {
        return;
    }
    tree call = call_through_conversions(initializer);
    if (call != NULL_TREE) {
        walk.permitted.insert(call);
    }
}

void record_hook_call(tree call, tree callee, location_t fallback) {
    location_t location = EXPR_HAS_LOCATION(call) ? EXPR_LOCATION(call) : fallback;
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr) {
        return;
    }
    add_finding(Kind::binding_hook, nullptr, place, qualified_name(callee));
}

// The callback of cp_walk_tree.  A node comes before its operands, so the
// variable of a name of a binding permits its call before the walk reaches
// that call.
tree walk_hook_calls(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& walk = *static_cast<HookWalk*>(data);
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    switch (TREE_CODE(node)) {
        case INIT_EXPR:
            permit_initializer(walk, TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            break;
        case DECL_EXPR:
            if (VAR_P(DECL_EXPR_DECL(node))) {
                permit_initializer(walk, DECL_EXPR_DECL(node), DECL_INITIAL(DECL_EXPR_DECL(node)));
            }
            break;
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                if (VAR_P(variable)) {
                    permit_initializer(walk, variable, DECL_INITIAL(variable));
                }
            }
            break;
        case CALL_EXPR: {
            tree callee = cp_get_callee_fndecl_nofold(node);
            if (is_hook(callee) && walk.permitted.count(node) == 0) {
                record_hook_call(node, callee, walk.fallback);
            }
            break;
        }
        default:
            break;
    }
    return NULL_TREE;
}

// True when the rule reads the code of FUNCTION in this unit (THE BODIES).
// FUNCTION is null at namespace scope, where the place of the code decides.
bool is_read_here(tree function, location_t location) {
    if (function != NULL_TREE && is_in_instantiation(function)) {
        Place place = place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine);
        return place.file_class == FileClass::base || place.file_class == FileClass::quarantined;
    }
    Place place = place_of(function != NULL_TREE ? DECL_SOURCE_LOCATION(function) : location, Scope::quarantine);
    return place.file != nullptr && is_walked_file(place);
}

void on_pre_genericize(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    // A clone of a constructor or a destructor gets a second event on a copy
    // of a body that cp_genericize lowered (linear.cpp, on_pre_genericize_linear).
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    HookWalk walk;
    walk.fallback = DECL_SOURCE_LOCATION(function);
    tree body = DECL_SAVED_TREE(function);
    cp_walk_tree_without_duplicates(&body, walk_hook_calls, &walk);
}

// PLUGIN_FOLDED_INITIALIZER (THE FOLDS).  The event comes while the front end
// parses or instantiates the function current_function_decl, or a variable of
// namespace scope.
void on_folded_initializer(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->original == error_mark_node
        || !is_read_here(current_function_decl, data->location)) {
        return;
    }
    HookWalk walk;
    walk.fallback = data->location;
    if (data->kind == PLUGIN_FOLDED_VARIABLE) {
        permit_initializer(walk, data->object, data->original);
    }
    tree original = data->original;
    cp_walk_tree_without_duplicates(&original, walk_hook_calls, &walk);
}

void on_collection(void*, void*) { hook_answers.clear(); }

}  // namespace

void report_binding_hook(const Finding& finding) {
    error_at(finding.spelling,
             "this call names the hook %qs of a structured binding, and only a structured binding calls the hook",
             finding.entity.c_str());
    inform(finding.spelling,
           "a direct call is a second form of the access to a part, and a call on an rvalue moves one part out and "
           "drops the other parts with no report");
    inform(finding.spelling,
           "write %<auto [first, second] = record;%> or %<auto [first, second] = std::move(record);%>, which moves "
           "each part one time");
    inform(finding.spelling,
           "a test of the hook itself puts the call in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region "
           "of %<foundation/Quarantine.h%>");
}

void register_binding_hook(const char* plugin_name) {
    names.linear = get_identifier("linear");
    names.binding_hook = get_identifier("binding_hook");
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
