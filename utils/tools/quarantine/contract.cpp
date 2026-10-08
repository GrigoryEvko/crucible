// The contract rule of the tree.
//
// A P2900 contract specifier (`pre` or `post` on a function declaration) is
// not permitted, and CRUCIBLE_PRE and CRUCIBLE_POST of foundation/contracts/
// replace it.  GCC 16 does not keep the specifier of a template in a header
// unit or a precompiled header, and a constant evaluation can ignore the
// specifier (CLAUDE.md section XII).  The rule applies to each file under the
// source root, of each class.  Each specifier that no region opts out is an
// error, in each mode.
//
// THE HOOKS
//     PLUGIN_FINISH_DECL     each declaration outside a template, also a
//                            local one.
//     PLUGIN_FINISH_PARSE_FUNCTION
//                            each function definition, also a template, a
//                            lambda and a member of a local class.
//     PLUGIN_FINISH_UNIT     one walk of every namespace, also of a library
//                            namespace that a file under the root opens again.
//                            It finds a template and a member of a class
//                            template that has no definition.
//
// The rule cannot see a specifier in a preprocessor arm that the unit does not
// compile, or on a member function of a local class in a template when the
// class declares the function and does not define it.  The rule contract-form
// (utils/assay/guard/engines/tsast/contract_form.py) reads the parse tree of
// each tracked file, and it finds these specifiers too.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// One contract specifier.  The spelling location decides whether the rule
// applies, and one finding stays for each spelling: a template and its
// instantiations share the specifier, and so do the uses of one macro.
void record_contract(bool is_precondition, location_t location) {
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION || in_library_file_at(location)) {
        return;
    }
    location_t spelled = linemap_resolve_location(line_table, location, LRK_SPELLING_LOCATION, nullptr);
    expanded_location where = expand_location(spelled);
    if (where.file == nullptr) {
        return;
    }
    const FileEntry& entry = classify_file(where.file);
    if (entry.file_class == FileClass::outside) {
        return;
    }
    std::string key = std::string(kind_name(Kind::contract_specifier)) + ' ' + entry.relative + ':'
                    + std::to_string(where.line) + ':' + std::to_string(where.column);
    if (!state.finding_keys.insert(key).second) {
        return;
    }
    Finding finding;
    finding.kind = Kind::contract_specifier;
    finding.file = &entry;
    finding.line = where.line;
    finding.column = where.column;
    finding.spelling = location;
    finding.entity = is_precondition ? "pre" : "post";
    state.findings.push_back(std::move(finding));
}

// Each contract specifier of a function.  The front end keeps the specifiers
// of a declaration in a table of its own, and each node of the list holds one
// PRECONDITION_STMT or POSTCONDITION_STMT.
void check_contracts(tree decl) {
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    if (decl == NULL_TREE || TREE_CODE(decl) != FUNCTION_DECL) {
        return;
    }
    for (tree specifier = get_fn_contract_specifiers(decl); specifier != NULL_TREE; specifier = TREE_CHAIN(specifier)) {
        if (TREE_CODE(specifier) != TREE_LIST || TREE_VALUE(specifier) == NULL_TREE
            || TREE_CODE(TREE_VALUE(specifier)) != TREE_LIST) {
            continue;
        }
        tree statement = CONTRACT_STATEMENT(specifier);
        if (statement == NULL_TREE || !CONTRACT_CONDITION_P(statement)) {
            continue;
        }
        location_t location = EXPR_LOCATION(statement);
        record_contract(PRECONDITION_P(statement),
                        location != UNKNOWN_LOCATION ? location : DECL_SOURCE_LOCATION(decl));
    }
}

void walk_contract_class(tree type);

void walk_contract_template(tree template_decl) {
    if (!state.contract_walked.insert(template_decl).second) {
        return;
    }
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result == NULL_TREE) {
        return;
    }
    if (TREE_CODE(result) == FUNCTION_DECL) {
        check_contracts(result);
        return;
    }
    if (TREE_CODE(result) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(result) || !CLASS_TYPE_P(TREE_TYPE(result))) {
        return;
    }
    walk_contract_class(TREE_TYPE(result));
    // The list of a class template holds its partial specializations.
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
            walk_contract_template(partial);
        }
    }
}

void walk_contract_decl(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)
        || in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case FUNCTION_DECL:
            check_contracts(decl);
            break;
        case TEMPLATE_DECL:
            walk_contract_template(decl);
            break;
        case TYPE_DECL:
            if (DECL_IMPLICIT_TYPEDEF_P(decl) && !DECL_SELF_REFERENCE_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                walk_contract_class(TREE_TYPE(decl));
            }
            break;
        default:
            break;
    }
}

// The members of a class, its nested classes and member templates, and the
// friends that a class template declares.  An implicit instantiation is not
// walked: it shares each specifier with its template.
void walk_contract_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || LAMBDA_TYPE_P(main_type) || !state.contract_walked.insert(main_type).second) {
        return;
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (TREE_CODE(member) == FIELD_DECL) {
            if (DECL_NAME(member) == NULL_TREE && ANON_AGGR_TYPE_P(TREE_TYPE(member))) {
                walk_contract_class(TREE_TYPE(member));
            }
            continue;
        }
        walk_contract_decl(member);
    }
    for (tree entry = CLASSTYPE_DECL_LIST(main_type); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        if (TREE_PURPOSE(entry) == NULL_TREE && TREE_VALUE(entry) != NULL_TREE && DECL_P(TREE_VALUE(entry))) {
            walk_contract_decl(TREE_VALUE(entry));
        }
    }
}

void on_finish_decl(void* gcc_data, void*) { check_contracts(static_cast<tree>(gcc_data)); }

void on_finish_parse_function(void* gcc_data, void*) { check_contracts(static_cast<tree>(gcc_data)); }

}  // namespace

// One walk of each namespace.  The two other hooks of the rule see each
// function definition and each declaration outside a template.  This walk
// finds a function template, a member template and a member of a class
// template that has no definition.  A file under the root can open a library
// namespace too, such as std, so the walk reads each namespace and skips each
// declaration of a library file.
void walk_contract_namespace(tree ns) {
    if (!state.contract_walked.insert(ns).second) {
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
                walk_contract_decl(*candidate);
            }
            continue;
        }
        if (TREE_CODE(member) == NAMESPACE_DECL) {
            if (DECL_NAMESPACE_ALIAS(member) == NULL_TREE) {
                walk_contract_namespace(member);
            }
            continue;
        }
        walk_contract_decl(member);
    }
}

// The diagnostic of one contract specifier that no region opts out.
void report_contract(const Finding& finding) {
    bool is_precondition = finding.entity == "pre";
    error_at(finding.spelling, "the %qs contract specifier is not permitted in this tree: write %<%s%> of %<%s%> %s",
             finding.entity.c_str(), is_precondition ? "CRUCIBLE_PRE(condition)" : "CRUCIBLE_POST(result, condition)",
             is_precondition ? "foundation/contracts/Pre.h" : "foundation/contracts/Post.h",
             is_precondition ? "as the first statement of the function body" : "before each return statement");
    inform(finding.spelling,
           "GCC 16 does not keep the contract specifier of a template in a header unit or in a precompiled "
           "header, and a constant evaluation can ignore the specifier (CLAUDE.md section XII)");
    inform(finding.spelling,
           "a test of the specifier itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region "
           "of %<foundation/Quarantine.h%>");
}

// Registers the hooks of the contract rule.
void register_contract_rule(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_parse_function, nullptr);
}

}  // namespace crucible_plugin
