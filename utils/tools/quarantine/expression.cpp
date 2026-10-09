// The family expression shape: the walk of a body, and the rules proof-routes,
// refined-pre-subsumption and syscall-grant-coverage.  The walk
// reads each expression of a function body, of an initializer, of a default
// argument and of an unevaluated operand.  It gives the shapes of an
// expression their findings, such as a new-expression, an explicit temporary
// and an asm statement, and it gives each declaration that an expression
// names to the family owner.
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
//
// THE PROOF ROUTES (the rule proof-routes)
//     A proof type attests to a fact that it cannot see, and only its door
//     builds it.  A class is a proof type when a witness row of the rule table
//     names it, by its qualified name with no template arguments.  A class of
//     namespace foundation or fixy is a proof type too when it has the proof
//     shape of test/fixy/test_forgeable_proofs.cpp: it is no union and no
//     aggregate, it declares a constructor, and each public constructor that
//     is not deleted, not a copy and not a move takes a proof type.  The
//     implicit default constructor counts.  A public constructor template
//     takes a proof type when a parameter of its pattern names one.  A
//     specialization that is not complete has the shape of its pattern.  A
//     type holds a proof type when it is one, or through a pointer, a
//     reference, an array or a template argument.
//     A route builds a proof with no call to its door.  Each route is a kind:
//       union           each union definition (PLUGIN_FINISH_TYPE), also an
//                       anonymous one and the pattern of a union template
//       pointer-cast    a static_cast, a reinterpret_cast or a bit_cast to a
//                       pointer or a reference whose target holds a proof
//                       type.  Outside a template, the cast is a conversion at
//                       a place where the source spells static_cast or
//                       reinterpret_cast.  A cast of nullptr and a cast to a
//                       function pointer give no pointer to storage.  The front
//                       end removes a cast with no side effect whose value the
//                       code discards, and a cast to the type of its operand.
//                       Neither gives a pointer that the code keeps
//       allocator       a type that holds std::allocator, allocator_traits or
//                       polymorphic_allocator of a type that holds a proof
//                       type, through a written template argument: in a
//                       declaration, a base, an alias of a namespace or of a
//                       class (check_route_alias), a signature, a type that an
//                       expression names, a temporary that the source spells,
//                       and a call of a member of such a class.
//                       The allocator of a default argument, as in
//                       std::vector<P>, makes no object that can allocate
//       function-specialization
//                       an explicit specialization of a function template.
//                       It is a friend of each class that befriends the
//                       template, so a specialization of a door builds the
//                       proof with no call of the door
//       member-pointer-specialization
//                       an explicit specialization whose template arguments
//                       hold a pointer to a member.  The access check does not
//                       apply to the arguments of an explicit specialization,
//                       so a pointer to a private member escapes through one
//       friend-template-specialization
//                       an explicit or a partial specialization of a class
//                       template that a class of include/ befriends with a
//                       template friend, outside the file that defines the
//                       template with a body.  The specialization is a friend
//                       of that class
//       explicit-instantiation
//                       each explicit instantiation directive, also an
//                       explicit instantiation declaration.  The access check
//                       does not apply to the names in a directive, so a
//                       pointer to a private member can escape through one
//       raw-allocation  a call of operator new, operator new[], malloc,
//                       calloc, realloc or aligned_alloc, of each namespace
//                       and class, whose arguments hold a sizeof of a type
//                       that holds a proof type.  The operand of the sizeof
//                       is a type, or an expression with that type.  A cast of
//                       that storage makes a proof with no call to its door
//     GCC keeps no tree of the last two routes that a plugin can read.  At
//     PLUGIN_FINISH_UNIT, an explicit instantiation of a class has the place of
//     its template, and an explicit instantiation of a function is on no list.
//     Outside a template, GCC folds each sizeof in the arguments of a call to
//     a constant before PLUGIN_PRE_GENERICIZE.  The patched GCC gives two
//     events of the parser for them (utils/toolchain/gcc/patches/0004-*.patch).
//     PLUGIN_EXPLICIT_INSTANTIATION gives each directive with its place.
//     PLUGIN_SIZEOF_OR_ALIGNOF gives each sizeof before the fold.  The plugin
//     keeps the place of each sizeof of a proof type, and a call of an
//     allocation function whose source range holds such a place is the route.
//     An instantiation of a template gives no event, so the sizeof of a
//     template parameter is no route.
//     The entity of a finding is ROUTE: KEY.  KEY is the name of the union,
//     the qualified name of the proof type, the qualified name of the
//     specialized template, or the instantiated class or declaration with its
//     template arguments.  The place of a route is the expansion point of its
//     location, so a route that a macro forms counts in the file that expands
//     the macro.
//
// THE REFINED PARAMETERS (the rule refined-pre-subsumption)
//     A parameter of a specialization of fixy::Refinement, the class of
//     fixy::Refined and fixy::SealedRefined, carries the proof of the
//     predicate of its first template argument.  The cite row of the class of
//     that predicate gives the decide procedures that test it.  A call of
//     decide::PROCEDURE in the expansion of CRUCIBLE_PRE, CRUCIBLE_PRE_FAST or
//     CRUCIBLE_PRE_MSG, in the body of the same function, whose arguments name
//     the parameter, is the finding retest.  Its entity is FUNCTION:
//     PARAMETER: decide::PROCEDURE.  A lambda has a body of its own.
//
// THE GRANTS (the rule syscall-grant-coverage)
//     A grant row of the rule table names a grant file, its alias, the
//     enumeration of the system calls and the door files.  The code of the
//     grant file is each body of the grant file, and each expression that a
//     macro body of the grant file spells, in each file that expands the
//     macro.  In a unit that reads the grant file, that code issues these
//     system calls, each one an enumerator NAME of the enumeration:
//       * a call or the address of the function NAME of the global namespace
//         of a library header
//       * a call of a function NAME_sys
//       * a call of syscall() with the number of the macro SYS_NAME, outside a
//         function NAME2_sys.  Inside such a helper, a number that is not
//         SYS_NAME2 is the finding wrong-helper
//       * a call of a function of a door file, which stands for each system
//         call that its body issues, also through each function of a door
//         file that it calls.  The plugin keeps the facts of the body of each
//         function of a door file that is not a template at
//         PLUGIN_PRE_GENERICIZE, and it reads the pattern of a template
//     At PLUGIN_FINISH_UNIT the unit compares that set with the enumerators
//     that the type of the alias names.  A system call that one set does not
//     hold is the finding not-granted or not-issued, and the line compared
//     names the grant file in each unit that compared the sets.  A grant file
//     whose alias, enumeration, grants or calls the unit cannot read gives
//     unread-grant.  A macro body that no unit expands issues nothing, because
//     no build compiles it.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The walk of one body visits each node one time, so it stays linear when
// subtrees are shared.  A declaration is one shared node for all of its uses,
// so each expression reads the declarations that are its operands: each use
// then counts, at the location of the expression that holds it.
struct RefinedParameter {
    tree parameter = NULL_TREE;
    const CiteRow* cite = nullptr;
};

struct BodyWalk {
    location_t last = UNKNOWN_LOCATION;
    bool is_pattern = false;
    hash_set<tree>* visited = nullptr;
    // The function whose body the walk reads, or null for an initializer.
    tree function = NULL_TREE;
    // The refined parameters of the function (THE REFINED PARAMETERS).
    std::vector<RefinedParameter> refined;
    // The grant row of the grant file that holds the code, or null (THE
    // GRANTS).
    const GrantRow* grant = nullptr;
};

// The identifiers that the family compares, interned one time.
struct Names {
    tree refinement = NULL_TREE;
    tree decide = NULL_TREE;
    tree syscall = NULL_TREE;
    std::unordered_set<tree> cited;  // each procedure of a cite row
    // 1 when the unit has a sentinel base file (THE SENTINEL of plugin.h), 0
    // when it has none, and -1 before the first test.
    int has_sentinel = -1;
    // THE PROOF ROUTES.
    tree bit_cast = NULL_TREE;
    tree allocator = NULL_TREE;
    tree allocator_traits = NULL_TREE;
    tree polymorphic_allocator = NULL_TREE;
    tree foundation = NULL_TREE;
    tree fixy = NULL_TREE;
    tree static_cast_keyword = NULL_TREE;
    tree reinterpret_cast_keyword = NULL_TREE;
    // The names of the allocation functions of the route raw-allocation, other
    // than the operators new.
    std::unordered_set<tree> allocators;
    // The qualified name of the class of each witness row, and the last name
    // of each one, which a test asks before it makes a qualified name.
    std::unordered_set<std::string> witnessed;
    std::unordered_set<tree> witnessed_last_names;
};

Names names;

// The answers of the type walks of THE PROOF ROUTES, keyed by tree.  Each
// collection clears them, because a collection can free a tree and reuse its
// address.
struct Caches {
    std::unordered_map<tree, bool> proof_class;
    std::unordered_map<tree, tree> held_proof;
    std::unordered_map<tree, tree> allocator_proof;
};

Caches caches;

// The facts of THE PROOF ROUTES that the end of the unit reads: each class
// template that a class of include/ befriends with a template friend, and
// each class template with a parameter that is no type, whose explicit
// specializations can hold a pointer to a member.  A template decl of a
// namespace or a class stays to the end of the unit.
struct RouteTemplates {
    std::vector<tree> befriended;
    std::unordered_set<tree> befriended_seen;
    std::vector<tree> value_parameters;
    std::unordered_set<tree> value_parameters_seen;
};

RouteTemplates route_templates;

