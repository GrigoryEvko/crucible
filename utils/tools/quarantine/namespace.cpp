// The family base namespace: a file outside the base declares nothing in a
// namespace of the base.  C++ lets each unit open a namespace again and add to
// it.  A new overload in a namespace of the base changes which function a call
// of the base reaches, and the body of a function that the base only declares
// replaces the code of the base, such as with a body that has no bounds check.
// So the rule base-namespace refuses each declaration in a namespace of the
// base that a file outside the base spells.
//
// THE NAMESPACES
//     A namespace of the base is the namespace foundation or fixy of the global
//     namespace, and each namespace inside it: a named, an inline and an
//     unnamed namespace.  A namespace with one of these names inside a
//     different namespace is no namespace of the base.
//
// THE FILES
//     A file of the base is a file that a layer row of the rule table holds,
//     and the check file of a header of the base, which stands where its header
//     stands (home.cpp, THE PLACE).  The rule judges each other file of the
//     source root and of the build directory, other than a host file and a
//     kernel file.  The place of a site is the expansion point of its location
//     (home.cpp, THE PLACE), so a declaration that a macro of the base makes in
//     a different file stands in that file.
//
// THE KINDS
//     declaration       a declaration of a namespace of the base: a function, a
//                       variable, a class, an enumeration, a concept, an alias,
//                       a typedef, a deduction guide, a namespace alias and a
//                       using-declaration.  A redeclaration of an entity of the
//                       base and the definition of an entity that the base only
//                       declares are sites too, and so is the definition of a
//                       member of a class of the base outside the class.  Its
//                       entity is the qualified name of what it declares
//     using-directive   a using-directive of a namespace of the base, and a
//                       using-directive of the global namespace when the unit
//                       reads a file of the base after it.  An unqualified
//                       lookup of the base that reaches the global namespace
//                       reads each name that the directive nominates.  Its
//                       entity is the namespace that it nominates
//     An explicit or a partial specialization, and an explicit specialization
//     of a member, is no site: the rules home and specialization-home judge it
//     (home.cpp and specialization.cpp).  So a coherent specialization of a
//     template with the open mark is the one extension of the base that a file
//     outside the base can write.  An explicit instantiation is no site, and
//     the rule proof-routes reads it.  A friend declaration is no site, because
//     the class that holds it is the site, or it names an entity that exists.
//     A member of a class that the same file defines is no site of its own,
//     because the class is the site.  A using-directive in a function body or
//     in a different namespace is no site, because no lookup of the base reads
//     it.
//
// THE READS
//     GCC keeps one location for an entity, and a redeclaration merges into
//     the first declaration.  The location moves to a definition, and it stays
//     at the first declaration for each other redeclaration.  So the family
//     reads each declaration when the parser completes it, and it walks the
//     namespaces of the base at the end of the parse:
//       PLUGIN_FINISH_DECL            a variable, a function with no body, a
//                                     typedef and an alias.  When the location
//                                     of the declaration is in a file of the
//                                     base, the declaration merged into a
//                                     declaration of the base, and the place
//                                     is the last token that the parser read
//       PLUGIN_FINISH_PARSE_FUNCTION  a function definition
//       PLUGIN_FINISH_TYPE            a class definition and an enumeration
//       PLUGIN_USING_FORM             a using-declaration, a using-directive
//                                     and a namespace alias
//       the walk at the end           each declaration of the namespaces of the
//                                     base, at the location that GCC keeps,
//                                     such as a concept, a forward declaration
//                                     of a new class and a namespace alias
//     One site gives one finding, because the key of a finding holds its place
//     and its entity (add_finding of plugin.h).
//
// WHAT THE FAMILY DOES NOT READ
//     A forward declaration of a class or of a class template gives no event,
//     and GCC moves the location of a class to its definition.  So a forward
//     declaration of a class that a file of the base defines later is no site.
//     Such a declaration can add a default template argument.  A declaration
//     of the global namespace before a file of the base is no site, such as an
//     overload that a qualified call ::name of the base can reach.  The control
//     control-known-gaps of utils/plants/base-namespace pins the two gaps.
//
// THE ERRORS
//     Each finding is a compile error in each build and each mode
//     (is_each_mode_kind of plugin.h).  The message of an error does not start
//     with "quarantine: ", so the driver of the negative fixtures keeps it, and
//     a regex of a fixture can read it.  A test of a planted declaration puts
//     it in an opt-out region of the reason class PROBE:, and the rule
//     quarantine counts the opted-out finding with the reason of the region.
//
// THE ACTIVATION
//     The argument base-namespace=on turns on the rule (plugin.cpp), and
//     Quarantine.cmake gives it to each compile of the tree.  A compile of a
//     plant of a different rule has no such argument.

