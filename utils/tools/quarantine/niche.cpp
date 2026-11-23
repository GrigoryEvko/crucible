// The kind niche-access of the rule proof-routes.
//
// A class that states its niche (the check of the statement of
// foundation/core/Choice.h) gets no test of its payload in the storage of a
// full Option.  The walk of the statement reads the members of the class and
// of its nested classes through reflection.  GCC 16 reflection gives no
// friend of a class and no body of a function.  So a friend function, a
// member of a friend class, a member of a nested class, and a closure or a
// local class in a body of the class, could build or write the class through
// a member that is not public, and the walk would not see it.  If such a body
// writes the empty word, a full Option reads as empty, a silent wrong value.
//
// Route
//     niche-access          a body that has the access of a class that
//                           states its niche, is not a member function of
//                           the class, takes no niche key, and does one of:
//                             - writes a field of the class (an assignment,
//                               an increment, or the address of the field as
//                               a pointer or a reference that is not const)
//                             - calls a constructor of the class that is not
//                               public, other than copy and move
//                             - calls a member function of the class that is
//                               not public and that is static or not const,
//                               other than the destructor, the copy and the
//                               move assignment
//                             - names a member of the class that is not
//                               public in a pointer to a member
//                             - holds a constant of the class that the front
//                               end folded, so no call names its constructor.
//     A const friend that only reads gives no route.  A function that takes
//     a niche_key<U> const& of foundation/core/Choice.h is a niche door: only
//     the storage of an Option builds a key, so its body may build the empty
//     value.  The entity is `niche-access: CLASS SHAPE in FUNCTION`.
//
// Hooks
//     PLUGIN_PRE_GENERICIZE  each body that a rule of each body reads in the
//                            unit (is_body_read_here of plugin.h): each
//                            instance of a template of a base or a
//                            quarantined file in each unit that makes it, and
//                            each other body of a walked file.  The census
//                            keeps one line for each place, so the instances
//                            of one pattern in many units give one finding.
//     the end of the unit    the sites of the classes that state a niche:
//                            the class has its own member
//                            is_niche_empty_never_built, or a specialization
//                            of foundation::core::niche for the class, or a
//                            partial specialization for its class template,
//                            has the member is_empty_never_built.  A hidden
//                            friend is read at the end of its class, before
//                            the specialization of the niche that follows the
//                            class, so the judge waits for the end.
//
// Retirement
//     The kind retires when GCC gives the friends of a class and the bodies of
//     functions to reflection, so the check of the statement reads them.  The
//     type change for one finding: make the body a member door of the class
//     that takes a voucher, which the walk of the statement reads, or give the
//     body a niche key when it is a niche door.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The shape of one access of a body to a member of a class.
enum class AccessShape : std::uint8_t {
    field_write,
    hidden_constructor,
    hidden_member,
    member_pointer,
    folded_constant,
};

// One access that the end of the unit judges.  OWNER is the main variant of
// the class, MEMBER the name of the field or the member, and FUNCTION the name
// of the function of the body.
struct AccessSite {
    tree owner = NULL_TREE;
    location_t location = UNKNOWN_LOCATION;
    AccessShape shape = AccessShape::field_write;
    std::string member;
    std::string function;
};

// The sites of the unit.  A site holds the tree of its class, so the marking
// of a collection drops the site of a class that the collection frees
// (on_marking).  A class that a collection frees states nothing that the end
// of the unit can read.
std::vector<AccessSite> access_sites;

// The state of the walk of one body.
struct NicheWalk {
    tree function = NULL_TREE;
    std::string function_name;
    hash_set<tree>* read_addresses = nullptr;
    hash_set<tree>* visited = nullptr;
};

// The phrase of a shape and its member in the entity of a finding.
std::string shape_phrase(AccessShape shape, const std::string& member) {
    switch (shape) {
        case AccessShape::field_write:
            return "writes the field " + member;
        case AccessShape::hidden_constructor:
            return "calls a constructor that is not public";
        case AccessShape::hidden_member:
            return "calls the member " + member + ", which is not public";
        case AccessShape::member_pointer:
            return "takes a pointer to the member " + member;
        case AccessShape::folded_constant:
            return "holds a constant that the compiler folded";
    }
    return "accesses " + member;
}

