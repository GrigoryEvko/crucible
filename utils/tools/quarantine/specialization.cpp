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
//     reason.  A coherent specialization of a marked template is no finding
//     (THE COHERENCE).  GCC
//     merges the annotation of a redeclaration into the template, and an
//     annotation keeps no location, so a second declaration in a different
//     file can add the mark.  Thus each marked template gives the record
//     open-extension at its home, and the module of the rule refuses a record
//     that no ledger row admits.  The ledger is the roster of the open
//     templates, and a mark that a unit adds gives a record with no row.
//
// THE COHERENCE
//     The open mark opens a template to each file for the types of that file
//     only, as the orphan rule of Rust does.  A marked template gives each
//     argument type its protocol, such as the key of the niche of a type.  A
//     specialization for a type of a different file then gets that authority,
//     and a door of the type can build a value that no valid value holds.  So
//     a site of a marked template outside its home is coherent only when the
//     file of the site defines each top-level template argument.  The head of
//     an argument is the class at namespace scope that holds it: the class
//     itself, its outermost enclosing class, or the primary template of an
//     instance.  An argument is owned when the file that defines its head
//     is the file of the site, and an enumeration is owned in the same way.  A
//     template template argument is owned where its template is defined, and
//     a pack when each element is owned.  Each other argument is owned by no
//     file: a fundamental type, a pointer, a reference, an array, a function
//     type, a template parameter, a lambda, a class that the unit only
//     declares, and a value.  The paths compare with read_as(), so a check
//     file counts as its header.  The base has no exemption: a base file that
//     specializes a marked template for a type of a different file is an
//     orphan too.
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
//     orphan             a site outside the home of a template with the open
//                        mark that is not coherent (THE COHERENCE).  Its
//                        entity is the template
//     foreign-primary    the home of a template of a base root outside the
//                        base.  Its entity is the template
//     forward-declaration
//                        a variable template that a unit declares with
//                        extern before its definition, at that declaration
//     open-extension     a record: a template with the open mark, at its home
//
// THE ERRORS
//     Each finding of the first four kinds is a compile error in each build
//     and each mode (is_each_mode_kind of plugin.h), so a forged
//     specialization does not compile.  The record open-extension is no
//     error, and the module of the rule judges it against the ledger.  The
//     message of an error does not start with "quarantine: ", as the error of
//     the contract rule does not.  The driver of the negative fixtures then
//     keeps it, and a regex of a fixture can read it.  A test of a forged
//     specialization puts it in an opt-out region of the reason class PROBE:,
//     and the rule quarantine counts the opted-out finding with the reason of
//     the region.
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

// The home path of each template of the rule by its qualified name, for the
// error of a site (THE ERRORS).  No tree keys the map, so it stays for the
// unit.
std::unordered_map<std::string, std::string> home_paths_by_entity;

struct Names {
    tree open_extension = NULL_TREE;
};

Names names;

void make_names() {
    if (names.open_extension == NULL_TREE) {
        names.open_extension = get_identifier("open_extension");
    }
}

// True when the primary template GENERAL carries the open mark, an annotation
// of the class foundation::open_extension: on the pattern class of a class
// template, or on the pattern variable of a variable template.  Complexity:
// linear in the attributes.
bool carries_open_mark(tree general) {
    tree result = DECL_TEMPLATE_RESULT(general);
    if (result == NULL_TREE) {
        return false;
    }
    tree attributes = TREE_CODE(result) == TYPE_DECL ? TYPE_ATTRIBUTES(TREE_TYPE(result)) : DECL_ATTRIBUTES(result);
    return carries_base_annotation(attributes, names.open_extension, NULL_TREE);
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
        home_paths_by_entity.emplace(home.entity, home.path);
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

// The path that defines the head of the template argument ARGUMENT, or an
// empty path for an argument that no file owns (THE COHERENCE).
std::string head_path(tree argument) {
    if (argument == NULL_TREE) {
        return {};
    }
    if (TREE_CODE(argument) == TEMPLATE_DECL) {
        const FileEntry* file = file_at(defining_location(general_of(argument)));
        return file != nullptr ? read_as(file->relative) : std::string{};
    }
    if (!TYPE_P(argument)) {
        return {};
    }
    tree type = TYPE_MAIN_VARIANT(argument);
    if (TREE_CODE(type) != ENUMERAL_TYPE && (!CLASS_TYPE_P(type) || LAMBDA_TYPE_P(type))) {
        return {};
    }
    // The outermost class that holds the type: a nested class of a class of
    // a different file belongs to that file.
    for (int depth = 0; depth < kTypeDepth; ++depth) {
        tree context = CP_TYPE_CONTEXT(type);
        if (context == NULL_TREE || !TYPE_P(context)) {
            break;
        }
        type = TYPE_MAIN_VARIANT(context);
    }
    tree decl = TYPE_MAIN_DECL(type);
    if (decl == NULL_TREE || DECL_FUNCTION_SCOPE_P(decl)) {
        return {};
    }
    location_t location = UNKNOWN_LOCATION;
    if (CLASS_TYPE_P(type) && CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE
        && TREE_CODE(CLASSTYPE_TI_TEMPLATE(type)) == TEMPLATE_DECL) {
        tree general = general_of(CLASSTYPE_TI_TEMPLATE(type));
        tree pattern = DECL_TEMPLATE_RESULT(general);
        if (pattern == NULL_TREE || !COMPLETE_TYPE_P(TREE_TYPE(pattern))) {
            return {};
        }
        location = defining_location(general);
    } else if (COMPLETE_TYPE_P(type) && !uses_template_parms(type)) {
        location = DECL_SOURCE_LOCATION(decl);
    }
    const FileEntry* file = file_at(location);
    return file != nullptr ? read_as(file->relative) : std::string{};
}

// True when the file PATH owns ARGUMENT, or each element of a pack.
bool owns_argument(tree argument, const std::string& path) {
    if (argument != NULL_TREE && ARGUMENT_PACK_P(argument)) {
        tree elements = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(elements); ++index) {
            if (!owns_argument(TREE_VEC_ELT(elements, index), path)) {
                return false;
            }
        }
        return true;
    }
    return head_path(argument) == path;
}