#include "plugin.h"

namespace crucible_plugin {

namespace {

bool is_active = false;

// The names of the two roots of the base (THE NAMESPACES).
struct Names {
    tree foundation = NULL_TREE;
    tree fixy = NULL_TREE;
};

Names names;

// A using-directive of the global namespace in a judged file, which the end
// of the parse judges against the files of the base that the unit reads after
// it (THE KINDS).
struct GlobalDirective {
    location_t location = UNKNOWN_LOCATION;
    std::string entity;
};

std::vector<GlobalDirective> global_directives;

// The namespaces that the walk at the end read, so that each one is read one
// time.
std::unordered_set<tree> walked_namespaces;

// True when the rule judges a declaration in FILE (THE FILES): a file of the
// root or of the build directory that is not of the base, not a host file
// and not a kernel file.  A check file of a header of the base counts as its
// header.  Complexity: O(rows) on the first request for each file.
bool is_judged_file(const FileEntry* file) {
    if (file == nullptr) {
        return false;
    }
    switch (file->file_class) {
        case FileClass::base:
        case FileClass::exempt:
        case FileClass::outside:
            return false;
        case FileClass::quarantined:
        case FileClass::unclassified:
            break;
    }
    static std::unordered_map<const FileEntry*, bool> judged;
    auto found = judged.find(file);
    if (found != judged.end()) {
        return found->second;
    }
    std::string path = read_as(file->relative);
    bool is_base_header = false;
    if (path != file->relative) {
        for (const LayerRow& layer : state.table.layers) {
            for (const std::string& layer_path : layer.paths) {
                is_base_header = is_base_header || row_holds(layer_path, path);
            }
        }
    }
    return judged.emplace(file, !is_base_header).first->second;
}

// True when the place of LOCATION stands in a judged file.  It reads the file
// of the line map first, because most declarations stand in a file of the
// base.
bool is_judged_at(location_t location) { return is_judged_file(file_at(location)); }

// One finding of KIND at LOCATION for ENTITY, when the place is in a judged
// file.
void record_site(Kind kind, location_t location, const std::string& entity) {
    if (!is_judged_at(location)) {
        return;
    }
    Place place = site_place(location);
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// True when TYPE is an instance or a specialization of a class template.  The
// rules home and specialization-home judge such a class (THE KINDS).
bool is_template_class_instance(tree type) {
    return type != NULL_TREE && CLASS_TYPE_P(type) && CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE
        && CLASSTYPE_USE_TEMPLATE(type) != 0;
}

// True when DECL is no site of its own (THE KINDS): an artificial
// declaration, an instantiation, a specialization, a friend that an instance
// injects, and a function that a friend declaration declares.
bool is_no_site(tree decl) {
    if (decl == NULL_TREE || is_erroneous_decl(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return true;
    }
    if (TREE_CODE(decl) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(decl)) {
        tree type = TREE_TYPE(decl);
        return (CLASS_TYPE_P(type) && (LAMBDA_TYPE_P(type) || is_template_class_instance(type)));
    }
    if (DECL_ARTIFICIAL(decl)) {
        return true;
    }
    if (!VAR_OR_FUNCTION_DECL_P(decl) || DECL_LANG_SPECIFIC(decl) == nullptr) {
        return false;
    }
    if (DECL_TEMPLATE_SPECIALIZATION(decl) || DECL_TEMPLATE_INSTANTIATION(decl)) {
        return true;
    }
    // The pattern of a template is the result of its template.  Each other
    // declaration with template information is a friend that an instance
    // injects, or a member of an instance.
    tree info = DECL_TEMPLATE_INFO(decl);
    if (info != NULL_TREE) {
        tree template_decl = TI_TEMPLATE(info);
        if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL
            || DECL_TEMPLATE_RESULT(template_decl) != decl) {
            return true;
        }
    }
    return TREE_CODE(decl) == FUNCTION_DECL && DECL_FRIEND_CONTEXT(decl) != NULL_TREE;
}

// The site of DECL at LOCATION, when DECL is a declaration of a namespace of
// the base or a member of a class of the base (THE KINDS).  A member of a
// class that a judged file defines is no site, because the class is the site,
// and a member of an instance or of a specialization is no site either.  When
// LOCATION is no judged place and MAY_READ_PARSER holds, the place is the last
// token that the parser read (THE READS).
void judge_declaration(tree decl, location_t location, bool may_read_parser) {
    if (is_no_site(decl)) {
        return;
    }
    tree class_scope = NULL_TREE;
    tree ns = enclosing_namespace_of(decl, class_scope);
    if (!in_base_namespace(ns)) {
        return;
    }
    if (class_scope != NULL_TREE) {
        if (is_template_class_instance(class_scope) || TYPE_MAIN_DECL(class_scope) == NULL_TREE
            || is_judged_at(DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(class_scope)))) {
            return;
        }
    }
    if (is_judged_at(location)) {
        record_site(Kind::base_namespace_declaration, location, qualified_name(decl));
    } else if (may_read_parser) {
        record_site(Kind::base_namespace_declaration, input_location, qualified_name(decl));
    }
}

// ── The hooks ───────────────────────────────────────────────────────────

// PLUGIN_FINISH_DECL: a variable, a function with no body, a typedef and an
// alias.  A function of a namespace that a class body completes is a friend,
// and no site (THE KINDS).
void on_finish_decl(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || !DECL_P(decl) || current_function_decl != NULL_TREE) {
        return;
    }
    if (TREE_CODE(decl) == FUNCTION_DECL && current_class_type != NULL_TREE
        && TREE_CODE(CP_DECL_CONTEXT(decl)) == NAMESPACE_DECL) {
        return;
    }
    if (!VAR_OR_FUNCTION_DECL_P(decl) && TREE_CODE(decl) != TYPE_DECL) {
        return;
    }
    judge_declaration(decl, DECL_SOURCE_LOCATION(decl), true);
}

// PLUGIN_FINISH_PARSE_FUNCTION: a function definition.  GCC moves the location
// of the function to the definition.
void on_finish_function(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || LAMBDA_FUNCTION_P(function)) {
        return;
    }
    judge_declaration(function, DECL_SOURCE_LOCATION(function), false);
}