// True when TYPE, with no reference and no cv, is a specialization of
// foundation::core::niche_key.  Complexity: constant.
bool is_niche_key_type(tree type) {
    if (type == NULL_TREE || type == error_mark_node) {
        return false;
    }
    if (TREE_CODE(type) == REFERENCE_TYPE) {
        type = TREE_TYPE(type);
    }
    type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(type) || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return false;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(type);
    if (template_decl == NULL_TREE || DECL_NAME(template_decl) == NULL_TREE
        || identifier_text(DECL_NAME(template_decl)) != "niche_key") {
        return false;
    }
    tree core = CP_DECL_CONTEXT(template_decl);
    if (core == NULL_TREE || TREE_CODE(core) != NAMESPACE_DECL || DECL_NAME(core) == NULL_TREE
        || identifier_text(DECL_NAME(core)) != "core") {
        return false;
    }
    tree foundation = CP_DECL_CONTEXT(core);
    return foundation != NULL_TREE && TREE_CODE(foundation) == NAMESPACE_DECL && DECL_NAME(foundation) != NULL_TREE
        && identifier_text(DECL_NAME(foundation)) == "foundation" && CP_DECL_CONTEXT(foundation) == global_namespace;
}

// True when FUNCTION takes a niche key, so it is a niche door.  Complexity:
// linear in the parameters.
bool takes_niche_key(tree function) {
    for (tree parameter = TYPE_ARG_TYPES(TREE_TYPE(function)); parameter != NULL_TREE && parameter != void_list_node;
         parameter = TREE_CHAIN(parameter)) {
        if (is_niche_key_type(TREE_VALUE(parameter))) {
            return true;
        }
    }
    return false;
}

// The class of a member, or null.
tree owner_of(tree member) {
    tree context = member != NULL_TREE ? DECL_CONTEXT(member) : NULL_TREE;
    if (context == NULL_TREE || !TYPE_P(context)) {
        return NULL_TREE;
    }
    context = TYPE_MAIN_VARIANT(context);
    return CLASS_TYPE_P(context) ? context : NULL_TREE;
}

// True when a pointer or a reference of TYPE reads only: its target is const.
bool points_to_const(tree type) {
    if (type == NULL_TREE || (TREE_CODE(type) != POINTER_TYPE && TREE_CODE(type) != REFERENCE_TYPE)) {
        return false;
    }
    return CP_TYPE_CONST_P(TREE_TYPE(type));
}

// Records one access of the body of the walk to a member of OWNER.  A member
// function of OWNER is the code of the author of the class, which the walk of
// the statement reads as a door.  A class of a library file and a closure
// state no niche, so they get no site.
void note_access(const NicheWalk& walk, tree owner, location_t location, AccessShape shape, tree member) {
    if (owner == NULL_TREE || LAMBDA_TYPE_P(owner)) {
        return;
    }
    tree owner_decl = TYPE_MAIN_DECL(owner);
    if (owner_decl == NULL_TREE || in_library_file_at(DECL_SOURCE_LOCATION(owner_decl))) {
        return;
    }
    if (owner_of(walk.function) == owner) {
        return;
    }
    AccessSite site;
    site.owner = owner;
    site.location = location != UNKNOWN_LOCATION ? location : DECL_SOURCE_LOCATION(walk.function);
    site.shape = shape;
    site.member = member != NULL_TREE && DECL_NAME(member) != NULL_TREE ? identifier_text(DECL_NAME(member)) : "";
    site.function = walk.function_name;
    access_sites.push_back(std::move(site));
}