// True when the file PATH owns each top-level argument of ARGUMENTS (THE
// COHERENCE).  A site with no argument is no coherent site.  Complexity:
// linear in the arguments and the depth of their scopes.
bool owns_arguments(tree arguments, const std::string& path) {
    tree inner = arguments != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(arguments) : NULL_TREE;
    if (inner == NULL_TREE || TREE_CODE(inner) != TREE_VEC || TREE_VEC_LENGTH(inner) == 0) {
        return false;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(inner); ++index) {
        if (!owns_argument(TREE_VEC_ELT(inner, index), path)) {
            return false;
        }
    }
    return true;
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

bool reads_template_sites(tree template_decl) {
    if (!is_active || template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return false;
    }
    return home_of(general_of(template_decl)).is_tree;
}

void note_template_site(tree template_decl, tree arguments, location_t location) {
    if (!is_active) {
        return;
    }
    const TemplateHome& home = home_of(general_of(template_decl));
    if (!home.is_tree) {
        return;
    }
    Place place = site_place(location);
    if (place.file == nullptr) {
        return;
    }
    std::string path = read_as(place.file->relative);
    if (path == home.path) {
        return;
    }
    if (!home.is_open) {
        add_finding(Kind::specialization_outside_home, nullptr, place, home.entity);
    } else if (!owns_arguments(arguments, path)) {
        add_finding(Kind::specialization_orphan, nullptr, place, home.entity);
    }
}

void report_specialization(const Finding& finding) {
    const char* entity = finding.entity.c_str();
    switch (finding.kind) {
        case Kind::specialization_outside_home: {
            auto found = home_paths_by_entity.find(finding.entity);
            const char* home = found != home_paths_by_entity.end() ? found->second.c_str() : "";
            error_at(finding.spelling, "this file specializes the template %qs outside its home %qs", entity, home);
            inform(finding.spelling,
                   "C++ has no orphan rule, so a specialization in a different file changes what each gate that "
                   "reads the template admits");
            inform(finding.spelling,
                   "move the specialization into %qs, or give the template the annotation "
                   "%<foundation::open_extension%> with its reason and a row of the rule specialization-home",
                   home);
            break;
        }
        case Kind::specialization_orphan: {
            auto found = home_paths_by_entity.find(finding.entity);
            const char* home = found != home_paths_by_entity.end() ? found->second.c_str() : "";
            error_at(finding.spelling,
                     "this file specializes the extension point %qs for a type that a different file defines", entity);
            inform(finding.spelling,
                   "the open mark opens a template to each file for its own types only, so a specialization for a "
                   "type of a different file gets the authority that the template gives that type");
            inform(finding.spelling,
                   "specialize the template in the file that defines each of its arguments, or in its home %qs", home);
            break;
        }
        case Kind::specialization_foreign_primary:
            error_at(finding.spelling,
                     "this file declares the template %qs of the base first, outside the base, so the home of the "
                     "template moves here",
                     entity);
            inform(finding.spelling,
                   "include the header of the template and remove the declaration, or move the template into the "
                   "base");
            break;
        case Kind::specialization_forward_declaration:
            error_at(finding.spelling,
                     "this file declares the variable template %qs with %<extern%> before its definition, so the home "
                     "of the template moves here",
                     entity);
            inform(finding.spelling, "include the header that defines the template, and remove the declaration");
            break;
        default:
            return;
    }
    inform(finding.spelling,
           "a test of a forged specialization puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> "
           "region of %<foundation/Quarantine.h%>");
}

void register_specialization_home(const char* plugin_name) {
    is_active = true;
    make_names();
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
