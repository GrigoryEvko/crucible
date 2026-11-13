// The family fresh brand, the rule fresh-brand: no caller names or deduces the
// fresh brand of a mint.
//
// THE FRESH BRAND
//     A mint takes its fresh brand from the default argument of a template
//     parameter: a closure type that CRUCIBLE_FRESH_BRAND or
//     CRUCIBLE_SCOPE_BRAND of include/foundation/Brand.h writes.  Each use of
//     the default makes a closure of its own, so each call site gets a brand
//     that no other site holds.  Each parameter of a function template whose
//     default argument is a closure type, or a value of a closure type, is a
//     fresh brand of the template (is_closure_default of plugin.h).
//
// THE ROUTES
//     A caller that gives a fresh brand an argument that it holds makes a
//     second value of one identity, such as a second exclusive token.  Three
//     routes give the parameter an argument that is no default:
//       an explicit template argument      mint<Tag, Held>(...)
//       a deduction from the function type the address of the mint with a
//                                          target type that names the brand,
//                                          and a conversion function template
//       an explicit specialization         its declared type, or its explicit
//                                          template arguments, name the brand
//     The two kinds close the three routes.  A specialization deduces the
//     arguments that it does not name from its declared type.
//
// THE KINDS
//     deduced-brand   the fresh brand stands in a deduced context of the
//                     function type: the return type or the type of a
//                     parameter.  A brand inside a nested name specifier, such
//                     as typename X<Brand>::type, inside a decltype or inside
//                     an expression is in no deduced context.  An alias that
//                     names the brand through such a context, such as
//                     foundation::brand::undeducible_brand_t of
//                     include/foundation/Brand.h, keeps it out of each
//                     deduction
//     named-brand     no parameter pack stands before the fresh brand in its
//                     template parameter list, so an explicit template
//                     argument reaches it.  A pack takes each explicit
//                     argument after it.  A constructor, a conversion function
//                     and a deduction guide take no explicit template argument
//     The entity is the qualified name of the template and the position of
//     the parameter in its list, from 0.
//
// THE TEMPLATES
//     Each function template of a file under the source root or of the build
//     directory: at namespace scope, a member template of a class or of a
//     class template, a friend template that a class declares, and the call
//     operator of a generic lambda.  The walk of the namespaces reads each
//     template that the unit only declares, and PLUGIN_FINISH_PARSE_FUNCTION
//     gives each definition, also of a lambda.
//
// THE ERRORS
//     Each finding is a compile error in each build and each mode
//     (is_each_mode_kind of plugin.h), at the first declaration of the
//     template.  The message does not start with "quarantine: ", so a regex of
//     a negative fixture reads it.  A test of an open route puts the template
//     in an opt-out region of the reason class PROBE:.
//
// THE ACTIVATION
//     The argument fresh-brand=on turns on the rule (plugin.cpp), and
//     Quarantine.cmake gives it to each compile of the tree.  A compile of a
//     plant of a different rule has no such argument.

#include "plugin.h"