// Records each field that the place TARGET names: the field of each
// COMPONENT_REF of the chain, through an element of an array.  The chain ends
// at a dereference, because a write through a pointer field writes its
// target and not the field.  A base field and a field of the front end are
// skipped.  Complexity: linear in the length of the chain.
void note_written_fields(const NicheWalk& walk, tree target, location_t location) {
    for (int depth = 0; target != NULL_TREE && depth < kTypeDepth; ++depth) {
        switch (TREE_CODE(target)) {
            case COMPONENT_REF: {
                tree field = TREE_OPERAND(target, 1);
                if (field != NULL_TREE && TREE_CODE(field) == FIELD_DECL && !DECL_ARTIFICIAL(field)) {
                    note_access(walk, owner_of(field), location, AccessShape::field_write, field);
                }
                target = TREE_OPERAND(target, 0);
                break;
            }
            case ARRAY_REF:
            case ARRAY_RANGE_REF:
            case REALPART_EXPR:
            case IMAGPART_EXPR:
            case VIEW_CONVERT_EXPR:
            case NON_LVALUE_EXPR:
            case NOP_EXPR:
                target = TREE_OPERAND(target, 0);
                break;
            default:
                return;
        }
    }
}

// The function that a call or a construction names, as its origin when the
// front end gives a clone, or null.
tree callee_of(tree call) {
    tree callee = NULL_TREE;
    if (TREE_CODE(call) == AGGR_INIT_EXPR) {
        tree function = AGGR_INIT_EXPR_FN(call);
        if (function != NULL_TREE && TREE_CODE(function) == ADDR_EXPR) {
            callee = TREE_OPERAND(function, 0);
        }
    } else {
        callee = cp_get_callee_fndecl_nofold(call);
    }
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL) {
        return NULL_TREE;
    }
    return DECL_CLONED_FUNCTION_P(callee) ? DECL_CLONED_FUNCTION(callee) : callee;
}

// Marks each address that the call passes to a parameter that is a pointer
// or a reference to const, also the object of a const member function: the
// callee only reads through it.  The walk visits the call before its
// arguments, so the mark comes first.  Complexity: linear in the arguments.
void mark_read_arguments(const NicheWalk& walk, tree call, tree callee) {
    bool const is_aggregate = TREE_CODE(call) == AGGR_INIT_EXPR;
    int const count = is_aggregate ? aggr_init_expr_nargs(call) : call_expr_nargs(call);
    tree parameter = TYPE_ARG_TYPES(TREE_TYPE(callee));
    // A construction holds its object in its slot, and not in its arguments.
    if (is_aggregate && AGGR_INIT_VIA_CTOR_P(call) && parameter != NULL_TREE) {
        parameter = TREE_CHAIN(parameter);
    }
    for (int index = 0; index < count && parameter != NULL_TREE && parameter != void_list_node;
         ++index, parameter = TREE_CHAIN(parameter)) {
        if (!points_to_const(TREE_VALUE(parameter))) {
            continue;
        }
        tree argument = is_aggregate ? AGGR_INIT_EXPR_ARG(call, index) : CALL_EXPR_ARG(call, index);
        STRIP_NOPS(argument);
        if (argument != NULL_TREE && TREE_CODE(argument) == ADDR_EXPR) {
            walk.read_addresses->add(argument);
        }
    }
}

// Records a call of a constructor or of a member function of a class that is
// not public, which can build or write the class: a constructor other than
// copy and move, and a member function that is static or not const, other
// than the destructor, the copy and the move assignment.
void note_call(const NicheWalk& walk, tree call) {
    tree callee = callee_of(call);
    if (callee == NULL_TREE) {
        return;
    }
    mark_read_arguments(walk, call, callee);
    if (!DECL_FUNCTION_MEMBER_P(callee) || (!TREE_PRIVATE(callee) && !TREE_PROTECTED(callee))) {
        return;
    }
    location_t location = EXPR_HAS_LOCATION(call) ? EXPR_LOCATION(call) : UNKNOWN_LOCATION;
    if (DECL_CONSTRUCTOR_P(callee)) {
        if (!DECL_COPY_CONSTRUCTOR_P(callee) && !DECL_MOVE_CONSTRUCTOR_P(callee)) {
            note_access(walk, owner_of(callee), location, AccessShape::hidden_constructor, NULL_TREE);
        }
        return;
    }
    bool const is_copy_or_move = DECL_ASSIGNMENT_OPERATOR_P(callee) && (copy_fn_p(callee) != 0 || move_fn_p(callee));
    // A const member function with an implicit object parameter only reads.
    // A member function with an explicit object parameter counts as a
    // writer, so the walk fails closed for it.
    bool const is_reader = DECL_CONST_MEMFUNC_P(callee);
    if (DECL_DESTRUCTOR_P(callee) || is_copy_or_move || is_reader) {
        return;
    }
    note_access(walk, owner_of(callee), location, AccessShape::hidden_member, callee);
}