// The place of each sizeof of a type that holds a proof type, in the sequence
// of the parse, with the qualified name of that proof type (the route
// raw-allocation).  The entries hold no tree, so a collection frees nothing
// that they name.
struct ProofSize {
    location_t location = UNKNOWN_LOCATION;
    std::string proof;
};

std::vector<ProofSize> proof_sizes;

// ── The findings of the family ──────────────────────────────────────────

// A finding of a rule with a base scope: it counts in a quarantined file, and
// in a base file only in the sentinel unit of that file (counts_in_unit).
void record_in_unit(Kind kind, location_t location, const std::string& entity) {
    Place place = place_of(location, Scope::quarantine);
    if (counts_in_unit(place)) {
        add_finding(kind, nullptr, place, entity);
    }
}

// The place of a route (THE PROOF ROUTES): the expansion point of LOCATION, in
// a base or a quarantined file, or a place with no file.
Place route_place(location_t location) {
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
    if (!owns_place(entry.file_class, Scope::quarantine)) {
        return place;
    }
    place.file_class = entry.file_class;
    place.file = &entry;
    place.line = where.line;
    place.column = where.column;
    place.spelling = point;
    return place;
}

// A finding of THE PROOF ROUTES, with the entity ROUTE: KEY.  It counts as a
// finding of a rule with a base scope does (counts_in_unit).
void record_route(Kind kind, location_t location, const std::string& key) {
    Place place = route_place(location);
    if (counts_in_unit(place)) {
        add_finding(kind, nullptr, place, std::string(kind_name(kind)) + ": " + key);
    }
}

// ── The proof types (THE PROOF ROUTES) ──────────────────────────────────

// A passkey chain is at most a key, a token that takes the key, and a carrier
// that takes the token, as max_gate_depth of test_forgeable_proofs.cpp.  The
// bound stops a cycle of types whose constructors take each other.
constexpr int kGateDepth = 4;

bool is_proof_class(tree type, int depth);

// The class whose constructors decide the shape of TYPE: the class itself
// when it is complete, else the pattern of its template.
tree shape_source(tree type) {
    if (COMPLETE_TYPE_P(type) || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return type;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(type);
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return type;
    }
    tree general = most_general_template(template_decl);
    tree result = general != NULL_TREE ? DECL_TEMPLATE_RESULT(general) : NULL_TREE;
    return result != NULL_TREE && TREE_CODE(result) == TYPE_DECL ? TREE_TYPE(result) : type;
}

// True when a public constructor CONSTRUCTOR takes a proof type, so that only
// a holder of that proof can call it.
bool is_gated_constructor(tree constructor, int depth) {
    for (tree parameter = FUNCTION_FIRST_USER_PARMTYPE(constructor);
         parameter != NULL_TREE && parameter != void_list_node; parameter = TREE_CHAIN(parameter)) {
        tree type = non_reference(TREE_VALUE(parameter));
        if (type != NULL_TREE && CLASS_TYPE_P(type) && is_proof_class(type, depth + 1)) {
            return true;
        }
    }
    return false;
}

// The proof shape of THE PROOF ROUTES, with no test of the namespace.
// Complexity: O(constructors * parameters) for each class.
bool has_proof_shape(tree type, int depth) {
    tree source = shape_source(type);
    if (TREE_CODE(source) != RECORD_TYPE || (COMPLETE_TYPE_P(source) && CP_AGGREGATE_TYPE_P(source))) {
        return false;
    }
    // A class has a copy constructor and a move constructor, which the
    // compiler can declare only on a use.  The implicit default constructor
    // is public, so the test must know whether it is deleted: declare it, as
    // a reflection query of members_of does.
    bool declares_constructor =
        COMPLETE_TYPE_P(source) && (CLASSTYPE_LAZY_COPY_CTOR(source) || CLASSTYPE_LAZY_MOVE_CTOR(source));
    if (source == type && COMPLETE_TYPE_P(source) && CLASSTYPE_LAZY_DEFAULT_CTOR(source)) {
        lazily_declare_fn(sfk_constructor, source);
    }
    // A constructor template is gated when a parameter of its pattern names
    // a proof type, as the erasure constructor of a Permission takes a key.
    for (ovl_iterator candidate(CLASSTYPE_CONSTRUCTORS(source)); candidate; ++candidate) {
        tree constructor = *candidate;
        tree function = TREE_CODE(constructor) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(constructor) : constructor;
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
            continue;
        }
        declares_constructor = true;
        bool is_closed = (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DELETED_FN(function))
                      || DECL_COPY_CONSTRUCTOR_P(function) || DECL_MOVE_CONSTRUCTOR_P(function)
                      || TREE_PRIVATE(function) || TREE_PROTECTED(function);
        if (!is_closed && !is_gated_constructor(function, depth)) {
            return false;
        }
    }
    return declares_constructor;
}

// True when a witness row of the rule table names the class TYPE.  The last
// name answers most classes before a qualified name is made.
bool is_witnessed(tree main_type) {
    tree decl = TYPE_MAIN_DECL(main_type);
    tree name = decl != NULL_TREE ? DECL_NAME(decl) : NULL_TREE;
    return name != NULL_TREE && names.witnessed_last_names.count(name) != 0
        && names.witnessed.count(qualified_name(decl)) != 0;
}

// True when TYPE is a proof type (THE PROOF ROUTES).  The cache keeps the
// answer of each class at the depth 0.
bool is_proof_class(tree type, int depth) {
    if (type == NULL_TREE || depth > kGateDepth) {
        return false;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (TREE_CODE(main_type) != RECORD_TYPE || !CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type)
        || TYPE_MAIN_DECL(main_type) == NULL_TREE) {
        return false;
    }
    if (depth == 0) {
        auto cached = caches.proof_class.find(main_type);
        if (cached != caches.proof_class.end()) {
            return cached->second;
        }
    }
    bool is_proof = is_witnessed(main_type);
    if (!is_proof) {
        tree top = top_namespace(TYPE_MAIN_DECL(main_type));
        bool is_base = top != global_namespace && (DECL_NAME(top) == names.foundation || DECL_NAME(top) == names.fixy);
        is_proof = is_base && has_proof_shape(main_type, depth);
    }
    if (depth == 0) {
        caches.proof_class.emplace(main_type, is_proof);
    }
    return is_proof;
}

tree held_proof_of(tree type, int depth);

// The proof type that a template argument holds, or null.
tree held_proof_of_argument(tree argument, int depth) {
    if (argument == NULL_TREE || argument == error_mark_node || depth >= kTypeDepth) {
        return NULL_TREE;
    }
    if (ARGUMENT_PACK_P(argument)) {
        tree arguments = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
            if (tree found = held_proof_of_argument(TREE_VEC_ELT(arguments, index), depth + 1)) {
                return found;
            }
        }
        return NULL_TREE;
    }
    return TYPE_P(argument) ? held_proof_of(argument, depth + 1) : NULL_TREE;
}

// The proof type that TYPE holds: the type itself, or through a pointer, a
// reference, an array or a template argument.  Returns null when it holds
// none.  The cache keeps the answer of each class.  Complexity: O(distinct
// types in the type tree).
tree held_proof_of(tree type, int depth) {
    for (; type != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(type) != POINTER_TYPE && TREE_CODE(type) != REFERENCE_TYPE && TREE_CODE(type) != ARRAY_TYPE) {
            break;
        }
        type = TREE_TYPE(type);
    }
    if (type == NULL_TREE || depth >= kTypeDepth || TREE_CODE(type) != RECORD_TYPE || !CLASS_TYPE_P(type)) {
        return NULL_TREE;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    auto cached = caches.held_proof.find(main_type);
    if (cached != caches.held_proof.end()) {
        return cached->second;
    }
    tree found = is_proof_class(main_type, 0) ? main_type : NULL_TREE;
    if (found == NULL_TREE && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
        tree arguments = INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(main_type));
        for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments) && found == NULL_TREE;
             ++index) {
            found = held_proof_of_argument(TREE_VEC_ELT(arguments, index), depth);
        }
    }
    caches.held_proof.emplace(main_type, found);
    return found;
}

// The proof type that the target of a cast to TYPE holds, when TYPE is a
// pointer or a reference whose target is no function, or null.
tree cast_target_proof(tree type) {
    if (type == NULL_TREE || (!TYPE_PTR_P(type) && !TYPE_REF_P(type))) {
        return NULL_TREE;
    }
    tree target = TREE_TYPE(type);
    return target != NULL_TREE && !FUNC_OR_METHOD_TYPE_P(target) ? held_proof_of(target, 0) : NULL_TREE;
}