// PLUGIN_FINISH_TYPE: a class definition and an enumeration.  GCC moves the
// location of a class to its definition.
void on_finish_type(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type)
        || (!CLASS_TYPE_P(type) && TREE_CODE(type) != ENUMERAL_TYPE) || TYPE_MAIN_DECL(type) == NULL_TREE) {
        return;
    }
    tree decl = TYPE_MAIN_DECL(type);
    judge_declaration(decl, DECL_SOURCE_LOCATION(decl), false);
}

// PLUGIN_USING_FORM: a using-declaration, a using-directive and a namespace
// alias of namespace scope.  The front end keeps a using form as a binding of
// the namespace, with no tree that the walk at the end reads.
void on_using_form(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_using_form_data*>(gcc_data);
    if (data == nullptr || current_function_decl != NULL_TREE || current_class_type != NULL_TREE) {
        return;
    }
    tree scope = current_namespace;
    switch (data->kind) {
        case PLUGIN_USING_DECLARATION:
        case PLUGIN_NAMESPACE_ALIAS:
            if (in_base_namespace(scope) && data->decl != NULL_TREE && DECL_P(data->decl)) {
                record_site(Kind::base_namespace_declaration, data->location, qualified_name(data->decl));
            }
            return;
        case PLUGIN_USING_DIRECTIVE: {
            if (data->target == NULL_TREE || TREE_CODE(data->target) != NAMESPACE_DECL) {
                return;
            }
            std::string entity = qualified_name(data->target);
            if (in_base_namespace(scope)) {
                record_site(Kind::base_namespace_using_directive, data->location, entity);
            } else if (scope == global_namespace && is_judged_at(data->location)) {
                global_directives.push_back(GlobalDirective{data->location, std::move(entity)});
            }
            return;
        }
        default:
            return;
    }
}