// True when the class has a constructor that is not public, other than copy
// and move.  Complexity: linear in the members.
bool has_hidden_constructor(tree owner) {
    for (tree member = TYPE_FIELDS(owner); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || !DECL_CONSTRUCTOR_P(function)) {
            continue;
        }
        bool const is_hidden = TREE_PRIVATE(member) || TREE_PROTECTED(member);
        if (is_hidden && !DECL_COPY_CONSTRUCTOR_P(function) && !DECL_MOVE_CONSTRUCTOR_P(function)) {
            return true;
        }
    }
    return false;
}

// True when FUNCTION has the access of OWNER: a friend of it, or a body in a
// scope of it, such as a member of a nested class or a closure in a member.
// Complexity: linear in the depth of the scopes and in the friends of OWNER.
bool has_access_of(tree function, tree owner) {
    if (is_friend(owner, function) != 0) {
        return true;
    }
    tree scope = DECL_CONTEXT(function);
    for (int depth = 0; scope != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TYPE_P(scope)) {
            if (TYPE_MAIN_VARIANT(scope) == owner) {
                return true;
            }
            scope = TYPE_CONTEXT(TYPE_MAIN_VARIANT(scope));
        } else if (TREE_CODE(scope) == FUNCTION_DECL) {
            scope = DECL_CONTEXT(scope);
        } else {
            return false;
        }
    }
    return false;
}

tree visit_niche_node(tree* node, int* walk_subtrees, void* data);

// The walk of one expression of the body, with the set of visited nodes of
// the body.
void walk_niche_expression(tree* expression, NicheWalk& walk) {
    if (expression != nullptr && *expression != NULL_TREE) {
        cp_walk_tree(expression, visit_niche_node, &walk, walk.visited);
    }
}

