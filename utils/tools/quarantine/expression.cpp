// The family expression shape: the walk of a body, and the rules
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
};

Names names;

// ── The findings of the family ──────────────────────────────────────────

// A finding of a rule with a base scope: it counts in a quarantined file, and
// in a base file only in the sentinel unit of that file (counts_in_unit).
void record_in_unit(Kind kind, location_t location, const std::string& entity) {
    Place place = place_of(location, Scope::quarantine);
    if (counts_in_unit(place)) {
        add_finding(kind, nullptr, place, entity);
    }
}

// ── The callees of a call ───────────────────────────────────────────────

// Calls VISIT for each function that the callee CALLEE of a call can name:
// the function itself, each candidate of an overload set and the function of
// a template.  A template keeps the set of a dependent call.
template <typename Visit>
void for_each_function(tree callee, Visit&& visit, int depth = 0) {
    if (callee == NULL_TREE || depth > 8) {
        return;
    }
    switch (TREE_CODE(callee)) {
        case FUNCTION_DECL:
            visit(callee);
            return;
        case TEMPLATE_DECL:
            if (tree result = DECL_TEMPLATE_RESULT(callee); result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL) {
                visit(result);
            }
            return;
        case ADDR_EXPR:
        case TEMPLATE_ID_EXPR:
            for_each_function(TREE_OPERAND(callee, 0), visit, depth + 1);
            return;
        case COMPONENT_REF:
            for_each_function(TREE_OPERAND(callee, 1), visit, depth + 1);
            return;
        case BASELINK:
            for_each_function(BASELINK_FUNCTIONS(callee), visit, depth + 1);
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(callee, true); candidate; ++candidate) {
                for_each_function(*candidate, visit, depth + 1);
            }
            return;
        default:
            return;
    }
}

// The callee tree of a call: the function of a CALL_EXPR or of an
// AGGR_INIT_EXPR.
tree callee_of(tree call) { return TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_FN(call) : CALL_EXPR_FN(call); }

int argument_count(tree call) {
    return TREE_CODE(call) == AGGR_INIT_EXPR ? aggr_init_expr_nargs(call) : call_expr_nargs(call);
}

tree argument_at(tree call, int index) {
    return TREE_CODE(call) == AGGR_INIT_EXPR ? AGGR_INIT_EXPR_ARG(call, index) : CALL_EXPR_ARG(call, index);
}

// True when FUNCTION is a function of the global namespace of a library
// header named NAME.
bool is_library_function(tree function, tree name) {
    return DECL_NAME(function) == name && library_of(function) == Library::c_library;
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

// The number of the first argument of a call of syscall(), through its
// location wrappers and conversions.
bool syscall_number_of(tree call, long long& number) {
    if (argument_count(call) == 0) {
        return false;
    }
    tree argument = argument_at(call, 0);
    for (int depth = 0; argument != NULL_TREE && depth < kTypeDepth; ++depth) {
        argument = tree_strip_any_location_wrapper(argument);
        if (TREE_CODE(argument) != NOP_EXPR && TREE_CODE(argument) != CONVERT_EXPR
            && TREE_CODE(argument) != NON_LVALUE_EXPR) {
            break;
        }
        argument = TREE_OPERAND(argument, 0);
    }
    if (argument == NULL_TREE || TREE_CODE(argument) != INTEGER_CST || !tree_fits_shwi_p(argument)) {
        return false;
    }
    number = tree_to_shwi(argument);
    return true;
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

// ── The walk of a body ──────────────────────────────────────────────────

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

// The expression shape of the rule of THE REFINED PARAMETERS at one node.
void check_expression_rules(tree node, BodyWalk& walk) {
    bool is_call = TREE_CODE(node) == CALL_EXPR || TREE_CODE(node) == AGGR_INIT_EXPR;
    if (is_call && !walk.refined.empty()) {
        check_retest(node, EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : walk.last, walk);
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

// A local declaration of a body that only the family reads.  A template keeps
// the initializer of a local in DECL_INITIAL, and cp_walk_tree does not reach
// it outside template processing.  A member function of a local class of a
// template is a pattern that no PLUGIN_PRE_GENERICIZE gives.
void walk_family_local(tree decl, BodyWalk& walk) {
    if (decl == NULL_TREE || !walk.is_pattern) {
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
        *walk_subtrees = 0;
    }
    return NULL_TREE;
}

// The body of a function of a sentinel base file, which no walk of the
// quarantine reads.  A body with no refined parameter holds no finding of
// the family, other than in a local class of a pattern.  A lambda has a body
// of its own.  Complexity: O(nodes of the body).
void walk_family_body(tree function, location_t location, bool is_pattern) {
    if (names.cited.empty() || (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function))
        || (DECL_LANG_SPECIFIC(function) != nullptr && DECL_DEFAULTED_FN(function))
        || DECL_SAVED_TREE(function) == NULL_TREE) {
        return;
    }
    std::vector<RefinedParameter> refined = refined_parameters_of(function);
    if (refined.empty() && !is_pattern) {
        return;
    }
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

// The value of the object-like macro NAME, through each macro that its one
// token names, as libcpp holds it.
bool macro_number(const std::string& name, int depth, long long& number) {
    if (depth > 8 || !cpp_defined(parse_in, reinterpret_cast<const unsigned char*>(name.c_str()),
                                  static_cast<int>(name.size()))) {
        return false;
    }
    cpp_hashnode* node = cpp_lookup(parse_in, reinterpret_cast<const unsigned char*>(name.c_str()),
                                    static_cast<unsigned>(name.size()));
    if (node == nullptr || !cpp_user_macro_p(node) || node->value.macro == nullptr) {
        return false;
    }
    const cpp_macro* macro = node->value.macro;
    if (macro->fun_like || macro->kind != cmk_macro || macro->count != 1) {
        return false;
    }
    const cpp_token& token = macro->exp.tokens[0];
    if (token.type == CPP_NAME) {
        return macro_number(reinterpret_cast<const char*>(NODE_NAME(token.val.node.node)), depth + 1, number);
    }
    if (token.type != CPP_NUMBER || token.val.str.len == 0) {
        return false;
    }
    long long value = 0;
    for (unsigned index = 0; index < token.val.str.len; ++index) {
        unsigned char digit = token.val.str.text[index];
        if (digit < '0' || digit > '9' || value > (LLONG_MAX - 9) / 10) {
            return false;
        }
        value = value * 10 + (digit - '0');
    }
    number = value;
    return true;
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

}  // namespace

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
    if (names.cited.empty() || !has_sentinel_file()) {
        return;
    }
    const char* file = DECL_SOURCE_FILE(function);
    if (file != nullptr && classify_file(file).is_sentinel) {
        walk_body(function, true);
    }
}

// The comparison of each grant row whose grant file the unit read (THE
// GRANTS).
void finish_expression_rules() {
    for (const GrantRow& row : state.table.grants) {
        auto unit = grant_units.find(&row);
        if (unit != grant_units.end() && unit->second.is_seen) {
            finish_grant(row, unit->second);
        }
    }
}

// Registers the walk of each function body.
void register_expressions(const char* plugin_name) {
    names.refinement = get_identifier("Refinement");
    names.decide = get_identifier("decide");
    names.syscall = get_identifier("syscall");
    for (const CiteRow& cite : state.table.cites) {
        for (const std::string& procedure : cite.procedures) {
            names.cited.insert(get_identifier(procedure.c_str()));
        }
    }
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize, nullptr);
}

}  // namespace crucible_plugin