namespace crucible_plugin {

namespace {

bool is_active = false;

// The templates that the rule read, cleared at each garbage collection,
// because a tree keys the set.  A template read again gives no new finding,
// because add_finding keeps one finding for each key.
std::unordered_set<tree> read_templates;

// True when the template parameter at LEVEL and INDEX stands in a deduced
// context of NODE, a type or a template argument.  A walk that reaches its
// bound fails closed.  Complexity: linear in the nodes of NODE.
bool deduces(tree node, int level, int index, int depth);

bool deduces_arguments(tree arguments, int level, int index, int depth) {
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC) {
        return false;
    }
    for (int position = 0; position < TREE_VEC_LENGTH(arguments); ++position) {
        if (deduces(TREE_VEC_ELT(arguments, position), level, index, depth + 1)) {
            return true;
        }
    }
    return false;
}

// True when VALUE, a value argument or an array bound, is the template
// parameter at LEVEL and INDEX itself, or that parameter less one, as GCC
// writes the largest index of an array of that bound.
bool is_value_parameter(tree value, int level, int index) {
    for (int depth = 0; value != NULL_TREE && depth < kTypeDepth; ++depth) {
        value = tree_strip_any_location_wrapper(value);
        switch (TREE_CODE(value)) {
            case NOP_EXPR:
            case CONVERT_EXPR:
            case IMPLICIT_CONV_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case MINUS_EXPR:
                value = TREE_OPERAND(value, 0);
                continue;
            case TEMPLATE_PARM_INDEX:
                return TEMPLATE_PARM_LEVEL(value) == level && TEMPLATE_PARM_IDX(value) == index;
            default:
                return false;
        }
    }
    return false;
}

bool is_type_parameter(tree type, int level, int index) {
    return TEMPLATE_TYPE_LEVEL(type) == level && TEMPLATE_TYPE_IDX(type) == index;
}

bool deduces(tree node, int level, int index, int depth) {
    if (node == NULL_TREE || node == error_mark_node) {
        return false;
    }
    if (depth > kTypeDepth) {
        return true;
    }
    if (ARGUMENT_PACK_P(node)) {
        return deduces_arguments(ARGUMENT_PACK_ARGS(node), level, index, depth);
    }
    if (PACK_EXPANSION_P(node)) {
        return deduces(PACK_EXPANSION_PATTERN(node), level, index, depth + 1);
    }
    if (TREE_CODE(node) == TREE_VEC) {
        return deduces_arguments(node, level, index, depth);
    }
    if (TREE_CODE(node) == TEMPLATE_DECL) {
        return DECL_TEMPLATE_TEMPLATE_PARM_P(node) && is_type_parameter(TREE_TYPE(node), level, index);
    }
    if (!TYPE_P(node)) {
        return is_value_parameter(node, level, index);
    }
    // An opaque alias keeps its own template arguments, and a deduction
    // compares them as the arguments of a class.
    if (dependent_opaque_alias_p(node)) {
        tree info = TYPE_ALIAS_TEMPLATE_INFO(node);
        return info != NULL_TREE && deduces_arguments(INNERMOST_TEMPLATE_ARGS(TI_ARGS(info)), level, index, depth);
    }
    switch (TREE_CODE(node)) {
        case TEMPLATE_TYPE_PARM:
        case TEMPLATE_TEMPLATE_PARM:
            return is_type_parameter(node, level, index);
        case BOUND_TEMPLATE_TEMPLATE_PARM:
            return is_type_parameter(node, level, index)
                || deduces_arguments(INNERMOST_TEMPLATE_ARGS(TYPE_TI_ARGS(node)), level, index, depth);
        case POINTER_TYPE:
        case REFERENCE_TYPE:
            return deduces(TREE_TYPE(node), level, index, depth + 1);
        case OFFSET_TYPE:
            return deduces(TYPE_OFFSET_BASETYPE(node), level, index, depth + 1)
                || deduces(TREE_TYPE(node), level, index, depth + 1);
        case ARRAY_TYPE: {
            tree domain = TYPE_DOMAIN(node);
            return deduces(TREE_TYPE(node), level, index, depth + 1)
                || (domain != NULL_TREE && is_value_parameter(TYPE_MAX_VALUE(domain), level, index));
        }
        case FUNCTION_TYPE:
        case METHOD_TYPE: {
            if (deduces(TREE_TYPE(node), level, index, depth + 1)) {
                return true;
            }
            for (tree parameter = TYPE_ARG_TYPES(node); parameter != NULL_TREE && parameter != void_list_node;
                 parameter = TREE_CHAIN(parameter)) {
                if (deduces(TREE_VALUE(parameter), level, index, depth + 1)) {
                    return true;
                }
            }
            return false;
        }
        case RECORD_TYPE:
        case UNION_TYPE:
        case ENUMERAL_TYPE: {
            if (TYPE_PTRMEMFUNC_P(node)) {
                return deduces(TYPE_PTRMEMFUNC_FN_TYPE(node), level, index, depth + 1);
            }
            tree main = TYPE_MAIN_VARIANT(node);
            if (!CLASS_TYPE_P(main) || CLASSTYPE_TEMPLATE_INFO(main) == NULL_TREE) {
                return false;
            }
            tree arguments = CLASSTYPE_TI_ARGS(main);
            if (TMPL_ARGS_HAVE_MULTIPLE_LEVELS(arguments)) {
                for (int level_index = 1; level_index <= TMPL_ARGS_DEPTH(arguments); ++level_index) {
                    if (deduces_arguments(TMPL_ARGS_LEVEL(arguments, level_index), level, index, depth)) {
                        return true;
                    }
                }
                return false;
            }
            return deduces_arguments(arguments, level, index, depth);
        }
        default:
            // TYPENAME_TYPE, DECLTYPE_TYPE, TYPEOF_TYPE, TRAIT_TYPE,
            // PACK_INDEX_TYPE, SPLICE_SCOPE, UNBOUND_CLASS_TEMPLATE and each
            // type with no part: no deduced context.
            return false;
    }
}

// True when a parameter pack stands before the position INDEX of the
// template parameter list PARAMETERS.
bool has_pack_before(tree parameters, int index) {
    for (int position = 0; position < index; ++position) {
        tree parameter = TREE_VALUE(TREE_VEC_ELT(parameters, position));
        if (parameter != NULL_TREE && parameter != error_mark_node && template_parameter_pack_p(parameter)) {
            return true;
        }
    }
    return false;
}

void record_fresh(Kind kind, tree template_decl, int index) {
    Place place = place_of(DECL_SOURCE_LOCATION(template_decl), Scope::tree);
    if (place.file == nullptr) {
        return;
    }
    add_finding(kind, nullptr, place,
                qualified_name(template_decl) + ", the template parameter " + std::to_string(index));
}

// The two kinds for one function template (THE KINDS).
void check_template(tree template_decl) {
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL
        || !read_templates.insert(template_decl).second) {
        return;
    }
    tree function = DECL_TEMPLATE_RESULT(template_decl);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || is_erroneous_decl(function)
        || in_library_file_at(DECL_SOURCE_LOCATION(template_decl))) {
        return;
    }
    // The front end gives a generic lambda with no capture a static function
    // and a conversion with the template parameters of its call operator.  The
    // call operator gives the finding.
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    tree levels = DECL_TEMPLATE_PARMS(template_decl);
    tree parameters = levels != NULL_TREE ? INNERMOST_TEMPLATE_PARMS(levels) : NULL_TREE;
    if (parameters == NULL_TREE) {
        return;
    }
    int level = static_cast<int>(TMPL_PARMS_DEPTH(levels));
    bool is_guide = deduction_guide_p(function);
    bool takes_explicit_arguments = !DECL_CONSTRUCTOR_P(function) && !DECL_CONV_FN_P(function) && !is_guide;
    tree type = TREE_TYPE(function);
    for (int index = 0; index < TREE_VEC_LENGTH(parameters); ++index) {
        tree entry = TREE_VEC_ELT(parameters, index);
        if (entry == NULL_TREE || entry == error_mark_node || !is_closure_default(TREE_PURPOSE(entry))) {
            continue;
        }
        bool is_deduced = false;
        if (type != NULL_TREE && FUNC_OR_METHOD_TYPE_P(type)) {
            is_deduced = !is_guide && deduces(TREE_TYPE(type), level, index, 0);
            tree parameter = TYPE_ARG_TYPES(type);
            if (TREE_CODE(type) == METHOD_TYPE && parameter != NULL_TREE) {
                parameter = TREE_CHAIN(parameter);
            }
            for (; !is_deduced && parameter != NULL_TREE && parameter != void_list_node;
                 parameter = TREE_CHAIN(parameter)) {
                is_deduced = deduces(TREE_VALUE(parameter), level, index, 0);
            }
        }
        if (is_deduced) {
            record_fresh(Kind::fresh_deduced_brand, template_decl, index);
        }
        if (takes_explicit_arguments && !has_pack_before(parameters, index)) {
            record_fresh(Kind::fresh_named_brand, template_decl, index);
        }
    }
}