// True when the class template of CLASS_TYPE is an allocator of THE PROOF
// ROUTES.
bool is_allocator_class(tree class_type) {
    if (!CLASS_TYPE_P(class_type) || CLASSTYPE_TEMPLATE_INFO(class_type) == NULL_TREE) {
        return false;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(class_type);
    tree name = template_decl != NULL_TREE ? DECL_NAME(template_decl) : NULL_TREE;
    return name != NULL_TREE
        && (name == names.allocator || name == names.allocator_traits || name == names.polymorphic_allocator);
}

// The proof type of an allocator that TYPE holds: an allocator class whose
// template arguments hold a proof type, through a pointer, a reference, an
// array or a written template argument.  Returns null when it holds none.
// GCC keeps one specialization for the two spellings std::vector<P> and
// std::vector<P, std::allocator<P>>, and the count of its written arguments
// is the count of the first spelling of the unit.  The cache keeps the answer
// of each class.  Complexity: O(distinct types in the type tree).
tree allocator_proof_of(tree type, int depth) {
    for (; type != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(type) != POINTER_TYPE && TREE_CODE(type) != REFERENCE_TYPE && TREE_CODE(type) != ARRAY_TYPE) {
            break;
        }
        type = TREE_TYPE(type);
    }
    if (type == NULL_TREE || depth >= kTypeDepth || TREE_CODE(type) != RECORD_TYPE || !CLASS_TYPE_P(type)
        || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return NULL_TREE;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    auto cached = caches.allocator_proof.find(main_type);
    if (cached != caches.allocator_proof.end()) {
        return cached->second;
    }
    tree found = NULL_TREE;
    bool is_allocator = is_allocator_class(main_type);
    tree arguments = INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(main_type));
    // An argument that a default of the template gives is no name of the
    // source: std::vector<P> holds std::allocator<P> through its default.
    // GCC records the count of the other arguments when a default fills one.
    int written = arguments != NULL_TREE ? TREE_VEC_LENGTH(arguments) : 0;
    if (!is_allocator && arguments != NULL_TREE && NON_DEFAULT_TEMPLATE_ARGS_COUNT(arguments) != NULL_TREE
        && TREE_CODE(NON_DEFAULT_TEMPLATE_ARGS_COUNT(arguments)) == INTEGER_CST) {
        written = static_cast<int>(int_cst_value(NON_DEFAULT_TEMPLATE_ARGS_COUNT(arguments)));
    }
    for (int index = 0; index < written && found == NULL_TREE; ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (argument != NULL_TREE && TYPE_P(argument)) {
            found = is_allocator ? held_proof_of(argument, depth + 1) : allocator_proof_of(argument, depth + 1);
        }
    }
    caches.allocator_proof.emplace(main_type, found);
    return found;
}

// The allocator route of a type that the code names at LOCATION.  A type that
// is no class template instance costs two tests.
void check_allocator_type(tree type, location_t location) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return;
    }
    if (tree proof = allocator_proof_of(type, 0)) {
        record_route(Kind::route_allocator, location, qualified_name(TYPE_MAIN_DECL(proof)));
    }
}

// The allocator routes of a function type: its return type and each
// parameter.
void check_allocator_signature(tree function_type, location_t location) {
    if (function_type == NULL_TREE || !FUNC_OR_METHOD_TYPE_P(function_type)) {
        return;
    }
    check_allocator_type(TREE_TYPE(function_type), location);
    for (tree parameter = TYPE_ARG_TYPES(function_type); parameter != NULL_TREE && parameter != void_list_node;
         parameter = TREE_CHAIN(parameter)) {
        check_allocator_type(TREE_VALUE(parameter), location);
    }
}

// True when EXPRESSION is a null pointer constant, through its location
// wrappers and conversions.
bool is_null_pointer(tree expression) {
    for (int depth = 0; expression != NULL_TREE && depth < kTypeDepth; ++depth) {
        expression = tree_strip_any_location_wrapper(expression);
        if (TREE_CODE(expression) != NOP_EXPR && TREE_CODE(expression) != CONVERT_EXPR) {
            break;
        }
        expression = TREE_OPERAND(expression, 0);
    }
    return expression != NULL_TREE
        && (expression == nullptr_node || NULLPTR_TYPE_P(TREE_TYPE(expression))
            || (TREE_CODE(expression) == INTEGER_CST && integer_zerop(expression)));
}

// True when the source spells static_cast or reinterpret_cast at the start of
// the range of LOCATION.
bool spells_cast_at(location_t location) {
    std::string spelled = identifier_spelled_at(location);
    return spelled == IDENTIFIER_POINTER(names.static_cast_keyword)
        || spelled == IDENTIFIER_POINTER(names.reinterpret_cast_keyword);
}

// A cast of a body to a pointer or a reference whose target holds a proof
// type.  Outside a template, CONVERSION is the outer node of the cast: a
// conversion, or the null test and the offset of a cast to a derived class.
// The front end gives that node the location of the cast, so it counts only
// where the source spells the cast at the start of its location.
void check_cast(tree conversion, location_t location, bool is_pattern) {
    tree proof = cast_target_proof(TREE_TYPE(conversion));
    if (proof == NULL_TREE) {
        return;
    }
    bool is_nop = TREE_CODE(conversion) == NOP_EXPR || TREE_CODE(conversion) == CONVERT_EXPR;
    if ((is_nop || is_pattern) && is_null_pointer(TREE_OPERAND(conversion, 0))) {
        return;
    }
    if (!is_pattern && !spells_cast_at(location)) {
        return;
    }
    record_route(Kind::route_pointer_cast, location, qualified_name(TYPE_MAIN_DECL(proof)));
}

// ── The specializations (THE PROOF ROUTES) ──────────────────────────────

// True when a template argument holds a pointer to a member: a constant of a
// pointer to a member, the address of a member of a class, or a class
// template instance whose arguments hold one.  Complexity: O(nodes of the
// argument).
bool holds_member_pointer(tree argument, int depth) {
    if (argument == NULL_TREE || argument == error_mark_node || depth >= kTypeDepth) {
        return false;
    }
    if (ARGUMENT_PACK_P(argument)) {
        tree packed = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(packed); ++index) {
            if (holds_member_pointer(TREE_VEC_ELT(packed, index), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    if (TYPE_P(argument)) {
        tree main_type = TYPE_MAIN_VARIANT(argument);
        if (!CLASS_TYPE_P(main_type) || CLASSTYPE_TEMPLATE_INFO(main_type) == NULL_TREE) {
            return false;
        }
        tree arguments = INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(main_type));
        for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
            if (holds_member_pointer(TREE_VEC_ELT(arguments, index), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    tree value = tree_strip_any_location_wrapper(argument);
    switch (TREE_CODE(value)) {
        case PTRMEM_CST:
        case OFFSET_REF:
            return true;
        case NOP_EXPR:
        case CONVERT_EXPR:
        case VIEW_CONVERT_EXPR:
            return holds_member_pointer(TREE_OPERAND(value, 0), depth + 1);
        case ADDR_EXPR: {
            tree operand = TREE_OPERAND(value, 0);
            return operand != NULL_TREE
                && (TREE_CODE(operand) == SCOPE_REF || TREE_CODE(operand) == OFFSET_REF
                    || TREE_CODE(operand) == BASELINK || (DECL_P(operand) && DECL_CLASS_SCOPE_P(operand)));
        }
        default:
            return false;
    }
}

// True when the innermost arguments of the template arguments ARGUMENTS hold
// a pointer to a member.
bool holds_member_pointer_in(tree arguments) {
    tree inner = arguments != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(arguments) : NULL_TREE;
    if (inner == NULL_TREE || TREE_CODE(inner) != TREE_VEC) {
        return false;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(inner); ++index) {
        if (holds_member_pointer(TREE_VEC_ELT(inner, index), 0)) {
            return true;
        }
    }
    return false;
}

// The qualified name of the most general template of TEMPLATE_DECL.
std::string template_key(tree template_decl) {
    tree general = most_general_template(template_decl);
    return qualified_name(general != NULL_TREE ? general : template_decl);
}

// An explicit specialization of a function template or a variable template:
// the route function-specialization for a function, and the route
// member-pointer-specialization when its template arguments hold a pointer to
// a member.  An explicit specialization of a member of an instance of a class
// template, such as `template <> void X<int>::f()`, is no specialization of a
// function template.
void check_decl_specialization(tree decl) {
    if (!VAR_OR_FUNCTION_DECL_P(decl) || DECL_LANG_SPECIFIC(decl) == nullptr || !DECL_TEMPLATE_SPECIALIZATION(decl)
        || DECL_TEMPLATE_INFO(decl) == NULL_TREE) {
        return;
    }
    tree template_decl = DECL_TI_TEMPLATE(decl);
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL || !PRIMARY_TEMPLATE_P(template_decl)) {
        return;
    }
    location_t location = DECL_SOURCE_LOCATION(decl);
    std::string key = template_key(template_decl);
    if (TREE_CODE(decl) == FUNCTION_DECL) {
        record_route(Kind::route_function_specialization, location, key);
    }
    if (holds_member_pointer_in(DECL_TI_ARGS(decl))) {
        record_route(Kind::route_member_pointer_specialization, location, key);
    }
}

// The template decl of a class template instance or specialization TYPE, or
// null.
tree class_template_of(tree type) {
    if (!CLASS_TYPE_P(type) || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return NULL_TREE;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(type);
    return template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL ? template_decl : NULL_TREE;
}

// An explicit specialization of a class template whose template arguments
// hold a pointer to a member.
void check_class_specialization(tree type) {
    if (!CLASSTYPE_TEMPLATE_SPECIALIZATION(type) || uses_template_parms(type)
        || !holds_member_pointer_in(CLASSTYPE_TI_ARGS(type))) {
        return;
    }
    if (tree template_decl = class_template_of(type)) {
        record_route(Kind::route_member_pointer_specialization, DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(type)),
                     template_key(template_decl));
    }
}

// True when a parameter of the innermost template parameter list of
// TEMPLATE_DECL is no type, so that an argument can be a pointer to a member.
bool has_value_parameter(tree template_decl) {
    tree levels = DECL_TEMPLATE_PARMS(template_decl);
    tree inner = levels != NULL_TREE ? INNERMOST_TEMPLATE_PARMS(levels) : NULL_TREE;
    for (int index = 0; inner != NULL_TREE && index < TREE_VEC_LENGTH(inner); ++index) {
        tree parameter = TREE_VALUE(TREE_VEC_ELT(inner, index));
        if (parameter != NULL_TREE && TREE_CODE(parameter) == PARM_DECL) {
            return true;
        }
    }
    return false;
}

// The template friends of a class of include/: each class template that the
// class befriends, for the end of the unit.  The pattern of a class template
// with a parameter that is no type, for the end of the unit too.  A class with
// no friend and no template costs two tests.
void note_route_templates(tree type, location_t location) {
    if (CLASSTYPE_FRIEND_CLASSES(type) != NULL_TREE) {
        Place place = route_place(location);
        if (place.file != nullptr && has_prefix(place.file->relative, "include/")) {
            for (tree entry = CLASSTYPE_FRIEND_CLASSES(type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
                tree befriended = TREE_VALUE(entry);
                if (befriended != NULL_TREE && TREE_CODE(befriended) == TEMPLATE_DECL
                    && route_templates.befriended_seen.insert(befriended).second) {
                    route_templates.befriended.push_back(befriended);
                }
            }
        }
    }
    tree template_decl = class_template_of(type);
    if (template_decl != NULL_TREE && PRIMARY_TEMPLATE_P(template_decl)
        && TREE_TYPE(DECL_TEMPLATE_RESULT(template_decl)) == type && has_value_parameter(template_decl)
        && route_templates.value_parameters_seen.insert(template_decl).second) {
        route_templates.value_parameters.push_back(template_decl);
    }
}

// The location that defines a class template: its pattern.
location_t pattern_location(tree template_decl) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    return result != NULL_TREE && TREE_CODE(result) == TYPE_DECL && CLASS_TYPE_P(TREE_TYPE(result))
             ? DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(TREE_TYPE(result)))
             : DECL_SOURCE_LOCATION(template_decl);
}

// Each explicit and each partial specialization of a befriended class
// template outside the file that defines the template with a body: the route
// friend-template-specialization.  A template with no definition in the unit
// has no defining file here, so each of its specializations counts.
void read_friend_specializations(tree befriended) {
    tree general = most_general_template(befriended);
    if (general == NULL_TREE) {
        general = befriended;
    }
    tree result = DECL_TEMPLATE_RESULT(general);
    if (result == NULL_TREE || TREE_CODE(result) != TYPE_DECL || !CLASS_TYPE_P(TREE_TYPE(result))) {
        return;
    }
    const FileEntry* home = COMPLETE_TYPE_P(TREE_TYPE(result)) ? route_place(pattern_location(general)).file : nullptr;
    std::string key = qualified_name(general);
    auto read_site = [&](location_t location) {
        if (route_place(location).file != home || home == nullptr) {
            record_route(Kind::route_friend_template_specialization, location, key);
        }
    };
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
            read_site(pattern_location(partial));
        }
    }
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree specialization = TREE_VALUE(entry);
        if (specialization != NULL_TREE && CLASS_TYPE_P(specialization)
            && CLASSTYPE_TEMPLATE_SPECIALIZATION(specialization) && !uses_template_parms(specialization)) {
            read_site(DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(specialization)));
        }
    }
}

