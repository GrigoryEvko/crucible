// The family specialization home: each specialization of a template of the
// tree stands in the home of its template, unless the template carries the
// open mark.  C++ has no orphan rule, so each unit can specialize each
// template that it can name.  A specialization of a trait in a different file
// changes what each gate that reads the trait admits.  The rule
// specialization-home closes that route for each template of the tree, and
// one mark opens a template that the design extends from each file.
//
// THE TEMPLATES
//     Each class template and each variable template whose home stands in a
//     file of the source root or of the build directory.  A template of a
//     header outside the root, such as std::tuple_size, is no template of the
//     rule, and neither is a template of a host file or of a kernel file.  A
//     template that a home row of the rule table governs is a template of the
//     rule home (home.cpp, THE ROWS), and the rule home gives this rule each
//     other template (home.cpp, THE TEMPLATES WITH NO ROW).  The rule
//     proof-routes refuses an explicit specialization of a function template
//     in each file (expression.cpp, THE PROOF ROUTES), so this rule reads no
//     function template.
//
// THE HOME
//     The home of a template is the file that defines it, or the file of its
//     first declaration when the unit does not define it.  The check file of a
//     header, test/layer/checks/LAYER/PATH.cpp, stands where
//     include/LAYER/PATH.h stands (home.cpp, THE PLACE).
//
// THE SITES
//     The family home reads the sites of a template (home.cpp, THE SITES):
//     each explicit and each partial specialization on its lists, and each
//     explicit specialization of a member of an implicit instantiation.  The
//     place of a site is the expansion point of its location.
//
// THE OPEN MARK
//     A template that the design opens to each file carries the P3394
//     annotation foundation::open_extension of
//     include/foundation/OpenExtension.h on its primary declaration, with its
//     reason.  A specialization of a marked template is no finding.  GCC
//     merges the annotation of a redeclaration into the template, and an
//     annotation keeps no location, so a second declaration in a different
//     file can add the mark.  Thus each marked template gives the record
//     open-extension at its home, and the module of the rule refuses a record
//     that no ledger row admits.  The ledger is the roster of the open
//     templates, and a mark that a unit adds gives a record with no row.
//
// THE FORWARD DECLARATION
//     A unit can declare a template before the header that declares it.  The
//     plugin gets no event for the declaration in the header.  GCC moves the
//     place of a class template to its definition, so a forward declaration
//     moves the home of a class template only when no unit defines it.  Then
//     each specialization in the header stands outside that home, so the unit
//     still gives the kind specialization.  Only a class template whose
//     header holds no specialization and no definition escapes, and a gate
//     cannot read such a template, because each use of it is incomplete.
//     GCC keeps the place of the first declaration of a variable template,
//     also after its definition, so a forward declaration moves its home.
//     Such a declaration needs extern, and GCC keeps that flag on the
//     pattern.  So a variable template with extern and an initializer gives
//     the kind forward-declaration at the place that GCC keeps.
//
// THE BASE ROOTS
//     A template of the namespace foundation or fixy has its home in a base
//     file.  The kind foreign-primary stands at the home of such a template
//     in a different file: a forward declaration of a template of the base
//     before its header, and a new template of a base namespace in a test.
//
// THE KINDS
//     specialization     a site outside the home of a template with no open
//                        mark.  Its entity is the template
//     foreign-primary    the home of a template of a base root outside the
//                        base.  Its entity is the template
//     forward-declaration
//                        a variable template that a unit declares with
//                        extern before its definition, at that declaration
//     open-extension     a record: a template with the open mark, at its home
//
// THE ACTIVATION
//     The argument specialization-home=on turns on the rule (plugin.cpp), and
//     Quarantine.cmake gives it to each compile of the tree.  A compile of a
//     plant of a different rule has no such argument, so its report holds no
//     line of the rule, and the family home reads no template with no row.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The base roots of THE BASE ROOTS, and the paths of their base files.
constexpr const char* kBaseRoots[] = {"foundation", "fixy"};
constexpr const char* kBasePaths[] = {"include/foundation/", "include/fixy/", "src/foundation/", "src/fixy/"};

// The facts of the home of one template (THE HOME): whether the rule reads the
// template, whether it carries the open mark, the path that a site compares,
// and the qualified name of the template.
struct TemplateHome {
    bool is_tree = false;
    bool is_open = false;
    std::string path;
    std::string entity;
};

bool is_active = false;

// The facts of each most general template, cleared at each garbage collection
// (on_collection), because a tree keys the map.
std::unordered_map<tree, TemplateHome> homes_by_template;

struct Names {
    tree open_extension = NULL_TREE;
    tree foundation = NULL_TREE;
};

Names names;

void make_names() {
    if (names.open_extension == NULL_TREE) {
        names.open_extension = get_identifier("open_extension");
        names.foundation = get_identifier("foundation");
    }
}