// ── The walk at the end ─────────────────────────────────────────────────

void walk_base_namespace(tree ns);

// The location of a declaration of a namespace: the pattern of a template.
location_t member_location(tree decl) {
    if (TREE_CODE(decl) == TEMPLATE_DECL) {
        tree result = DECL_TEMPLATE_RESULT(decl);
        return result != NULL_TREE ? defining_location(decl) : DECL_SOURCE_LOCATION(decl);
    }
    return DECL_SOURCE_LOCATION(decl);
}

// One member of the names of a namespace of the base.  A binding that a
// using-declaration makes holds a declaration of a different namespace, and it
// is no member.
void walk_member(tree decl, tree ns) {
    switch (TREE_CODE(decl)) {
        case TREE_LIST:
            if (TREE_VALUE(decl) != NULL_TREE && TREE_CODE(TREE_VALUE(decl)) != TREE_LIST) {
                walk_member(TREE_VALUE(decl), ns);
            }
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(decl, true); candidate; ++candidate) {
                if (TREE_CODE(*candidate) != OVERLOAD) {
                    walk_member(*candidate, ns);
                }
            }
            return;
        default:
            break;
    }
    if (!DECL_P(decl) || CP_DECL_CONTEXT(decl) != ns) {
        return;
    }
    if (TREE_CODE(decl) == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(decl) == NULL_TREE) {
        walk_base_namespace(decl);
        return;
    }
    tree pattern = TREE_CODE(decl) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(decl) : decl;
    if (pattern == NULL_TREE) {
        return;
    }
    if (TREE_CODE(decl) == TEMPLATE_DECL && TREE_CODE(pattern) == FUNCTION_DECL
        && DECL_LANG_SPECIFIC(pattern) != nullptr && DECL_FRIEND_CONTEXT(pattern) != NULL_TREE) {
        return;
    }
    if (TREE_CODE(decl) != TEMPLATE_DECL && TREE_CODE(decl) != NAMESPACE_DECL && is_no_site(decl)) {
        return;
    }
    location_t location = member_location(decl);
    if (is_judged_at(location)) {
        record_site(Kind::base_namespace_declaration, location, qualified_name(decl));
    }
}

// Each declaration of NS and of each namespace inside it, one time.
// Complexity: linear in the declarations of the namespaces of the base.
void walk_base_namespace(tree ns) {
    if (!walked_namespaces.insert(ns).second || NAMESPACE_LEVEL(ns) == nullptr) {
        return;
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        walk_member(decl, ns);
    }
}