// Each explicit specialization of a class template with a parameter that is
// no type, also one that the unit only declares (check_class_specialization).
void read_value_specializations(tree template_decl) {
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree specialization = TREE_VALUE(entry);
        if (specialization != NULL_TREE && CLASS_TYPE_P(specialization)) {
            check_class_specialization(specialization);
        }
    }
}

// ── The refined parameters (THE REFINED PARAMETERS) ─────────────────────

// The cite row of the predicate of a refinement type TYPE, or null.
const CiteRow* cite_of_refinement(tree type) {
    type = non_reference(type);
    if (type == NULL_TREE || !CLASS_TYPE_P(type) || CLASSTYPE_TEMPLATE_INFO(type) == NULL_TREE) {
        return nullptr;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree template_decl = most_general_template(CLASSTYPE_TI_TEMPLATE(main_type));
    if (template_decl == NULL_TREE || DECL_NAME(template_decl) != names.refinement
        || qualified_name(template_decl) != "fixy::Refinement") {
        return nullptr;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(main_type));
    if (arguments == NULL_TREE || TREE_VEC_LENGTH(arguments) == 0) {
        return nullptr;
    }
    tree predicate = TREE_VEC_ELT(arguments, 0);
    tree predicate_type = predicate != NULL_TREE && TYPE_P(predicate) ? predicate
                        : predicate != NULL_TREE                     ? TREE_TYPE(predicate)
                                                                     : NULL_TREE;
    if (predicate_type == NULL_TREE || !CLASS_TYPE_P(predicate_type)) {
        return nullptr;
    }
    std::string name = qualified_name(TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(predicate_type)));
    for (const CiteRow& cite : state.table.cites) {
        if (cite.predicate == name) {
            return &cite;
        }
    }
    return nullptr;
}

// The refined parameters of FUNCTION whose predicate has a cite row.
std::vector<RefinedParameter> refined_parameters_of(tree function) {
    std::vector<RefinedParameter> found;
    if (names.cited.empty()) {
        return found;
    }
    for (tree parameter = DECL_ARGUMENTS(function); parameter != NULL_TREE; parameter = DECL_CHAIN(parameter)) {
        if (DECL_ARTIFICIAL(parameter) || TREE_CODE(parameter) != PARM_DECL) {
            continue;
        }
        if (const CiteRow* cite = cite_of_refinement(TREE_TYPE(parameter))) {
            found.push_back(RefinedParameter{parameter, cite});
        }
    }
    return found;
}

// True when a macro of the precondition family expands the token at
// LOCATION.  Complexity: O(depth of the macro expansion).
bool is_in_precondition(location_t location) {
    location_t current = location;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, current); ++depth) {
        const line_map* map = linemap_lookup(line_table, current);
        cpp_hashnode* macro = MACRO_MAP_MACRO(linemap_check_macro(map));
        const char* name = macro != nullptr ? reinterpret_cast<const char*>(NODE_NAME(macro)) : "";
        if (std::strcmp(name, "CRUCIBLE_PRE") == 0 || std::strcmp(name, "CRUCIBLE_PRE_FAST") == 0
            || std::strcmp(name, "CRUCIBLE_PRE_MSG") == 0) {
            return true;
        }
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return false;
}

tree find_parameter(tree* node_pointer, int*, void* data) {
    return *node_pointer == static_cast<tree>(data) ? *node_pointer : NULL_TREE;
}

// A call of a decide procedure that tests again the predicate of a refined
// parameter of the function of the walk.  The entity names the function by
// its qualified name, so two overloads of one name in a file share a row.
void check_retest(tree call, location_t location, const BodyWalk& walk) {
    tree procedure = NULL_TREE;
    for_each_function(callee_of(call), [&](tree function) {
        tree context = CP_DECL_CONTEXT(function);
        if (procedure == NULL_TREE && names.cited.count(DECL_NAME(function)) != 0 && context != NULL_TREE
            && TREE_CODE(context) == NAMESPACE_DECL && DECL_NAME(context) == names.decide) {
            procedure = DECL_NAME(function);
        }
    });
    if (procedure == NULL_TREE || !is_in_precondition(location)) {
        return;
    }
    for (const RefinedParameter& refined : walk.refined) {
        bool is_cited = false;
        for (const std::string& cited : refined.cite->procedures) {
            is_cited = is_cited || cited == IDENTIFIER_POINTER(procedure);
        }
        bool is_named = false;
        for (int index = 0; index < argument_count(call) && !is_named && is_cited; ++index) {
            tree argument = argument_at(call, index);
            is_named = argument != NULL_TREE
                    && cp_walk_tree_without_duplicates(&argument, find_parameter, refined.parameter) != NULL_TREE;
        }
        if (is_named) {
            record_in_unit(Kind::refined_retest, location,
                         qualified_name(walk.function) + ": " + simple_name(refined.parameter)
                             + ": decide::" + IDENTIFIER_POINTER(procedure));
        }
    }
}

// ── The grants (THE GRANTS) ─────────────────────────────────────────────

// One function that the code names: its name, the enumeration of the system
// calls is not known before the end of the unit.
struct NamedFunction {
    std::string name;
    location_t location = UNKNOWN_LOCATION;
    bool is_library = false;  // a function of the global namespace of a library header
};

// One call of syscall(): its number when the first argument is a constant,
// the place, and the helper NAME_sys that holds it, or "".
struct SyscallCall {
    bool has_number = false;
    long long number = 0;
    location_t location = UNKNOWN_LOCATION;
    std::string helper;
};

// The facts of the code of one body: the functions that it names, its calls
// of syscall() and the functions of a door file that it calls.
struct BodyFacts {
    std::vector<NamedFunction> named;
    std::vector<SyscallCall> syscalls;
    std::vector<std::pair<tree, location_t>> doors;
};

// The facts of a grant file in this unit, and of each function of a door
// file that is not a template, by its DECL_UID.  A function of a door file is
// a decl of a namespace or a class, which no collection frees.
struct GrantUnit {
    bool is_seen = false;
    BodyFacts facts;
};

std::unordered_map<const GrantRow*, GrantUnit> grant_units;
std::unordered_map<unsigned, BodyFacts> door_facts;

// The grant row of the door file that holds FUNCTION, or null.
const GrantRow* door_row_of(tree function) {
    const char* file = DECL_SOURCE_FILE(function);
    return file != nullptr ? classify_file(file).grant_door : nullptr;
}