void walk_fresh_class(tree type);

void walk_fresh_decl(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl) || is_erroneous_decl(decl)
        || in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case TEMPLATE_DECL: {
            tree result = DECL_TEMPLATE_RESULT(decl);
            if (result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL) {
                check_template(decl);
            } else if (result != NULL_TREE && TREE_CODE(result) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(result)
                       && CLASS_TYPE_P(TREE_TYPE(result))) {
                walk_fresh_class(TREE_TYPE(result));
            }
            break;
        }
        case TYPE_DECL:
            if (DECL_IMPLICIT_TYPEDEF_P(decl) && !DECL_SELF_REFERENCE_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                walk_fresh_class(TREE_TYPE(decl));
            }
            break;
        default:
            break;
    }
}

// The member templates of a class, its nested classes, and the friend
// templates that it declares.  An implicit instantiation is not walked: it
// shares each template parameter list with its pattern.
void walk_fresh_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type) || !read_templates.insert(main_type).second) {
        return;
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) != FIELD_DECL) {
            walk_fresh_decl(member);
        }
    }
    for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        if (TREE_PURPOSE(entry) == NULL_TREE && TREE_VALUE(entry) != NULL_TREE && DECL_P(TREE_VALUE(entry))) {
            walk_fresh_decl(TREE_VALUE(entry));
        }
    }
}