// The start of the last line map of a file of the base in the unit.  The
// front end reads the whole unit before the parse, so each line map exists
// at the end of the parse.  Complexity: linear in the line maps.
location_t last_base_map_start() {
    location_t last = UNKNOWN_LOCATION;
    for (unsigned index = 0; index < LINEMAPS_ORDINARY_USED(line_table); ++index) {
        const line_map_ordinary* map = LINEMAPS_ORDINARY_MAP_AT(line_table, index);
        const char* file = ORDINARY_MAP_FILE_NAME(map);
        if (file == nullptr) {
            continue;
        }
        const FileEntry& entry = classify_file(file);
        if (entry.file_class == FileClass::base && MAP_START_LOCATION(map) > last) {
            last = MAP_START_LOCATION(map);
        }
    }
    return last;
}

}  // namespace

bool in_base_namespace(tree ns) {
    static tree const foundation = get_identifier("foundation");
    static tree const fixy = get_identifier("fixy");
    if (ns == NULL_TREE || TREE_CODE(ns) != NAMESPACE_DECL || ns == global_namespace) {
        return false;
    }
    tree top = top_namespace(ns);
    return top != global_namespace && (DECL_NAME(top) == foundation || DECL_NAME(top) == fixy);
}

tree enclosing_namespace_of(tree decl, tree& class_scope) {
    class_scope = NULL_TREE;
    tree current = TYPE_P(decl) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(decl)) : CP_DECL_CONTEXT(decl);
    for (int depth = 0; current != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL) {
            return current;
        }
        if (TREE_CODE(current) == FUNCTION_DECL) {
            return NULL_TREE;
        }
        if (!TYPE_P(current)) {
            return NULL_TREE;
        }
        class_scope = TYPE_MAIN_VARIANT(current);
        current = CP_TYPE_CONTEXT(class_scope);
    }
    return NULL_TREE;
}

bool is_base_namespace_active() { return is_active; }

void finish_base_namespace() {
    if (!is_active) {
        return;
    }
    walked_namespaces.clear();
    for (tree root : {names.foundation, names.fixy}) {
        tree ns = get_namespace_binding(global_namespace, root);
        if (ns != NULL_TREE && TREE_CODE(ns) == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(ns) == NULL_TREE) {
            walk_base_namespace(ns);
        }
    }
    if (global_directives.empty()) {
        return;
    }
    location_t last = last_base_map_start();
    for (const GlobalDirective& directive : global_directives) {
        if (last != UNKNOWN_LOCATION && directive.location < last) {
            record_site(Kind::base_namespace_using_directive, directive.location, directive.entity);
        }
    }
}

void report_base_namespace(const Finding& finding) {
    const char* entity = finding.entity.c_str();
    switch (finding.kind) {
        case Kind::base_namespace_declaration:
            error_at(finding.spelling, "this file declares %qs in a namespace of the base, outside the base", entity);
            inform(finding.spelling,
                   "a new overload changes which function a call of the base reaches, and a body of a function "
                   "that the base only declares replaces the code of the base");
            inform(finding.spelling,
                   "declare the entity in a namespace of this file.  A type of this file extends a function of the "
                   "base through an overload beside its own definition, or through a coherent specialization of a "
                   "template with the open mark");
            break;
        case Kind::base_namespace_using_directive:
            error_at(finding.spelling,
                     "this using-directive of the namespace %qs can change a lookup of the base, outside the base",
                     entity);
            inform(finding.spelling,
                   "a directive in a namespace of the base, or at the global scope before a file of the base, adds "
                   "names to each unqualified lookup of the base that reaches its scope");
            inform(finding.spelling,
                   "put the directive in a function body or in a namespace of this file, or after the last include");
            break;
        default:
            return;
    }
    inform(finding.spelling,
           "a test of a planted declaration puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> "
           "region of %<foundation/Quarantine.h%>");
}

void register_base_namespace(const char* plugin_name) {
    is_active = true;
    names.foundation = get_identifier("foundation");
    names.fixy = get_identifier("fixy");
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type, nullptr);
    register_callback(plugin_name, PLUGIN_USING_FORM, on_using_form, nullptr);
}

}  // namespace crucible_plugin