// The name of the helper NAME_sys that FUNCTION is, or "".
std::string helper_name_of(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_NAME(function) == NULL_TREE) {
        return {};
    }
    std::string name = IDENTIFIER_POINTER(DECL_NAME(function));
    return name.size() > 4 && name.compare(name.size() - 4, 4, "_sys") == 0 ? name : std::string{};
}

struct FactsWalk {
    BodyFacts* facts = nullptr;
    std::string helper;
    location_t last = UNKNOWN_LOCATION;
    hash_set<tree>* visited = nullptr;
};

tree collect_body_facts(tree* node_pointer, int* walk_subtrees, void* data);

// The initializer of a local of a pattern.  A template keeps it in
// DECL_INITIAL, and cp_walk_tree does not reach it outside template
// processing.
void collect_local_facts(tree decl, FactsWalk& walk) {
    if (decl != NULL_TREE && VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
        cp_walk_tree(&DECL_INITIAL(decl), collect_body_facts, &walk, walk.visited);
    }
}

// The facts of one node of a body (THE GRANTS).  A function counts where the
// code names it: a call, an address or a candidate of a dependent call.
tree collect_body_facts(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    FactsWalk& walk = *static_cast<FactsWalk*>(data);
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        case DECL_EXPR:
            collect_local_facts(DECL_EXPR_DECL(node), walk);
            return NULL_TREE;
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                collect_local_facts(variable, walk);
            }
            return NULL_TREE;
        case FUNCTION_DECL:
        case OVERLOAD:
        case TEMPLATE_DECL:
        case BASELINK:
            for_each_function(node, [&](tree function) {
                if (DECL_NAME(function) == NULL_TREE) {
                    return;
                }
                walk.facts->named.push_back(NamedFunction{IDENTIFIER_POINTER(DECL_NAME(function)), walk.last,
                                                          library_of(function) == Library::c_library});
                if (door_row_of(function) != nullptr) {
                    walk.facts->doors.emplace_back(function, walk.last);
                }
            });
            *walk_subtrees = 0;
            return NULL_TREE;
        case CALL_EXPR: {
            bool is_syscall = false;
            for_each_function(callee_of(node), [&](tree function) {
                is_syscall = is_syscall || is_library_function(function, names.syscall);
            });
            if (is_syscall) {
                SyscallCall call;
                call.has_number = syscall_number_of(node, call.number);
                call.location = EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : walk.last;
                call.helper = walk.helper;
                walk.facts->syscalls.push_back(std::move(call));
            }
            return NULL_TREE;
        }
        default:
            return NULL_TREE;
    }
}

// The facts of the body BODY of the function FUNCTION, or of an initializer
// when FUNCTION is null.  Complexity: O(nodes of the body).
void collect_facts(tree* body, tree function, location_t location, BodyFacts& facts) {
    if (body == nullptr || *body == NULL_TREE) {
        return;
    }
    hash_set<tree> visited;
    FactsWalk walk;
    walk.facts = &facts;
    walk.helper = helper_name_of(function);
    walk.last = location;
    walk.visited = &visited;
    cp_walk_tree(body, collect_body_facts, &walk, &visited);
}

// The grant row of the grant file that holds LOCATION, or null.  A unit that
// read no grant file pays one test.
const GrantRow* grant_row_at(location_t location) {
    if (grant_units.empty()) {
        return nullptr;
    }
    Place place = place_of(location, Scope::quarantine);
    return place.file != nullptr ? place.file->grant : nullptr;
}

// The facts of an expression NODE of a body outside a grant file, when a macro
// body of a grant file spells it at LOCATION (THE GRANTS).  The grant file
// issues such a call in each file that expands its macro.  A macro body is in
// no helper NAME_sys.  A location that no macro expansion gives costs one
// test.
void collect_macro_facts(tree node, location_t location) {
    if (!linemap_location_from_macro_expansion_p(line_table, location)) {
        return;
    }
    const GrantRow* grant = grant_row_at(location);
    if (grant == nullptr) {
        return;
    }
    FactsWalk walk;
    walk.facts = &grant_units[grant].facts;
    walk.last = location;
    int walk_subtrees = 1;
    if (TREE_CODE(node) == CALL_EXPR) {
        collect_body_facts(&node, &walk_subtrees, &walk);
    }
    for (int index = 0; index < TREE_OPERAND_LENGTH(node); ++index) {
        tree operand = TREE_OPERAND(node, index);
        if (operand == NULL_TREE) {
            continue;
        }
        switch (TREE_CODE(operand)) {
            case FUNCTION_DECL:
            case OVERLOAD:
            case TEMPLATE_DECL:
            case BASELINK:
                collect_body_facts(&operand, &walk_subtrees, &walk);
                break;
            default:
                break;
        }
    }
}

// ── The routes of a call (THE PROOF ROUTES) ─────────────────────────────

// True when FUNCTION gives raw storage: an operator new or new[] of each
// scope, or a function with a name of names.allocators.
bool is_raw_allocator(tree function) {
    tree name = DECL_NAME(function);
    return name != NULL_TREE && (IDENTIFIER_NEW_OP_P(name) || names.allocators.count(name) != 0);
}

// True when the source range of RANGE holds the token at LOCATION.  Two
// tokens of one macro expansion compare by their position in the expansion.
bool range_holds(const source_range& range, location_t location) {
    return linemap_compare_locations(line_table, range.m_start, location) >= 0
        && linemap_compare_locations(line_table, location, range.m_finish) >= 0;
}

// A call of an allocation function whose source range holds the sizeof of a
// proof type: the route raw-allocation.  A call has one finding, with the
// first such sizeof of its range.  A unit with no sizeof of a proof type
// pays one test for each call.  Complexity: O(sizeofs of proof types) for
// each call of an allocation function.
void check_raw_allocation(tree call) {
    if (proof_sizes.empty() || !EXPR_HAS_LOCATION(call)) {
        return;
    }
    bool is_allocation = false;
    for_each_function(callee_of(call), [&](tree function) { is_allocation = is_allocation || is_raw_allocator(function); });
    if (!is_allocation) {
        return;
    }
    location_t location = EXPR_LOCATION(call);
    source_range range = get_range_from_loc(line_table, location);
    for (const ProofSize& size : proof_sizes) {
        if (range_holds(range, size.location)) {
            record_route(Kind::route_raw_allocation, location, size.proof);
            return;
        }
    }
}

// A bit_cast, a call of a member of an allocator of a proof type, and a raw
// allocation with the size of a proof type.
void check_call_routes(tree call, location_t location) {
    check_raw_allocation(call);
    tree callee = callee_of(call);
    bool is_bit_cast = false;
    tree allocator = NULL_TREE;
    for_each_function(callee, [&](tree function) {
        is_bit_cast = is_bit_cast || DECL_NAME(function) == names.bit_cast;
        // A constructor builds the allocator of a default argument too, as in
        // std::vector<P>(count, value), and the source spells none.  A spelled
        // object of the class is a declaration or a temporary.
        tree context = DECL_CONTEXT(function);
        bool is_member_call = !DECL_CONSTRUCTOR_P(function) && !DECL_DESTRUCTOR_P(function);
        if (allocator == NULL_TREE && is_member_call && context != NULL_TREE && TYPE_P(context)) {
            allocator = allocator_proof_of(context, 0);
        }
    });
    if (allocator != NULL_TREE) {
        record_route(Kind::route_allocator, location, qualified_name(TYPE_MAIN_DECL(allocator)));
    }
    if (is_bit_cast) {
        tree target = TREE_TYPE(call);
        if (TREE_CODE(callee) == TEMPLATE_ID_EXPR || (TREE_CODE(callee) == ADDR_EXPR && target == NULL_TREE)) {
            tree arguments = TREE_CODE(callee) == TEMPLATE_ID_EXPR ? TREE_OPERAND(callee, 1) : NULL_TREE;
            target = arguments != NULL_TREE && TREE_CODE(arguments) == TREE_VEC && TREE_VEC_LENGTH(arguments) > 0
                       ? TREE_VEC_ELT(arguments, 0)
                       : target;
        }
        tree proof = target != NULL_TREE && TYPE_P(target) ? cast_target_proof(target) : NULL_TREE;
        if (proof != NULL_TREE) {
            record_route(Kind::route_pointer_cast, location, qualified_name(TYPE_MAIN_DECL(proof)));
        }
    }
}

// ── The walk of a body ──────────────────────────────────────────────────

void walk_expression(tree* expression, BodyWalk& walk);

// A new-expression.  Its entity is the operator, with no printed type
// (declaration.cpp, THE ENTITY OF AN OBJECT).  The call of an allocation
// function in owner.cpp gives the same entity.
void check_new_expression(tree expression, location_t location) {
    bool is_placement = TREE_OPERAND(expression, 0) != NULL_TREE;
    record(Kind::raw_new_delete, location, is_placement ? "placement new" : "new");
}