void walk_fresh_namespace(tree ns) {
    if (!read_templates.insert(ns).second) {
        return;
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        tree member = decl;
        if (TREE_CODE(member) == TREE_LIST) {
            member = TREE_VALUE(member);
            if (member == NULL_TREE || TREE_CODE(member) == TREE_LIST) {
                continue;
            }
        }
        if (TREE_CODE(member) == OVERLOAD) {
            for (ovl_iterator candidate(member, true); candidate; ++candidate) {
                walk_fresh_decl(*candidate);
            }
            continue;
        }
        if (TREE_CODE(member) == NAMESPACE_DECL) {
            if (DECL_NAMESPACE_ALIAS(member) == NULL_TREE) {
                walk_fresh_namespace(member);
            }
            continue;
        }
        walk_fresh_decl(member);
    }
}

// PLUGIN_FINISH_PARSE_FUNCTION: the template of each function definition, also
// of the call operator of a generic lambda.
void on_finish_parse_function(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return;
    }
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
        && DECL_TEMPLATE_RESULT(template_decl) == function) {
        check_template(template_decl);
    }
}

void on_collection(void*, void*) { read_templates.clear(); }

}  // namespace

void walk_fresh_brands() {
    if (is_active) {
        walk_fresh_namespace(global_namespace);
    }
}

void report_fresh_brand(const Finding& finding) {
    const char* entity = finding.entity.c_str();
    switch (finding.kind) {
        case Kind::fresh_deduced_brand:
            error_at(finding.spelling,
                     "a fresh brand stands in a deduced context of the function type, so a caller can deduce a brand "
                     "that it holds: %s",
                     entity);
            inform(finding.spelling,
                   "the address of the function with a target type, a conversion or an explicit specialization "
                   "names the brand and makes a second value of one identity");
            inform(finding.spelling,
                   "write the brand through %<foundation::brand::undeducible_brand_t%> of %<foundation/Brand.h%>, "
                   "which keeps it out of each deduction");
            break;
        case Kind::fresh_named_brand:
            error_at(finding.spelling,
                     "no parameter pack stands before a fresh brand, so an explicit template argument names a brand "
                     "that the caller holds: %s",
                     entity);
            inform(finding.spelling,
                   "put a pack %<class... Refused%> before the brand and require that the pack is empty, so the "
                   "pack takes each explicit argument");
            break;
        default:
            return;
    }
    inform(finding.spelling,
           "a test of an open route puts the template in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> "
           "region of %<foundation/Quarantine.h%>");
}

void register_fresh_brand(const char* plugin_name) {
    is_active = true;
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