tree visit_niche_node(tree* node, int* walk_subtrees, void* data) {
    NicheWalk& walk = *static_cast<NicheWalk*>(data);
    tree current = *node;
    if (TYPE_P(current)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    location_t location = EXPR_P(current) && EXPR_HAS_LOCATION(current) ? EXPR_LOCATION(current) : UNKNOWN_LOCATION;
    switch (TREE_CODE(current)) {
        case MODIFY_EXPR:
        case INIT_EXPR:
        case PREINCREMENT_EXPR:
        case PREDECREMENT_EXPR:
        case POSTINCREMENT_EXPR:
        case POSTDECREMENT_EXPR:
            note_written_fields(walk, TREE_OPERAND(current, 0), location);
            break;
        case NOP_EXPR:
        case CONVERT_EXPR: {
            // A conversion of an address to a pointer or a reference to const
            // reads, as the object of a call of a const member does.
            tree operand = TREE_OPERAND(current, 0);
            if (operand != NULL_TREE && TREE_CODE(operand) == ADDR_EXPR && points_to_const(TREE_TYPE(current))) {
                walk.read_addresses->add(operand);
            }
            break;
        }
        case ADDR_EXPR:
            if (!walk.read_addresses->contains(current) && !points_to_const(TREE_TYPE(current))) {
                note_written_fields(walk, TREE_OPERAND(current, 0), location);
            }
            break;
        case CALL_EXPR:
        case AGGR_INIT_EXPR:
            note_call(walk, current);
            break;
        case PTRMEM_CST: {
            tree member = PTRMEM_CST_MEMBER(current);
            if (member != NULL_TREE && DECL_P(member) && (TREE_PRIVATE(member) || TREE_PROTECTED(member))) {
                note_access(walk, owner_of(member), location, AccessShape::member_pointer, member);
            }
            break;
        }
        case DECL_EXPR: {
            // cp_walk_tree does not read the initializer of a local outside
            // template processing.  A constant initializer that the front end
            // folded names no constructor.  When the class has a constructor
            // that is not public and the body has the access of the class, the
            // constant can come from that constructor, so it is a site.
            tree decl = DECL_EXPR_DECL(current);
            if (decl != NULL_TREE && VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
                tree initial = DECL_INITIAL(decl);
                tree type = TYPE_MAIN_VARIANT(TREE_TYPE(decl));
                if (TREE_CODE(initial) == CONSTRUCTOR && CLASS_TYPE_P(type) && has_hidden_constructor(type)
                    && has_access_of(walk.function, type)) {
                    note_access(walk, type, DECL_SOURCE_LOCATION(decl), AccessShape::folded_constant, NULL_TREE);
                }
                walk_niche_expression(&DECL_INITIAL(decl), walk);
            }
            break;
        }
        default:
            break;
    }
    return NULL_TREE;
}

// PLUGIN_PRE_GENERICIZE: the sites of one body that a rule of each body reads
// in this unit.  A niche door gives no site.
void on_pre_genericize_niche(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || !is_body_read_here(function, DECL_SOURCE_LOCATION(function)) || takes_niche_key(function)) {
        return;
    }
    hash_set<tree> read_addresses;
    hash_set<tree> visited;
    NicheWalk walk;
    walk.function = function;
    walk.function_name = qualified_name(function);
    walk.read_addresses = &read_addresses;
    walk.visited = &visited;
    walk_niche_expression(&DECL_SAVED_TREE(function), walk);
}

// PLUGIN_GGC_MARKING: each site of a class that the collection frees goes.
void on_niche_marking(void*, void*) {
    access_sites.erase(std::remove_if(access_sites.begin(), access_sites.end(),
                                      [](const AccessSite& site) { return !ggc_marked_p(site.owner); }),
                       access_sites.end());
}