// An explicit temporary, such as one that the code passes to a template: a
// TARGET_EXPR that a constructor initializes, whose range starts at the name
// of its class (spells_class_at).  The source names the class there, and each
// library class that the class holds is a std-entity, as a functional cast in
// a template is.  An implicit list initialization has the flag
// TARGET_EXPR_LIST_INIT_P, and a call that returns a class calls no
// constructor, so neither is a finding.  A TARGET_EXPR with no location of
// its own is no finding either.  The allocator route of the class counts at
// the same place (THE PROOF ROUTES).
void check_temporary(tree target) {
    if (TARGET_EXPR_LIST_INIT_P(target) || TARGET_EXPR_INITIAL(target) == NULL_TREE || !EXPR_HAS_LOCATION(target)) {
        return;
    }
    location_t location = EXPR_LOCATION(target);
    tree callee = cp_get_callee_fndecl_nofold(TARGET_EXPR_INITIAL(target));
    if (callee == NULL_TREE || TREE_CODE(callee) != FUNCTION_DECL || !DECL_CONSTRUCTOR_P(callee)) {
        return;
    }
    if (allocator_proof_of(TREE_TYPE(target), 0) != NULL_TREE && spells_class_at(location, TREE_TYPE(target))) {
        check_allocator_type(TREE_TYPE(target), location);
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
            check_allocator_type(TREE_TYPE(decl), DECL_SOURCE_LOCATION(decl));
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
        check_allocator_type(DECL_ORIGINAL_TYPE(decl), DECL_SOURCE_LOCATION(decl));
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

// A type that the code names in an expression: the uses of the family owner,
// and the allocator route (THE PROOF ROUTES).
void check_named_type(tree type, location_t location) {
    check_type_use(type, location);
    check_allocator_type(type, location);
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
            check_named_type(TREE_TYPE(node), location);
            break;
        case CONSTRUCTOR:
            if (COMPOUND_LITERAL_P(node)) {
                check_named_type(TREE_TYPE(node), location);
            }
            break;
        case TEMPLATE_ID_EXPR:
            if (tree arguments = TREE_OPERAND(node, 1); arguments != NULL_TREE && TREE_CODE(arguments) == TREE_VEC) {
                for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
                    if (tree argument = TREE_VEC_ELT(arguments, index); argument != NULL_TREE && TYPE_P(argument)) {
                        check_named_type(argument, location);
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

// The expression shapes of the rules of THE PROOF ROUTES and THE REFINED
// PARAMETERS at one node.
void check_expression_rules(tree node, BodyWalk& walk) {
    switch (TREE_CODE(node)) {
        case NOP_EXPR:
        case CONVERT_EXPR:
        case VIEW_CONVERT_EXPR:
        case COND_EXPR:
        case POINTER_PLUS_EXPR:
            if (!walk.is_pattern && EXPR_HAS_LOCATION(node)) {
                check_cast(node, EXPR_LOCATION(node), false);
            }
            break;
        case STATIC_CAST_EXPR:
        case REINTERPRET_CAST_EXPR:
            check_cast(node, walk.last, true);
            break;
        case BIT_CAST_EXPR:
            if (tree proof = cast_target_proof(TREE_TYPE(node))) {
                record_route(Kind::route_pointer_cast, walk.last, qualified_name(TYPE_MAIN_DECL(proof)));
            }
            break;
        case CALL_EXPR:
        case AGGR_INIT_EXPR: {
            location_t location = EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : walk.last;
            check_call_routes(node, location);
            if (!walk.refined.empty()) {
                check_retest(node, location, walk);
            }
            break;
        }
        default:
            break;
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
                check_named_type(TREE_TYPE(TREE_OPERAND(node, 0)), walk.last);
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
            check_owned_node(node, walk.last, walk.function);
            walk_expression(&TREE_OPERAND(node, 0), walk);
            *walk_subtrees = 0;
            return NULL_TREE;
        // A using-directive in a function body names its namespace.  The front
        // end also keeps a USING_STMT for each target of a using-declaration
        // that is no template, for the debug information, and check_using
        // reads the using-declaration itself.
        case USING_STMT:
            if (tree used = USING_STMT_NAMESPACE(node); used != NULL_TREE && TREE_CODE(used) == NAMESPACE_DECL) {
                note_project_reference(used, walk.last);
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
    check_expression_rules(node, walk);
    check_owned_node(node, walk.last, walk.function);
    if (walk.grant == nullptr && !grant_units.empty() && EXPR_P(node)) {
        collect_macro_facts(node, walk.last);
    }
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
        check_named_type(node, walk.last);
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

// One walk of a body or an initializer, with its own set of visited nodes.
// The code of a grant file also gives its facts (THE GRANTS).
void walk_root_of(tree* root, location_t location, bool is_pattern, tree function,
                  std::vector<RefinedParameter> refined, const GrantRow* grant) {
    hash_set<tree> visited;
    BodyWalk walk;
    walk.last = location;
    walk.is_pattern = is_pattern;
    walk.visited = &visited;
    walk.function = function;
    walk.refined = std::move(refined);
    walk.grant = grant;
    walk_expression(root, walk);
    if (grant != nullptr) {
        collect_facts(root, function, location, grant_units[grant].facts);
    }
}

// ── The walk of a sentinel base file (THE SENTINEL of plugin.h) ─────────

// True when the unit has a sentinel base file: the argument sentinel names a
// header, or the main file of the unit is a base file.
bool has_sentinel_file() {
    if (names.has_sentinel < 0) {
        bool is_base_main =
            main_input_filename != nullptr && classify_file(main_input_filename).file_class == FileClass::base;
        names.has_sentinel = !state.sentinel.empty() || is_base_main ? 1 : 0;
    }
    return names.has_sentinel == 1;
}

tree visit_family_node(tree* node_pointer, int* walk_subtrees, void* data);

// A local declaration of a body that only the family reads: the allocator
// route of its type (THE PROOF ROUTES).  A template keeps the initializer of a
// local in DECL_INITIAL, and cp_walk_tree does not reach it outside template
// processing.  A member function of a local class of a template is a pattern
// that no PLUGIN_PRE_GENERICIZE gives.
void walk_family_local(tree decl, BodyWalk& walk) {
    if (decl == NULL_TREE) {
        return;
    }
    if (VAR_P(decl)) {
        check_allocator_type(TREE_TYPE(decl), DECL_SOURCE_LOCATION(decl));
    } else if (TREE_CODE(decl) == TYPE_DECL && !DECL_ARTIFICIAL(decl)) {
        check_allocator_type(DECL_ORIGINAL_TYPE(decl), DECL_SOURCE_LOCATION(decl));
    }
    if (!walk.is_pattern) {
        return;
    }
    if (VAR_P(decl) && DECL_INITIAL(decl) != NULL_TREE) {
        cp_walk_tree(&DECL_INITIAL(decl), visit_family_node, &walk, walk.visited);
        return;
    }
    if (TREE_CODE(decl) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(decl) || !CLASS_TYPE_P(TREE_TYPE(decl))) {
        return;
    }
    for (tree member = TYPE_FIELDS(TREE_TYPE(decl)); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree function = TREE_CODE(member) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(member) : member;
        if (function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL) {
            walk_body(function, true);
        }
    }
}

// One node of a body that only the family reads.  The rules of the
// quarantine read no base file, so the walk reads only the expression shapes
// of the family and the local declarations of a pattern.
tree visit_family_node(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    BodyWalk& walk = *static_cast<BodyWalk*>(data);
    if (EXPR_P(node) && EXPR_HAS_LOCATION(node)) {
        walk.last = EXPR_LOCATION(node);
    }
    switch (TREE_CODE(node)) {
        case REFLECT_EXPR:
        case FUNCTION_DECL:
        case VAR_DECL:
        case TEMPLATE_DECL:
        case OVERLOAD:
        case BASELINK:
            *walk_subtrees = 0;
            return NULL_TREE;
        case DECL_EXPR:
            walk_family_local(DECL_EXPR_DECL(node), walk);
            return NULL_TREE;
        case BIND_EXPR:
            for (tree variable = BIND_EXPR_VARS(node); variable != NULL_TREE; variable = DECL_CHAIN(variable)) {
                walk_family_local(variable, walk);
            }
            return NULL_TREE;
        default:
            break;
    }
    check_expression_rules(node, walk);
    if (TYPE_P(node)) {
        check_allocator_type(node, walk.last);
        *walk_subtrees = 0;
    }
    return NULL_TREE;
}

// The body of a function of a sentinel base file, which no walk of the
// quarantine reads.  Each body can hold a route (THE PROOF ROUTES), and a body
// with a refined parameter can hold a test of its predicate.  A lambda has a
// body of its own.  Complexity: O(nodes of the body).
void walk_family_body(tree function, location_t location, bool is_pattern) {
    if ((DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function))
        || (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function))
        || DECL_SAVED_TREE(function) == NULL_TREE) {
        return;
    }
    check_allocator_signature(TREE_TYPE(function), location);
    std::vector<RefinedParameter> refined = refined_parameters_of(function);
    hash_set<tree> visited;
    BodyWalk walk;
    walk.last = location;
    walk.is_pattern = is_pattern;
    walk.visited = &visited;
    walk.function = function;
    walk.refined = std::move(refined);
    cp_walk_tree(&DECL_SAVED_TREE(function), visit_family_node, &walk, &visited);
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

// ── The end of a unit (THE GRANTS) ──────────────────────────────────────

// The declaration that the qualified name QUALIFIED names, through each
// namespace, or null.
tree lookup_qualified(const std::string& qualified) {
    tree scope = global_namespace;
    std::size_t start = 0;
    while (true) {
        std::size_t end = qualified.find("::", start);
        std::string part = qualified.substr(start, end == std::string::npos ? std::string::npos : end - start);
        tree found = lookup_qualified_name(scope, get_identifier(part.c_str()), LOOK_want::NORMAL, false);
        if (found == NULL_TREE || found == error_mark_node) {
            return NULL_TREE;
        }
        if (end == std::string::npos) {
            return found;
        }
        if (TREE_CODE(found) != NAMESPACE_DECL) {
            return NULL_TREE;
        }
        scope = found;
        start = end + 2;
    }
}

// The enumerators of ENUMERATION that TYPE names: each constant of a
// template argument, through each template argument.  Complexity: O(nodes
// of the type tree).
void collect_granted(tree type, tree enumeration, std::unordered_map<std::string, bool>& granted, int depth) {
    if (type == NULL_TREE || depth > kTypeDepth || !TYPE_P(type)) {
        return;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || CLASSTYPE_TEMPLATE_INFO(main_type) == NULL_TREE) {
        return;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(CLASSTYPE_TI_ARGS(main_type));
    std::vector<tree> pending;
    for (int index = 0; arguments != NULL_TREE && index < TREE_VEC_LENGTH(arguments); ++index) {
        pending.push_back(TREE_VEC_ELT(arguments, index));
    }
    for (std::size_t index = 0; index < pending.size(); ++index) {
        tree argument = pending[index];
        if (argument == NULL_TREE) {
            continue;
        }
        if (ARGUMENT_PACK_P(argument)) {
            tree packed = ARGUMENT_PACK_ARGS(argument);
            for (int item = 0; item < TREE_VEC_LENGTH(packed); ++item) {
                pending.push_back(TREE_VEC_ELT(packed, item));
            }
        } else if (TYPE_P(argument)) {
            collect_granted(argument, enumeration, granted, depth + 1);
        } else if (TREE_CODE(argument) == INTEGER_CST && TYPE_MAIN_VARIANT(TREE_TYPE(argument)) == enumeration) {
            for (tree value = TYPE_VALUES(enumeration); value != NULL_TREE; value = TREE_CHAIN(value)) {
                tree constant = TREE_VALUE(value);
                tree initial = TREE_CODE(constant) == CONST_DECL ? DECL_INITIAL(constant) : constant;
                if (initial != NULL_TREE && tree_int_cst_equal(initial, argument)) {
                    granted[IDENTIFIER_POINTER(TREE_PURPOSE(value))] = true;
                }
            }
        }
    }
}

// The facts of a function of a door file: its facts of PLUGIN_PRE_GENERICIZE,
// or the facts of its pattern.  The memo holds each function one time.
const BodyFacts& facts_of_door(tree function, std::unordered_map<unsigned, BodyFacts>& patterns) {
    auto stored = door_facts.find(DECL_UID(function));
    if (stored != door_facts.end()) {
        return stored->second;
    }
    auto read = patterns.find(DECL_UID(function));
    if (read != patterns.end()) {
        return read->second;
    }
    BodyFacts& facts = patterns[DECL_UID(function)];
    tree pattern = function;
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_TEMPLATE_INFO(function) != NULL_TREE
        && TREE_CODE(DECL_TI_TEMPLATE(function)) == TEMPLATE_DECL) {
        tree general = most_general_template(DECL_TI_TEMPLATE(function));
        pattern = general != NULL_TREE ? DECL_TEMPLATE_RESULT(general) : function;
    }
    if (pattern != NULL_TREE && TREE_CODE(pattern) == FUNCTION_DECL && DECL_SAVED_TREE(pattern) != NULL_TREE) {
        tree body = DECL_SAVED_TREE(pattern);
        collect_facts(&body, pattern, DECL_SOURCE_LOCATION(pattern), facts);
    }
    return facts;
}

// The comparison of one grant row in a unit that read its grant file.
void finish_grant(const GrantRow& row, GrantUnit& unit) {
    const FileEntry* file = nullptr;
    for (const auto& [real, entry] : state.entries) {
        if (entry.grant == &row) {
            file = &entry;
        }
    }
    if (file == nullptr) {
        return;
    }
    Place head;
    head.file_class = file->file_class;
    head.file = file;
    head.line = 1;
    head.column = 1;
    tree alias = lookup_qualified(row.alias);
    tree enumeration_decl = lookup_qualified(row.enumeration);
    tree enumeration = enumeration_decl != NULL_TREE && TREE_CODE(enumeration_decl) == TYPE_DECL
                         ? TYPE_MAIN_VARIANT(TREE_TYPE(enumeration_decl))
                         : NULL_TREE;
    if (alias == NULL_TREE || TREE_CODE(alias) != TYPE_DECL || enumeration == NULL_TREE
        || TREE_CODE(enumeration) != ENUMERAL_TYPE) {
        add_finding(Kind::grant_unread, nullptr, head,
                    alias == NULL_TREE ? "no alias " + row.alias : "no enumeration " + row.enumeration);
        return;
    }
    Place alias_place = place_of(DECL_SOURCE_LOCATION(alias), Scope::quarantine);
    if (alias_place.file != file) {
        alias_place = head;
    }
    std::unordered_map<std::string, bool> catalog;
    std::unordered_map<long long, std::string> numbers;
    for (tree value = TYPE_VALUES(enumeration); value != NULL_TREE; value = TREE_CHAIN(value)) {
        std::string name = IDENTIFIER_POINTER(TREE_PURPOSE(value));
        catalog[name] = true;
        long long number = 0;
        if (macro_number("SYS_" + name, 0, number)) {
            numbers[number] = name;
        }
    }
    std::unordered_map<std::string, bool> granted;
    collect_granted(DECL_ORIGINAL_TYPE(alias) != NULL_TREE ? DECL_ORIGINAL_TYPE(alias) : TREE_TYPE(alias), enumeration,
                    granted, 0);
    // The system calls that the code issues, with the place of the first
    // site, in the order of the sites.
    std::vector<std::pair<std::string, location_t>> issued;
    std::unordered_map<std::string, bool> is_issued;
    auto issue = [&](const std::string& name, location_t location) {
        if (!is_issued[name]) {
            is_issued[name] = true;
            issued.emplace_back(name, location);
        }
    };
    auto read_facts = [&](const BodyFacts& facts, location_t at, bool is_own) {
        for (const NamedFunction& named : facts.named) {
            if (named.is_library && catalog.count(named.name) != 0) {
                issue(named.name, is_own ? named.location : at);
            }
            std::string base = named.name.size() > 4 ? named.name.substr(0, named.name.size() - 4) : std::string{};
            if (!base.empty() && named.name.compare(named.name.size() - 4, 4, "_sys") == 0 && catalog.count(base) != 0) {
                issue(base, is_own ? named.location : at);
            }
        }
        for (const SyscallCall& call : facts.syscalls) {
            auto named = call.has_number ? numbers.find(call.number) : numbers.end();
            if (named == numbers.end()) {
                continue;
            }
            if (call.helper.empty()) {
                issue(named->second, is_own ? call.location : at);
            } else if (call.helper != named->second + "_sys") {
                Place place = place_of(call.location, Scope::quarantine);
                if (place.file != nullptr && (place.file->grant == &row || place.file->grant_door == &row)) {
                    add_finding(Kind::grant_wrong_helper, nullptr, place, call.helper + ": SYS_" + named->second);
                }
            }
        }
    };
    read_facts(unit.facts, UNKNOWN_LOCATION, true);
    // The closure of the functions of the door files, from each call of the
    // grant file: each call stands for each system call of its function and
    // of each function of a door file that the function calls.
    std::unordered_map<unsigned, BodyFacts> patterns;
    for (const auto& [door, location] : unit.facts.doors) {
        std::vector<tree> pending{door};
        std::unordered_set<unsigned> seen{DECL_UID(door)};
        for (std::size_t index = 0; index < pending.size(); ++index) {
            const BodyFacts& facts = facts_of_door(pending[index], patterns);
            read_facts(facts, location, false);
            for (const auto& [callee, unused] : facts.doors) {
                if (seen.insert(DECL_UID(callee)).second) {
                    pending.push_back(callee);
                }
            }
        }
    }
    // The wrong helpers of each function of a door file of the row.
    for (const auto& [uid, facts] : door_facts) {
        for (const SyscallCall& call : facts.syscalls) {
            auto named = call.has_number ? numbers.find(call.number) : numbers.end();
            if (named != numbers.end() && !call.helper.empty() && call.helper != named->second + "_sys") {
                Place place = place_of(call.location, Scope::quarantine);
                if (place.file != nullptr && place.file->grant_door == &row) {
                    add_finding(Kind::grant_wrong_helper, nullptr, place, call.helper + ": SYS_" + named->second);
                }
            }
        }
    }
    if (granted.empty() || issued.empty()) {
        add_finding(Kind::grant_unread, nullptr, alias_place, granted.empty() ? "no grant" : "no call");
        return;
    }
    for (const auto& [name, location] : issued) {
        if (granted.count(name) == 0) {
            Place place = place_of(location, Scope::quarantine);
            add_finding(Kind::grant_not_granted, nullptr, place.file != nullptr ? place : alias_place, name);
        }
    }
    std::vector<std::string> sorted_grants;
    for (const auto& [name, unused] : granted) {
        sorted_grants.push_back(name);
    }
    std::sort(sorted_grants.begin(), sorted_grants.end());
    for (const std::string& name : sorted_grants) {
        if (is_issued.count(name) == 0) {
            add_finding(Kind::grant_not_issued, nullptr, alias_place, name);
        }
    }
    add_finding(Kind::grant_compared, nullptr, alias_place,
                std::to_string(issued.size()) + " issued, " + std::to_string(granted.size()) + " granted");
}

// ── The declarations and the types of the family (THE PROOF ROUTES) ─────

// PLUGIN_FINISH_TYPE gives each class that the source defines, also a local
// one, an anonymous one, the pattern of a template and an explicit or partial
// specialization, and no implicit instantiation.  Each union definition is a
// route.  The bases and the member aliases of a class can hold an allocator.
// A class of include/ can befriend a class template, and a class template can
// have a parameter that is no type.
void on_finish_route_type(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !CLASS_TYPE_P(type)) {
        return;
    }
    tree decl = TYPE_MAIN_DECL(type);
    location_t location = decl != NULL_TREE ? DECL_SOURCE_LOCATION(decl) : input_location;
    if (TREE_CODE(type) == UNION_TYPE) {
        tree name = decl != NULL_TREE ? DECL_NAME(decl) : NULL_TREE;
        bool is_named = name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE && !IDENTIFIER_ANON_P(name);
        record_route(Kind::route_union, location, is_named ? IDENTIFIER_POINTER(name) : "<anonymous>");
        return;
    }
    if (tree binfo = TYPE_BINFO(type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            check_allocator_type(BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), location);
        }
    }
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == TYPE_DECL && !DECL_ARTIFICIAL(member) && DECL_ORIGINAL_TYPE(member) != NULL_TREE) {
            check_allocator_type(DECL_ORIGINAL_TYPE(member), DECL_SOURCE_LOCATION(member));
        }
    }
    note_route_templates(type, location);
    check_class_specialization(type);
}

// PLUGIN_FINISH_DECL gives each declaration that cp_finish_decl finishes: a
// variable, a data member and a declaration of a function.  cp_finish_decl
// returns before the hook for an alias declaration, and check_route_alias
// reads an alias from the walk of the declarations.  The allocator route of
// its type, and an explicit specialization of a function template or a
// variable template.  A type that holds no class template instance costs two
// tests.
void on_finish_route_decl(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || decl == error_mark_node || !DECL_P(decl) || DECL_ARTIFICIAL(decl)) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case VAR_DECL:
        case FIELD_DECL:
            check_allocator_type(TREE_TYPE(decl), DECL_SOURCE_LOCATION(decl));
            break;
        case FUNCTION_DECL:
            check_allocator_signature(TREE_TYPE(decl), DECL_SOURCE_LOCATION(decl));
            break;
        default:
            break;
    }
    check_decl_specialization(decl);
}

// PLUGIN_FINISH_PARSE_FUNCTION gives each function definition: an explicit
// specialization of a function template.
void on_finish_route_function(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function != NULL_TREE && TREE_CODE(function) == FUNCTION_DECL) {
        check_decl_specialization(function);
    }
}