// True when ATTRIBUTE is an annotation of the class foundation::open_extension.
// The value of an annotation has the type of the annotation, with a const
// qualifier.
bool is_open_mark(tree attribute) {
    if (!annotation_p(attribute)) {
        return false;
    }
    tree value = TREE_VALUE(attribute);
    if (value == NULL_TREE || TREE_CODE(value) != TREE_LIST || TREE_VALUE(value) == NULL_TREE) {
        return false;
    }
    tree type = TREE_TYPE(TREE_VALUE(value));
    if (type == NULL_TREE || type == error_mark_node || !TYPE_P(type)) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    tree decl = CLASS_TYPE_P(type) ? TYPE_NAME(type) : NULL_TREE;
    if (decl == NULL_TREE || TREE_CODE(decl) != TYPE_DECL || DECL_NAME(decl) != names.open_extension) {
        return false;
    }
    tree scope = CP_DECL_CONTEXT(decl);
    return scope != NULL_TREE && TREE_CODE(scope) == NAMESPACE_DECL && DECL_NAME(scope) == names.foundation
        && CP_DECL_CONTEXT(scope) == global_namespace;
}

// True when the primary template GENERAL carries the open mark: an
// annotation of the pattern class of a class template, or of the pattern
// variable of a variable template.  Complexity: linear in the attributes.
bool carries_open_mark(tree general) {
    tree result = DECL_TEMPLATE_RESULT(general);
    if (result == NULL_TREE) {
        return false;
    }
    tree attributes = TREE_CODE(result) == TYPE_DECL ? TYPE_ATTRIBUTES(TREE_TYPE(result)) : DECL_ATTRIBUTES(result);
    for (tree attribute = attributes; attribute != NULL_TREE; attribute = TREE_CHAIN(attribute)) {
        if (is_open_mark(attribute)) {
            return true;
        }
    }
    return false;
}

// True when GENERAL stands in a namespace of a base root (THE BASE ROOTS).
bool is_in_base_root(tree general) {
    tree top = top_namespace(general);
    if (top == NULL_TREE || DECL_NAME(top) == NULL_TREE) {
        return false;
    }
    const char* name = IDENTIFIER_POINTER(DECL_NAME(top));
    for (const char* root : kBaseRoots) {
        if (std::strcmp(name, root) == 0) {
            return true;
        }
    }
    return false;
}

// True when GENERAL is a variable template that a declaration with extern
// introduced before its definition (THE FORWARD DECLARATION).
bool is_forward_declared_variable(tree general) {
    tree result = DECL_TEMPLATE_RESULT(general);
    return result != NULL_TREE && VAR_P(result) && DECL_LANG_SPECIFIC(result) != nullptr && DECL_THIS_EXTERN(result)
        && DECL_INITIAL(result) != NULL_TREE;
}

bool is_base_path(const std::string& path) {
    for (const char* prefix : kBasePaths) {
        if (has_prefix(path, prefix)) {
            return true;
        }
    }
    return false;
}

// The facts of the home of GENERAL, a most general template, made and kept on
// the first request.  The first request also gives the record open-extension
// of a marked template and the finding foreign-primary, at the home.
// Complexity: O(depth of the scopes and the attributes) on the first request.
const TemplateHome& home_of(tree general) {
    auto found = homes_by_template.find(general);
    if (found != homes_by_template.end()) {
        return found->second;
    }
    TemplateHome home;
    location_t defining = defining_location(general);
    const FileEntry* file = file_at(defining);
    if (file != nullptr && file->file_class != FileClass::exempt) {
        home.is_tree = true;
        home.path = read_as(file->relative);
        home.entity = qualified_name(general);
        home.is_open = carries_open_mark(general);
        Place place = site_place(defining);
        if (place.file != nullptr && home.is_open) {
            add_finding(Kind::specialization_open_extension, nullptr, place, home.entity);
        }
        if (place.file != nullptr && is_in_base_root(general) && !is_base_path(home.path)) {
            add_finding(Kind::specialization_foreign_primary, nullptr, place, home.entity);
        }
        if (place.file != nullptr && is_forward_declared_variable(general)) {
            add_finding(Kind::specialization_forward_declaration, nullptr, place, home.entity);
        }
    }
    return homes_by_template.emplace(general, std::move(home)).first->second;
}

tree general_of(tree template_decl) {
    tree general = most_general_template(template_decl);
    return general != NULL_TREE ? general : template_decl;
}

void on_collection(void*, void*) { homes_by_template.clear(); }

}  // namespace

bool is_specialization_home_active() { return is_active; }

bool read_template_home(tree template_decl) {
    if (!is_active || template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return false;
    }
    const TemplateHome& home = home_of(general_of(template_decl));
    return home.is_tree && !home.is_open;
}

void note_template_site(tree template_decl, location_t location) {
    if (!is_active) {
        return;
    }
    const TemplateHome& home = home_of(general_of(template_decl));
    if (!home.is_tree || home.is_open) {
        return;
    }
    Place place = site_place(location);
    if (place.file == nullptr || read_as(place.file->relative) == home.path) {
        return;
    }
    add_finding(Kind::specialization_outside_home, nullptr, place, home.entity);
}

void register_specialization_home(const char* plugin_name) {
    is_active = true;
    make_names();
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