// The member NAME of CLASS_TYPE: a static data member, or a using-declaration
// that brings one into the class.  Complexity: linear in the members.
bool has_statement_member(tree class_type, tree name) {
    for (tree member = TYPE_FIELDS(class_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if ((VAR_P(member) || TREE_CODE(member) == USING_DECL) && DECL_NAME(member) == name) {
            return true;
        }
    }
    return false;
}

// The classes that state their niche through a specialization of
// foundation::core::niche: the class of each explicit specialization, and the
// most general template of the class of each partial specialization.
struct NicheStatements {
    std::unordered_set<tree> classes;
    std::unordered_set<tree> class_templates;
};

// The template foundation::core::niche, or null when the unit does not
// declare it.
tree niche_template() {
    tree foundation = lookup_qualified_name(global_namespace, get_identifier("foundation"), LOOK_want::NORMAL, false);
    if (foundation == NULL_TREE || foundation == error_mark_node || TREE_CODE(foundation) != NAMESPACE_DECL) {
        return NULL_TREE;
    }
    tree core = lookup_qualified_name(foundation, get_identifier("core"), LOOK_want::NORMAL, false);
    if (core == NULL_TREE || core == error_mark_node || TREE_CODE(core) != NAMESPACE_DECL) {
        return NULL_TREE;
    }
    tree niche = lookup_qualified_name(core, get_identifier("niche"), LOOK_want::NORMAL, false);
    bool const is_class_template =
        niche != NULL_TREE && TREE_CODE(niche) == TEMPLATE_DECL && DECL_CLASS_TEMPLATE_P(niche);
    return is_class_template ? niche : NULL_TREE;
}

// The first template argument of ARGUMENTS, when it is a type.
tree first_type_argument(tree arguments) {
    tree innermost = arguments != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(arguments) : NULL_TREE;
    if (innermost == NULL_TREE || TREE_VEC_LENGTH(innermost) == 0) {
        return NULL_TREE;
    }
    tree argument = TREE_VEC_ELT(innermost, 0);
    return argument != NULL_TREE && TYPE_P(argument) ? TYPE_MAIN_VARIANT(argument) : NULL_TREE;
}

// The statements of the specializations of the niche in this unit.
// Complexity: linear in the specializations and their members.
NicheStatements read_niche_statements() {
    NicheStatements statements;
    tree niche = niche_template();
    if (niche == NULL_TREE) {
        return statements;
    }
    tree name = get_identifier("is_empty_never_built");
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(niche); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree specialization = TREE_VALUE(entry);
        tree argument = first_type_argument(TREE_PURPOSE(entry));
        if (specialization != NULL_TREE && TYPE_P(specialization) && CLASS_TYPE_P(specialization)
            && argument != NULL_TREE && has_statement_member(specialization, name)) {
            statements.classes.insert(argument);
        }
    }
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(niche); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial == NULL_TREE || TREE_CODE(partial) != TEMPLATE_DECL) {
            continue;
        }
        tree pattern = TREE_TYPE(DECL_TEMPLATE_RESULT(partial));
        tree argument = first_type_argument(TREE_PURPOSE(entry));
        if (pattern == NULL_TREE || !CLASS_TYPE_P(pattern) || !has_statement_member(pattern, name)
            || argument == NULL_TREE || !CLASS_TYPE_P(argument) || CLASSTYPE_TEMPLATE_INFO(argument) == NULL_TREE) {
            continue;
        }
        statements.class_templates.insert(most_general_template(CLASSTYPE_TI_TEMPLATE(argument)));
    }
    return statements;
}

// True when OWNER states its niche: in its own member, or in a specialization
// of the niche for it or for its class template.
bool states_niche(tree owner, const NicheStatements& statements) {
    static tree const member_name = get_identifier("is_niche_empty_never_built");
    if (statements.classes.count(owner) != 0 || has_statement_member(owner, member_name)) {
        return true;
    }
    tree info = CLASSTYPE_TEMPLATE_INFO(owner);
    if (info == NULL_TREE || TREE_CODE(TI_TEMPLATE(info)) != TEMPLATE_DECL) {
        return false;
    }
    return statements.class_templates.count(most_general_template(TI_TEMPLATE(info))) != 0;
}

// The name of a class for an entity: the name of its most general template
// for an instance, so that the instances of one pattern in many units give
// one line of the census.
std::string owner_name(tree owner) {
    tree info = CLASSTYPE_TEMPLATE_INFO(owner);
    if (info != NULL_TREE && TREE_CODE(TI_TEMPLATE(info)) == TEMPLATE_DECL) {
        return qualified_name(most_general_template(TI_TEMPLATE(info)));
    }
    return qualified_name(TYPE_MAIN_DECL(owner));
}

}  // namespace

// The findings of the unit: each site of a class that states its niche.
void finish_niche_access() {
    if (access_sites.empty()) {
        return;
    }
    NicheStatements const statements = read_niche_statements();
    for (const AccessSite& site : access_sites) {
        if (!states_niche(site.owner, statements)) {
            continue;
        }
        Place place = place_of(site.location, Scope::quarantine);
        if (place.file == nullptr) {
            continue;
        }
        std::string entity = std::string(kind_name(Kind::route_niche_access)) + ": " + owner_name(site.owner) + ' '
                           + shape_phrase(site.shape, site.member) + " in " + site.function;
        add_finding(Kind::route_niche_access, nullptr, place, entity);
    }
    access_sites.clear();
}

// Registers the hooks of the kind niche-access.
void register_niche_access(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_niche, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_MARKING, on_niche_marking, nullptr);
}

}  // namespace crucible_plugin