void on_route_collection(void*, void*) {
    caches.proof_class.clear();
    caches.held_proof.clear();
    caches.allocator_proof.clear();
}

// The key of an explicit instantiation: the instantiated class, or the
// instantiated function or variable with its scope and each of its template
// arguments (library.cpp, kPrintFlags).
std::string instantiation_key(tree entity) {
    return TYPE_P(entity) ? type_text(TYPE_MAIN_VARIANT(entity)) : declaration_text(entity);
}

// PLUGIN_EXPLICIT_INSTANTIATION gives each explicit instantiation directive
// that the parser reads, with the place of its first token: the route
// explicit-instantiation.
void on_explicit_instantiation(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_explicit_instantiation_data*>(gcc_data);
    if (data == nullptr || data->entity == NULL_TREE || data->entity == error_mark_node) {
        return;
    }
    record_route(Kind::route_explicit_instantiation, data->location, instantiation_key(data->entity));
}

// PLUGIN_SIZEOF_OR_ALIGNOF gives each sizeof and alignof that the parser
// reads, before GCC folds it.  The plugin keeps the place of each sizeof of a
// type that holds a proof type, for check_raw_allocation.  The operand is a
// type, or an expression with a type.  A sizeof that the parser reads one
// more time, when it parses tentatively, adds one more place of the same
// tokens, and a call has one finding.  Complexity: the cached type walk of
// held_proof_of for each sizeof.
void on_sizeof_or_alignof(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_sizeof_or_alignof_data*>(gcc_data);
    if (data == nullptr || data->code != SIZEOF_EXPR || data->operand == NULL_TREE
        || data->operand == error_mark_node) {
        return;
    }
    tree operand = data->operand;
    tree type = TYPE_P(operand) ? operand : TREE_TYPE(tree_strip_any_location_wrapper(operand));
    if (type == NULL_TREE || !TYPE_P(type)) {
        return;
    }
    if (tree proof = held_proof_of(type, 0)) {
        proof_sizes.push_back(ProofSize{data->location, qualified_name(TYPE_MAIN_DECL(proof))});
    }
}

}  // namespace

// An alias of a namespace or of a class that the walk of the declarations
// reads (check_alias in declaration.cpp): the allocator route of the type
// that it names.  The census keeps one finding for the member alias that
// PLUGIN_FINISH_TYPE also reads.
void check_route_alias(tree decl) {
    if (decl != NULL_TREE && TREE_CODE(decl) == TYPE_DECL && !DECL_ARTIFICIAL(decl)) {
        check_allocator_type(DECL_ORIGINAL_TYPE(decl), DECL_SOURCE_LOCATION(decl));
    }
}

// One walk of a body or an initializer, with its own set of visited nodes.
void walk_root(tree* root, location_t location, bool is_pattern) {
    walk_root_of(root, location, is_pattern, NULL_TREE, {}, grant_row_at(location));
}

// The signature, and the body when IS_PATTERN or when the plugin reads it
// from PLUGIN_PRE_GENERICIZE.  The body of a static initializer has places in
// each file, so it gets no location gate.  A function of a door file that is
// not a template keeps the facts of its body (THE GRANTS).
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
    location_t location = DECL_SOURCE_LOCATION(function);
    Place place = place_of(location, Scope::quarantine);
    if (!is_pattern && place.file != nullptr && place.file->grant_door != nullptr
        && DECL_SAVED_TREE(function) != NULL_TREE) {
        collect_facts(&DECL_SAVED_TREE(function), function, location, door_facts[DECL_UID(function)]);
    }
    if (!is_worth_a_walk_at(place, location)) {
        if (place.file != nullptr && place.file->is_sentinel) {
            walk_family_body(function, location, is_pattern);
        }
        return;
    }
    if (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function)) {
        return;
    }
    check_allocator_signature(TREE_TYPE(function), location);
    check_signature(function, is_pattern);
    if (is_pattern) {
        walk_constraints(function);
    }
    if (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function)) {
        return;
    }
    tree body = DECL_SAVED_TREE(function);
    if (body != NULL_TREE) {
        const GrantRow* grant = place.file != nullptr ? place.file->grant : nullptr;
        if (grant != nullptr) {
            grant_units[grant].is_seen = true;
        }
        walk_root_of(&body, location, is_pattern, function, refined_parameters_of(function), grant);
    }
}

// The pattern of a function of a sentinel base file, which no walk of the
// quarantine reads (THE SENTINEL of plugin.h).  The walk of the family
// declaration property gives each function of each namespace, and a unit
// with no sentinel base file pays one test.
void walk_family_pattern(tree function) {
    if (!has_sentinel_file()) {
        return;
    }
    const char* file = DECL_SOURCE_FILE(function);
    if (file != nullptr && classify_file(file).is_sentinel) {
        walk_body(function, true);
    }
}

// The comparison of each grant row whose grant file the unit read (THE
// GRANTS), and the specializations of the templates that THE PROOF ROUTES
// noted.  Complexity: O(specializations of the noted templates).
void finish_expression_rules() {
    for (const GrantRow& row : state.table.grants) {
        auto unit = grant_units.find(&row);
        if (unit != grant_units.end() && unit->second.is_seen) {
            finish_grant(row, unit->second);
        }
    }
    for (tree befriended : route_templates.befriended) {
        read_friend_specializations(befriended);
    }
    for (tree template_decl : route_templates.value_parameters) {
        read_value_specializations(template_decl);
    }
}

// Registers the walk of each function body and the hooks of THE PROOF ROUTES.
void register_expressions(const char* plugin_name) {
    names.refinement = get_identifier("Refinement");
    names.decide = get_identifier("decide");
    names.syscall = get_identifier("syscall");
    for (const CiteRow& cite : state.table.cites) {
        for (const std::string& procedure : cite.procedures) {
            names.cited.insert(get_identifier(procedure.c_str()));
        }
    }
    names.bit_cast = get_identifier("bit_cast");
    names.allocator = get_identifier("allocator");
    names.allocator_traits = get_identifier("allocator_traits");
    names.polymorphic_allocator = get_identifier("polymorphic_allocator");
    names.foundation = get_identifier("foundation");
    names.fixy = get_identifier("fixy");
    names.static_cast_keyword = get_identifier("static_cast");
    names.reinterpret_cast_keyword = get_identifier("reinterpret_cast");
    for (const char* allocator : {"malloc", "calloc", "realloc", "aligned_alloc"}) {
        names.allocators.insert(get_identifier(allocator));
    }
    for (const WitnessRow& witness : state.table.witnesses) {
        names.witnessed.insert(witness.name);
        std::size_t last = witness.name.rfind("::");
        names.witnessed_last_names.insert(
            get_identifier(last == std::string::npos ? witness.name.c_str() : witness.name.c_str() + last + 2));
    }
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_route_type, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_route_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_route_function, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_route_collection, nullptr);
    register_callback(plugin_name, PLUGIN_EXPLICIT_INSTANTIATION, on_explicit_instantiation, nullptr);
    register_callback(plugin_name, PLUGIN_SIZEOF_OR_ALIGNOF, on_sizeof_or_alignof, nullptr);
}

}  // namespace crucible_plugin
