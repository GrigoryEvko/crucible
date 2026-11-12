// The family home: a definition or a specialization of an entity stands only in
// its home.  C++ has no orphan rule: each unit can define or specialize each
// entity that it can name.  A specialization of a trait outside the file that
// owns the trait changes what a concept admits, and a second definition of a
// door class mints the authority of the door.  So the rule home names where an
// entity is at home, and gives a finding for each site of the entity outside
// its home.
//
// THE ROWS
//     The rule table (utils/rule-table.txt) gives each home in a row:
//         home CLASS ENTITY [PATH...] | REASON
//         home-args CLASS ENTITY [PATH...] | REASON
//     CLASS is a class of THE CLASSES, and ENTITY is a qualified name.  An
//     ENTITY of one name names each namespace of that name.  A PATH admits
//     each file that it holds: a directory each file under it, and a file that
//     file.  A row with no path admits no file.  The row of an entity also
//     governs each declaration inside the entity: a member of a class, and a
//     declaration of a namespace and of the namespaces in it.  The innermost
//     row decides: the row of a class decides for the class, and the row of
//     its namespace decides for each class with no row of its own.
//     A home-args row also admits a specialization in the file that defines
//     each of its arguments (THE ARGUMENTS).
//
// THE CLASSES
//     The class of a row selects the sites of its entity and the kind of a
//     site outside its home:
//       federation  the definition of the entity, each explicit or partial
//                   specialization, the definition of each member and each
//                   class inside it: federation-outside-home
//       owner       the definition of a class: owner-outside-home
//       combine     the definition of a function named combine_ids:
//                   combine-outside-home, and THE BODY OF combine_ids
//       split       each explicit or partial specialization, and each explicit
//                   specialization of a member: away-from-arguments
//       trait       each explicit or partial specialization, each explicit
//                   specialization of a member, and each declaration of a
//                   namespace of a row: outside-home
//     A site that a path of its row admits is the record at-home.
//
// THE SITES
//     An explicit specialization and a partial specialization are on the lists
//     of their template (DECL_TEMPLATE_INSTANTIATIONS and
//     DECL_TEMPLATE_SPECIALIZATIONS), also when the unit only declares them.
//     The walk of the namespaces at the end of the parse reads the lists of
//     each template.  An explicit specialization of a member of an implicit
//     instantiation, such as `template <> constexpr bool X<Fake>::value =
//     true;`, is on no list, so the hooks PLUGIN_FINISH_DECL,
//     PLUGIN_FINISH_PARSE_FUNCTION and PLUGIN_FINISH_TYPE read it where the
//     unit parses it.  GCC gives a declaration that a definition follows the
//     location of the definition, so the walk reads the definition of a
//     member that stands outside its class at its own place.
//
// THE TEMPLATES WITH NO ROW
//     A class template or a variable template that no row governs is a
//     template of the rule specialization-home (specialization.cpp), also
//     when it carries the open mark.  The family reads its sites, as THE
//     SITES tells, and gives each site with its template arguments to that
//     rule.  A row of the template replaces that rule for the template.
//
// THE PLACE
//     The place of a site is the expansion point of its location.  A
//     declaration that a macro makes stands in the file that expands the macro,
//     as a call of CRUCIBLE_ADMIT_MACHINE_TRANSITION does.  A site in a header
//     outside the source root is no site.  The check file of a header,
//     test/layer/checks/LAYER/PATH.cpp, stands where include/LAYER/PATH.h
//     stands, because it holds the checks of that header (CLAUDE.md section XV).
//
// THE ARGUMENTS
//     A specialization of a home-args row is beside its arguments when each
//     top-level template argument is a class type that does not depend on a
//     template parameter, and the file that defines the class is the file of
//     the specialization.  A class template instance is defined where its
//     template is defined.  A pointer, a reference, an enumeration, a value
//     and a pack are no tags.
//
// THE SCOPE
//     The hooks and the lists cost little, so each unit reads each site that
//     it compiles.  Two reads walk trees, and a unit reads a file with them
//     only in these cases: the unit compiles the file, the argument sentinel
//     names the file, or no sentinel names the file.  A quarantined header
//     outside include/ has no sentinel, and a header of include/ that the
//     list of the argument not-standalone names has none either, because it
//     cannot compile alone (THE SENTINEL of plugin.h).  The two reads read such
//     a header in each unit that includes it.  They are THE BODY OF
//     combine_ids and THE GATES.
//
// THE BODY OF combine_ids
//     The class combine gives second-body for a function named combine_ids
//     outside the namespace of its row, and for a function or a lambda whose
//     body holds the salt 0x9e3779b97f4a7c15, a shift left by 6, a shift right
//     by 2 and a call of fmix64.  It gives derived-name for a
//     declaration with a name that holds combine_ids and more, such as
//     combine_ids_runtime.
//
// THE GATES
//     A gate is a concept definition or a requires-clause in a header of
//     include/foundation/, include/fixy/ or include/crucible/, or in the check
//     file of such a header.  A gate that reads a template that a unit can
//     specialize gives open-read: a variable template, a function template, or
//     a member of a class template instance, such as trait<T>::value.  The
//     template is a template of the two layers, and for a gate of
//     include/crucible/ also a template of include/crucible/.  A concept, a
//     function that is not a template and a sealed template are closed.  Each
//     gate reads each template one time.
//
// THE SEALED TEMPLATES
//     The rule specialization-home makes each specialization of a template
//     with no row outside its home a compile error (specialization.cpp).  So
//     a class template or a variable template is sealed when the unit defines
//     it, no row governs it, and it has no open mark.  A unit cannot change
//     the answer of a gate that reads it from a different file.  A class
//     template that the unit only declares is sealed too when the unit holds
//     a specialization of it in its home.  A different file that declares or
//     defines the template first moves the home there, and that
//     specialization then stands outside the home.  So is a class template
//     of the namespace foundation or fixy whose home is a header of the two
//     layers: the rule specialization-home refuses its definition outside
//     the base (THE BASE ROOTS of specialization.cpp).  A template of a row
//     stays open, because its row admits sites in the paths of the row.  A
//     function template stays open, because the rule specialization-home
//     does not read it.  Each other template that the unit only declares
//     stays open, because a different file can give its definition.  When a
//     compile has no argument specialization-home=on, no template is sealed.
//
//     The definition of a sealed template can read an open template, so a
//     gate over a sealed template reads each open leaf of the definition, at
//     each depth.  The walk of a definition reads the pattern and each
//     explicit and partial specialization: the initializer of a variable
//     template, and the bases, the data members, the member aliases, the
//     nested classes and the friends of a class template.  It reads the
//     signature, the constraint and the noexcept specification of each
//     member function.  It reads the body of a function that a constant
//     evaluation or the deduction of its return type reads: a constexpr, a
//     consteval and an auto function.  It reads each class template instance
//     that a type of the definition names, and the template arguments of
//     each sealed template that the gate or the definition reads.  A cycle of
//     sealed templates gives the leaves of the cycle, and a chain longer than
//     kSealDepth is open.
//
// WHAT THE FAMILY DOES NOT READ
//     Code that no unit of the build compiles, such as a BPF C header that
//     clang compiles.  A gate and a body in a function body, such as a lambda
//     with a requires-clause, outside THE BODY OF combine_ids.  A use of a
//     name, and not its declaration.

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The salt and the finalizer of combine_ids (THE BODY OF combine_ids).
constexpr unsigned long long kCombineSalt = 0x9E3779B97F4A7C15ULL;
constexpr char kCombineName[] = "combine_ids";
constexpr std::size_t kCombineLength = sizeof kCombineName - 1;
constexpr char kMixName[] = "fmix64";
// The directory of the headers.
constexpr char kHeaderTree[] = "include/";
// The two layers of the base, and the runtime layer.
constexpr const char* kLayerTrees[] = {"include/foundation/", "include/fixy/"};
constexpr char kCrucibleTree[] = "include/crucible/";

// The kind of a site outside its home, for each class (THE CLASSES).
Kind outside_kind_of(HomeClass home_class) {
    switch (home_class) {
        case HomeClass::federation:
            return Kind::federation_outside_home;
        case HomeClass::owner:
            return Kind::owner_outside_home;
        case HomeClass::combine:
            return Kind::combine_outside_home;
        case HomeClass::split:
            return Kind::split_away_from_arguments;
        case HomeClass::trait:
            return Kind::trait_outside_home;
    }
    return Kind::trait_outside_home;
}

// The home of one entity: its row, the class of the row, and the qualified
// name of the entity.
struct Home {
    const HomeRow* row = nullptr;
    HomeClass home_class = HomeClass::trait;
    std::string entity;

    [[nodiscard]] bool is_set() const { return row != nullptr; }
};

// The rows by their entity, and the last name of each entity, for a quick
// test before a qualified name is made.  The walk asks each declaration of
// the unit, so a bit of NAME_BITS for each last name answers most of them
// before the set does.
constexpr std::size_t kNameBitWords = 16;

struct Index {
    bool is_ready = false;
    std::unordered_map<std::string, const HomeRow*> qualified;
    std::unordered_map<std::string, const HomeRow*> one_name;
    std::unordered_set<tree> last_names;
    std::uint64_t name_bits[kNameBitWords] = {};
};

// The bit of an identifier in Index::name_bits.
std::size_t name_bit(tree name) {
    return (reinterpret_cast<std::uintptr_t>(name) >> 4) % (kNameBitWords * 64);
}

Index index_of_rows;

// Caches keyed by tree, cleared at each garbage collection (on_collection).
std::unordered_map<tree, Home> template_homes;
std::unordered_map<tree, int> gate_kinds;  // the open kind of a template, or kClosedTemplate
std::unordered_set<tree> walked;
// The open leaves of each sealed template whose walk is complete, and the
// depth of each sealed template whose walk has not finished (THE SEALED
// TEMPLATES).
std::unordered_map<tree, std::vector<tree>> sealed_leaves;
std::unordered_map<tree, int> sealing;

constexpr int kClosedTemplate = 0;
constexpr int kLayerTemplate = 1;
constexpr int kCrucibleTemplate = 2;

void build_index() {
    if (index_of_rows.is_ready) {
        return;
    }
    index_of_rows.is_ready = true;
    for (const HomeRow& row : state.table.homes) {
        (row.is_one_name ? index_of_rows.one_name : index_of_rows.qualified).emplace(row.entity, &row);
        std::size_t last = row.entity.rfind("::");
        std::string name = last == std::string::npos ? row.entity : row.entity.substr(last + 2);
        tree identifier = get_identifier(name.c_str());
        index_of_rows.last_names.insert(identifier);
        std::size_t bit = name_bit(identifier);
        index_of_rows.name_bits[bit / 64] |= std::uint64_t{1} << (bit % 64);
    }
}

// ── The place ───────────────────────────────────────────────────────────

// site_place(), file_at() and read_as() of THE PLACE stand after this
// namespace, because the family specialization home reads them too.

bool has_any_prefix(const std::string& path, std::initializer_list<const char*> prefixes) {
    for (const char* prefix : prefixes) {
        if (has_prefix(path, prefix)) {
            return true;
        }
    }
    return false;
}

// True when the family reads the trees of FILE in this unit (THE SCOPE).
bool is_scope_file(const FileEntry* file) {
    if (file == nullptr || file->file_class == FileClass::outside) {
        return false;
    }
    // One process compiles one unit, so its main file is the same for each call.
    static const FileEntry* const main_file =
        main_input_filename != nullptr ? &classify_file(main_input_filename) : nullptr;
    if (file == main_file) {
        return true;
    }
    if ((!state.sentinel.empty() && file->relative == state.sentinel) || file->cannot_compile_alone) {
        return true;
    }
    return file->file_class == FileClass::quarantined && !has_prefix(file->relative, kHeaderTree);
}

// ── The rows of a declaration ───────────────────────────────────────────

// The scope that holds DECL, a declaration or a type, or NULL_TREE at the
// global namespace.
tree scope_of(tree node) {
    tree context = TYPE_P(node) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(node)) : CP_DECL_CONTEXT(node);
    return context == global_namespace ? NULL_TREE : context;
}

// The name of a declaration or of a type, or NULL_TREE.
tree name_of(tree node) {
    if (TYPE_P(node)) {
        tree type_decl = TYPE_NAME(TYPE_MAIN_VARIANT(node));
        return type_decl != NULL_TREE && TREE_CODE(type_decl) == TYPE_DECL ? DECL_NAME(type_decl) : NULL_TREE;
    }
    return DECL_P(node) ? DECL_NAME(node) : NULL_TREE;
}

// The row of NODE itself: a row whose entity is its qualified name, or a row
// of one name for a namespace of that name.  Complexity: O(1), and one
// qualified name when the last name of a row matches.
const HomeRow* own_row(tree node) {
    tree name = name_of(node);
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE) {
        return nullptr;
    }
    std::size_t bit = name_bit(name);
    if ((index_of_rows.name_bits[bit / 64] & (std::uint64_t{1} << (bit % 64))) == 0
        || index_of_rows.last_names.count(name) == 0) {
        return nullptr;
    }
    if (TREE_CODE(node) == NAMESPACE_DECL) {
        auto found = index_of_rows.one_name.find(identifier_text(name));
        if (found != index_of_rows.one_name.end()) {
            auto qualified = index_of_rows.qualified.find(qualified_name(node));
            return qualified != index_of_rows.qualified.end() ? qualified->second : found->second;
        }
    }
    auto found = index_of_rows.qualified.find(qualified_name(TYPE_P(node) ? TYPE_MAIN_DECL(node) : node));
    return found != index_of_rows.qualified.end() ? found->second : nullptr;
}

// The innermost row of NODE and of its scopes (THE ROWS), and the scope that
// the row names.  Complexity: O(depth of the scopes).
const HomeRow* governing_row(tree node, tree& named) {
    int depth = 0;
    for (tree current = node; current != NULL_TREE && depth < kTypeDepth; current = scope_of(current), ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL && DECL_NAMESPACE_INLINE_P(current)) {
            continue;
        }
        if (const HomeRow* row = own_row(current)) {
            named = current;
            return row;
        }
    }
    named = NULL_TREE;
    return nullptr;
}

// The text of the entity that a row names, at the scope NAMED: the qualified
// name of a class, a template or a function, and of a namespace.
std::string entity_text(tree named) {
    return qualified_name(TYPE_P(named) ? TYPE_MAIN_DECL(named) : named);
}

// ── The record of a site ────────────────────────────────────────────────

bool row_admits(const HomeRow& row, const std::string& path) {
    for (const std::string& row_path : row.paths) {
        if (row_holds(row_path, path)) {
            return true;
        }
    }
    return false;
}

void add_home_finding(Kind kind, const Place& place, const std::string& entity) {
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// The rows and the files of the at-home lines of this unit: one line for each
// row and file is sufficient for the module of the rule.
std::unordered_set<std::string> at_home_keys;

// One site of an entity at LOCATION: an at-home line when a path of its row
// admits the place, and a finding when nothing admits it.  BESIDE says that
// the specialization is beside its arguments (THE ARGUMENTS).
void record_site(const Home& home, location_t location, bool is_beside = false) {
    Place place = site_place(location);
    if (place.file == nullptr) {
        return;
    }
    std::string path = read_as(place.file->relative);
    if (home.row != nullptr && row_admits(*home.row, path)) {
        if (at_home_keys.insert(std::to_string(home.row->line) + ' ' + place.file->relative).second) {
            add_home_finding(Kind::home_at_home, place, home.row->entity);
        }
        return;
    }
    if (home.row != nullptr && home.row->is_args && is_beside) {
        return;
    }
    add_home_finding(outside_kind_of(home.home_class), place, home.entity);
}

// ── The templates ───────────────────────────────────────────────────────

// defining_location() stands after this namespace, with the helpers of THE
// PLACE.

bool is_class_template(tree template_decl) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    return result != NULL_TREE && TREE_CODE(result) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(result)
        && CLASS_TYPE_P(TREE_TYPE(result));
}

bool is_variable_template(tree template_decl) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    return result != NULL_TREE && VAR_P(result);
}

// The home of a class template or a variable template: its row, or no home
// (THE TEMPLATES WITH NO ROW).  Complexity: O(depth of the scopes) on the
// first request.
const Home& home_of_template(tree template_decl) {
    tree general = most_general_template(template_decl);
    if (general == NULL_TREE) {
        general = template_decl;
    }
    auto found = template_homes.find(general);
    if (found != template_homes.end()) {
        return found->second;
    }
    Home home;
    tree named = NULL_TREE;
    home.row = governing_row(general, named);
    if (home.row != nullptr) {
        home.home_class = home.row->home_class;
        home.entity = entity_text(named);
    }
    return template_homes.emplace(general, std::move(home)).first->second;
}

// The file that defines a class type that is a template argument, or null for
// a type that is no tag (THE ARGUMENTS).  A template parameter, a pack
// expansion and a dependent name are no tags.
const FileEntry* tag_file(tree argument) {
    if (argument == NULL_TREE || !TYPE_P(argument)) {
        return nullptr;
    }
    tree type = TYPE_MAIN_VARIANT(argument);
    if (!CLASS_TYPE_P(type) || LAMBDA_TYPE_P(type) || TYPE_UNNAMED_P(type)) {
        return nullptr;
    }
    tree decl = TYPE_MAIN_DECL(type);
    if (decl == NULL_TREE || DECL_FUNCTION_SCOPE_P(decl)) {
        return nullptr;
    }
    // An instance, also one with dependent arguments such as Whole<UserTag>,
    // is defined where its template is defined.
    if (CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE
        && (CLASSTYPE_IMPLICIT_INSTANTIATION(type) || uses_template_parms(type))) {
        tree general = most_general_template(CLASSTYPE_TI_TEMPLATE(type));
        if (general == NULL_TREE || !is_class_template(general)
            || !COMPLETE_TYPE_P(TREE_TYPE(DECL_TEMPLATE_RESULT(general)))) {
            return nullptr;
        }
        return file_at(defining_location(general));
    }
    if (uses_template_parms(type)) {
        return nullptr;
    }
    return COMPLETE_TYPE_P(type) ? file_at(DECL_SOURCE_LOCATION(decl)) : nullptr;
}

// True when ARGUMENT is a tag that SITE defines, or a pack of such tags.
bool is_tag_of(tree argument, const FileEntry* site) {
    if (argument != NULL_TREE && TREE_CODE(argument) == TYPE_ARGUMENT_PACK) {
        tree elements = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(elements); ++index) {
            if (!is_tag_of(TREE_VEC_ELT(elements, index), site)) {
                return false;
            }
        }
        return true;
    }
    return tag_file(argument) == site;
}

// True when each top-level argument of ARGUMENTS is a tag that the file of
// LOCATION defines (THE ARGUMENTS).
bool is_beside_arguments(tree arguments, location_t location) {
    const FileEntry* site = file_at(location);
    tree inner = arguments != NULL_TREE ? INNERMOST_TEMPLATE_ARGS(arguments) : NULL_TREE;
    if (site == nullptr || inner == NULL_TREE || TREE_CODE(inner) != TREE_VEC || TREE_VEC_LENGTH(inner) == 0) {
        return false;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(inner); ++index) {
        if (!is_tag_of(TREE_VEC_ELT(inner, index), site)) {
            return false;
        }
    }
    return true;
}

// One site of TEMPLATE_DECL with the template arguments ARGUMENTS at
// LOCATION: a site of its row, or a site of the rule specialization-home for
// a template with no row (THE TEMPLATES WITH NO ROW).
void record_template_site(const Home& home, tree template_decl, tree arguments, location_t location) {
    if (home.is_set()) {
        record_site(home, location, home.row->is_args && is_beside_arguments(arguments, location));
    } else {
        note_template_site(template_decl, arguments, location);
    }
}

// Each explicit and each partial specialization of TEMPLATE_DECL (THE SITES).
void read_specializations(tree template_decl) {
    if (!is_class_template(template_decl) && !is_variable_template(template_decl)) {
        return;
    }
    const Home& home = home_of_template(template_decl);
    if (home.is_set() ? home.home_class == HomeClass::combine : !reads_template_sites(template_decl)) {
        return;
    }
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial == NULL_TREE || TREE_CODE(partial) != TEMPLATE_DECL) {
            continue;
        }
        record_template_site(home, template_decl, TREE_PURPOSE(entry), defining_location(partial));
    }
    bool is_class = is_class_template(template_decl);
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree value = TREE_VALUE(entry);
        if (value == NULL_TREE) {
            continue;
        }
        bool is_explicit = is_class ? CLASS_TYPE_P(value) && CLASSTYPE_TEMPLATE_SPECIALIZATION(value)
                                    : VAR_P(value) && DECL_LANG_SPECIFIC(value) != nullptr
                                          && DECL_TEMPLATE_SPECIALIZATION(value);
        if (!is_explicit || uses_template_parms(value)) {
            continue;
        }
        record_template_site(home, template_decl, TREE_PURPOSE(entry),
                             DECL_SOURCE_LOCATION(is_class ? TYPE_MAIN_DECL(value) : value));
    }
}

// An explicit specialization of a member of an implicit instantiation, such
// as `template <> int X<Fake>::value = 1;`, and a member template that the
// source defines for one implicit instantiation, such as
// `template <> template <class U> void X<Fake>::f(U) {}`: a site of the
// template of the instantiation.  The hooks read it, because it is on no list.
void read_member_specialization(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl)) {
        return;
    }
    // An implicit member, such as a copy constructor, is no site of the source.
    bool is_template_member = VAR_OR_FUNCTION_DECL_P(decl) && !DECL_ARTIFICIAL(decl) && DECL_LANG_SPECIFIC(decl) != nullptr
                           && DECL_TEMPLATE_INFO(decl) != NULL_TREE
                           && (DECL_TEMPLATE_SPECIALIZATION(decl)
                               || (TREE_CODE(DECL_TI_TEMPLATE(decl)) == TEMPLATE_DECL
                                   && PRIMARY_TEMPLATE_P(DECL_TI_TEMPLATE(decl))));
    bool is_specialization = TREE_CODE(decl) == TYPE_DECL
                               ? CLASS_TYPE_P(TREE_TYPE(decl)) && CLASSTYPE_TEMPLATE_SPECIALIZATION(TREE_TYPE(decl))
                               : is_template_member;
    tree context = DECL_CONTEXT(decl);
    if (!is_specialization || context == NULL_TREE || !CLASS_TYPE_P(context)
        || CLASSTYPE_TEMPLATE_INFO(context) == NULL_TREE || !CLASSTYPE_IMPLICIT_INSTANTIATION(context)) {
        return;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(context);
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return;
    }
    const Home& home = home_of_template(template_decl);
    if (home.is_set() ? home.home_class == HomeClass::combine : !reads_template_sites(template_decl)) {
        return;
    }
    record_template_site(home, template_decl, CLASSTYPE_TI_ARGS(context), DECL_SOURCE_LOCATION(decl));
}

// ── The names of combine_ids ────────────────────────────────────────────

// True when the identifier NAME holds combine_ids.  Each hook asks it for
// each declaration, so it scans for the first letter and compares only there.
bool holds_combine_name(tree name) {
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE) {
        return false;
    }
    std::size_t length = IDENTIFIER_LENGTH(name);
    const char* text = IDENTIFIER_POINTER(name);
    for (std::size_t start = 0; start + kCombineLength <= length; ++start) {
        if (text[start] == kCombineName[0] && std::memcmp(text + start, kCombineName, kCombineLength) == 0) {
            return true;
        }
    }
    return false;
}

bool is_combine_name(tree name) {
    return name != NULL_TREE && TREE_CODE(name) == IDENTIFIER_NODE && IDENTIFIER_LENGTH(name) == kCombineLength
        && std::memcmp(IDENTIFIER_POINTER(name), kCombineName, kCombineLength) == 0;
}

// A declaration with a name that holds combine_ids and more.  An artificial
// declaration and a declaration of a library file are no site.
void read_derived_name(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return;
    }
    tree name = DECL_NAME(decl);
    if (!holds_combine_name(name) || is_combine_name(name)) {
        return;
    }
    add_home_finding(Kind::combine_derived_name, site_place(DECL_SOURCE_LOCATION(decl)), qualified_name(decl));
}

// The definition of a function named combine_ids: a site of the row of the
// canonical function at its home, and a second body in each other scope.
void read_combine_definition(tree function) {
    if (!is_combine_name(DECL_NAME(function))) {
        return;
    }
    tree named = NULL_TREE;
    const HomeRow* row = governing_row(function, named);
    location_t location = DECL_SOURCE_LOCATION(function);
    if (row != nullptr && row->home_class == HomeClass::combine && named == function) {
        Home home;
        home.row = row;
        home.home_class = row->home_class;
        home.entity = row->entity;
        record_site(home, location);
        return;
    }
    add_home_finding(Kind::combine_second_body, site_place(location), qualified_name(function));
}

// The facts of THE BODY OF combine_ids that a walk of one body finds.
struct ShapeFacts {
    tree mix = NULL_TREE;  // the identifier fmix64 of the unit
    bool has_salt = false;
    bool has_left_shift = false;
    bool has_right_shift = false;
    bool has_mix = false;
};

// The value of a shift count, through the conversions, or -1.
long shift_count(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(node) == INTEGER_CST) {
            return tree_fits_shwi_p(node) ? static_cast<long>(tree_to_shwi(node)) : -1;
        }
        if (!CONVERT_EXPR_P(node) && TREE_CODE(node) != NON_LVALUE_EXPR && TREE_CODE(node) != VIEW_CONVERT_EXPR) {
            return -1;
        }
        node = TREE_OPERAND(node, 0);
    }
    return -1;
}

// The name that a callee spells, or NULL_TREE.
tree callee_name(tree callee) {
    for (int depth = 0; callee != NULL_TREE && depth < kTypeDepth; ++depth) {
        switch (TREE_CODE(callee)) {
            case ADDR_EXPR:
            case NOP_EXPR:
                callee = TREE_OPERAND(callee, 0);
                break;
            case TEMPLATE_ID_EXPR:
                callee = TREE_OPERAND(callee, 0);
                break;
            case SCOPE_REF:
                callee = TREE_OPERAND(callee, 1);
                break;
            case COMPONENT_REF:
                callee = TREE_OPERAND(callee, 1);
                break;
            case BASELINK:
                callee = BASELINK_FUNCTIONS(callee);
                break;
            case OVERLOAD:
                return OVL_NAME(callee);
            case IDENTIFIER_NODE:
                return callee;
            default:
                return DECL_P(callee) ? DECL_NAME(callee) : NULL_TREE;
        }
    }
    return NULL_TREE;
}

tree visit_shape(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto* facts = static_cast<ShapeFacts*>(data);
    // A type holds no fact of the shape.
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    switch (TREE_CODE(node)) {
        case INTEGER_CST:
            facts->has_salt = facts->has_salt || (tree_fits_uhwi_p(node) && tree_to_uhwi(node) == kCombineSalt);
            break;
        case LSHIFT_EXPR:
        case LROTATE_EXPR:
            facts->has_left_shift = facts->has_left_shift
                                 || (TREE_CODE(node) == LSHIFT_EXPR && shift_count(TREE_OPERAND(node, 1)) == 6);
            break;
        case RSHIFT_EXPR:
            facts->has_right_shift = facts->has_right_shift || shift_count(TREE_OPERAND(node, 1)) == 2;
            break;
        case MODOP_EXPR: {
            tree code = TREE_OPERAND(node, 1);
            long count = shift_count(TREE_OPERAND(node, 2));
            if (code != NULL_TREE && TREE_CODE(code) == LSHIFT_EXPR && count == 6) {
                facts->has_left_shift = true;
            } else if (code != NULL_TREE && TREE_CODE(code) == RSHIFT_EXPR && count == 2) {
                facts->has_right_shift = true;
            }
            break;
        }
        case CALL_EXPR:
            facts->has_mix = facts->has_mix || callee_name(CALL_EXPR_FN(node)) == facts->mix;
            break;
        default:
            break;
    }
    // The walk stops at the first node after which the body holds each fact.
    return facts->has_salt && facts->has_left_shift && facts->has_right_shift && facts->has_mix ? node : NULL_TREE;
}

// A function or a lambda of a scope file whose body has the shape of
// combine_ids (THE BODY OF combine_ids).  A function named combine_ids is a
// site of its name, and not of its body.
void read_body_shape(tree function) {
    if (is_combine_name(DECL_NAME(function)) || DECL_SAVED_TREE(function) == NULL_TREE
        || !is_scope_file(file_at(DECL_SOURCE_LOCATION(function)))) {
        return;
    }
    // A body that calls fmix64 names it, so while the unit has no identifier
    // fmix64, no body has the shape, and the walk does not start.  An
    // identifier stays in the table to the end of the unit.
    static tree mix = NULL_TREE;
    if (mix == NULL_TREE) {
        mix = maybe_get_identifier(kMixName);
        if (mix == NULL_TREE) {
            return;
        }
    }
    ShapeFacts facts;
    facts.mix = mix;
    cp_walk_tree_without_duplicates(&DECL_SAVED_TREE(function), visit_shape, &facts);
    if (facts.has_salt && facts.has_left_shift && facts.has_right_shift && facts.has_mix) {
        Place place = site_place(DECL_SOURCE_LOCATION(function));
        // A lambda is named by the scope that holds its closure type.
        std::string entity = qualified_name(function);
        if (LAMBDA_FUNCTION_P(function)) {
            tree scope = scope_of(DECL_CONTEXT(function));
            entity = (scope != NULL_TREE ? qualified_name(scope) + "::" : std::string{}) + "(lambda)";
        }
        add_home_finding(Kind::combine_second_body, place, entity);
    }
}

// ── The gates ───────────────────────────────────────────────────────────

// True when the unit holds an explicit or a partial specialization of the
// class template GENERAL in the home file of GENERAL (THE SEALED TEMPLATES).
// Complexity: linear in the specializations of GENERAL.
bool has_specialization_at_home(tree general) {
    const FileEntry* home = file_at(defining_location(general));
    if (home == nullptr) {
        return false;
    }
    std::string home_path = read_as(home->relative);
    auto is_at_home = [&home_path](location_t location) {
        const FileEntry* file = file_at(location);
        return file != nullptr && read_as(file->relative) == home_path;
    };
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL && is_at_home(defining_location(partial))) {
            return true;
        }
    }
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree value = TREE_VALUE(entry);
        if (value != NULL_TREE && CLASS_TYPE_P(value) && CLASSTYPE_TEMPLATE_SPECIALIZATION(value)
            && !uses_template_parms(value) && is_at_home(DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(value)))) {
            return true;
        }
    }
    return false;
}

// True when GENERAL is a template of the namespace foundation or fixy whose
// home is a header of the two layers (THE SEALED TEMPLATES).  The rule
// specialization-home refuses its definition in a file outside the base.
bool is_base_root_template(tree general) {
    tree top = top_namespace(general);
    if (top == NULL_TREE || DECL_NAME(top) == NULL_TREE) {
        return false;
    }
    const char* name = IDENTIFIER_POINTER(DECL_NAME(top));
    if (std::strcmp(name, "foundation") != 0 && std::strcmp(name, "fixy") != 0) {
        return false;
    }
    const FileEntry* file = file_at(defining_location(general));
    return file != nullptr && has_any_prefix(read_as(file->relative), {kLayerTrees[0], kLayerTrees[1]});
}

// True when the most general template GENERAL is sealed (THE SEALED
// TEMPLATES).  The walk at the end of the parse asks it, so the unit holds
// each definition and each specialization that it reads.
bool is_sealed_template(tree general) {
    if (!is_specialization_home_active() || home_of_template(general).is_set()) {
        return false;
    }
    tree result = DECL_TEMPLATE_RESULT(general);
    bool is_defined = false;
    if (is_class_template(general)) {
        is_defined = COMPLETE_TYPE_P(TREE_TYPE(result)) || has_specialization_at_home(general)
                  || is_base_root_template(general);
    } else if (is_variable_template(general)) {
        is_defined = DECL_INITIAL(result) != NULL_TREE && DECL_INITIAL(result) != error_mark_node;
    }
    return is_defined && read_template_home(general);
}

// The open kind of TEMPLATE_DECL for a gate (THE GATES): kLayerTemplate for a
// template of the two layers, kCrucibleTemplate for a template of
// include/crucible/, and kClosedTemplate for a concept and each other
// template.
int gate_kind(tree template_decl) {
    tree general = most_general_template(template_decl);
    if (general == NULL_TREE) {
        general = template_decl;
    }
    auto found = gate_kinds.find(general);
    if (found != gate_kinds.end()) {
        return found->second;
    }
    int kind = kClosedTemplate;
    tree result = DECL_TEMPLATE_RESULT(general);
    if (result != NULL_TREE && TREE_CODE(result) != CONCEPT_DECL) {
        const FileEntry* file = file_at(defining_location(general));
        std::string path = file != nullptr ? read_as(file->relative) : std::string{};
        if (has_any_prefix(path, {kLayerTrees[0], kLayerTrees[1]})) {
            kind = kLayerTemplate;
        } else if (has_prefix(path, kCrucibleTree)) {
            kind = kCrucibleTemplate;
        }
    }
    gate_kinds.emplace(general, kind);
    return kind;
}

// The templates that one gate reads, whether the gate is in a header of
// include/crucible/, and the patterns of the class templates that hold the
// gate.  A gate of a class template that reads a member of its own instance
// is no open read: an explicit specialization of the class replaces the gate
// too.  A member of a different instance of the same template is an open read.
// The walk of the definition of a sealed template reads each type that the
// definition names, too (THE SEALED TEMPLATES).
struct GateReads {
    bool is_crucible = false;
    bool is_definition = false;
    // A walk at the event of a folded initializer keeps each template and
    // each class that it reads in DEFERRED, and the walk of the definition
    // reads them at the end of the parse (THE FOLDED SPECIALIZATIONS).
    bool is_deferred = false;
    std::vector<tree> deferred;
    std::vector<tree> templates;
    std::vector<tree> owners;
};

// THE FOLDED SPECIALIZATIONS
//     The front end replaces the initializer of a constexpr variable with its
//     value.  An explicit specialization does not depend on a template
//     parameter, so the definition of a sealed template keeps no read of the
//     initializer of an explicit specialization.  The fifth patch of GCC
//     gives the event PLUGIN_FOLDED_INITIALIZER with the initializer before
//     the fold.  The event walks it and keeps each template and each class
//     that it reads, for the object: an explicit specialization of a variable
//     template, or a static data member of an explicit specialization of a
//     class template.  Such an object, a template and a class instance stay
//     to the end of the unit, so no collection frees a kept tree.
std::unordered_map<tree, std::vector<tree>> folded_reads;

// The bound of a chain of sealed templates.  A longer chain is open, so the
// walk fails closed.
constexpr int kSealDepth = 256;
constexpr int kNoUnfinished = INT_MAX;

// The lowest depth of a sealed template whose walk has not finished, that
// the walks met since the last walk of a definition started.
int lowest_unfinished = kNoUnfinished;

void note_member_of(GateReads& reads, tree type);
void scan_type(GateReads& reads, tree type, int depth);
tree visit_gate(tree* node_pointer, int* walk_subtrees, void* data);
std::vector<tree> leaves_of(tree general);

void add_read(GateReads& reads, tree general) {
    if (std::find(reads.templates.begin(), reads.templates.end(), general) == reads.templates.end()) {
        reads.templates.push_back(general);
    }
}

// The template arguments of a read of a sealed template: a sealed template
// can read the members of a class that an argument names.  Complexity:
// linear in the nodes of the arguments.
void note_arguments(GateReads& reads, tree arguments, int depth) {
    if (arguments == NULL_TREE || depth >= kTypeDepth) {
        return;
    }
    if (TREE_CODE(arguments) == TREE_VEC) {
        for (int index = 0; index < TREE_VEC_LENGTH(arguments); ++index) {
            note_arguments(reads, TREE_VEC_ELT(arguments, index), depth + 1);
        }
        return;
    }
    if (ARGUMENT_PACK_P(arguments)) {
        note_arguments(reads, ARGUMENT_PACK_ARGS(arguments), depth + 1);
        return;
    }
    if (TYPE_P(arguments)) {
        scan_type(reads, arguments, depth + 1);
        return;
    }
    cp_walk_tree_without_duplicates(&arguments, visit_gate, &reads);
}

// One read of TEMPLATE_DECL with the template arguments ARGUMENTS, or
// NULL_TREE.  An open template is one read.  A sealed template gives the
// open leaves of its definition and of its arguments (THE SEALED TEMPLATES).
void note_open(GateReads& reads, tree template_decl, tree arguments) {
    if (template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL) {
        return;
    }
    if (reads.is_deferred) {
        reads.deferred.push_back(template_decl);
        note_arguments(reads, arguments, 0);
        return;
    }
    // A member template of a class template is a member of that class
    // template: an explicit specialization of the class replaces it.
    tree scope = DECL_CONTEXT(template_decl);
    if (scope != NULL_TREE && CLASS_TYPE_P(scope) && CLASSTYPE_TEMPLATE_INFO(scope) != NULL_TREE) {
        note_member_of(reads, scope);
        note_arguments(reads, arguments, 0);
        return;
    }
    int kind = gate_kind(template_decl);
    if (kind != kLayerTemplate && (kind != kCrucibleTemplate || !reads.is_crucible)) {
        return;
    }
    tree general = most_general_template(template_decl);
    general = general != NULL_TREE ? general : template_decl;
    if (!is_sealed_template(general)) {
        add_read(reads, general);
        return;
    }
    for (tree leaf : leaves_of(general)) {
        add_read(reads, leaf);
    }
    note_arguments(reads, arguments, 0);
}

// The patterns of the class templates that hold DECL, from DECL out: each
// enclosing class, and the pattern of DECL itself when it is a class template.
std::vector<tree> owner_patterns(tree decl) {
    std::vector<tree> owners;
    if (TREE_CODE(decl) == TEMPLATE_DECL && is_class_template(decl)) {
        owners.push_back(TYPE_MAIN_VARIANT(TREE_TYPE(DECL_TEMPLATE_RESULT(decl))));
    }
    int depth = 0;
    for (tree scope = CP_DECL_CONTEXT(decl); scope != NULL_TREE && TYPE_P(scope) && depth < kTypeDepth;
         scope = CP_TYPE_CONTEXT(scope), ++depth) {
        owners.push_back(TYPE_MAIN_VARIANT(scope));
    }
    return owners;
}

// A read of a member of a class template instance: the class template.
void note_member_of(GateReads& reads, tree type) {
    if (type == NULL_TREE || !TYPE_P(type)) {
        return;
    }
    if (TREE_CODE(type) == TYPENAME_TYPE) {
        note_member_of(reads, TYPE_CONTEXT(type));
        return;
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (std::find(reads.owners.begin(), reads.owners.end(), main_type) != reads.owners.end()) {
        return;
    }
    if (reads.is_deferred) {
        if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
            reads.deferred.push_back(main_type);
        }
        return;
    }
    if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE
        && TREE_CODE(CLASSTYPE_TI_TEMPLATE(main_type)) == TEMPLATE_DECL) {
        note_open(reads, CLASSTYPE_TI_TEMPLATE(main_type), CLASSTYPE_TI_ARGS(main_type));
    }
}

// A name of a function template or a variable template: a TEMPLATE_DECL, an
// overload set or a member reference, with its template arguments or
// NULL_TREE.
void note_template_name(GateReads& reads, tree name, tree arguments) {
    if (name == NULL_TREE) {
        return;
    }
    if (TREE_CODE(name) == BASELINK) {
        name = BASELINK_FUNCTIONS(name);
    }
    if (TREE_CODE(name) == TEMPLATE_DECL) {
        tree result = DECL_TEMPLATE_RESULT(name);
        if (result != NULL_TREE && (VAR_P(result) || TREE_CODE(result) == FUNCTION_DECL)) {
            note_open(reads, name, arguments);
        }
        return;
    }
    if (TREE_CODE(name) == OVERLOAD) {
        for (ovl_iterator candidate(name, true); candidate; ++candidate) {
            if (TREE_CODE(*candidate) == TEMPLATE_DECL) {
                note_open(reads, *candidate, arguments);
            }
        }
    }
}

// Each class template instance that a type of the definition of a sealed
// template names, through each pointer, reference, array, signature and
// decltype (THE SEALED TEMPLATES).  A gate names a type with no read, but the
// definition of a sealed template can give the type to the gate.
void scan_type(GateReads& reads, tree type, int depth) {
    if (type == NULL_TREE || !TYPE_P(type) || depth >= kTypeDepth) {
        return;
    }
    switch (TREE_CODE(type)) {
        case TYPENAME_TYPE:
            note_member_of(reads, TYPE_CONTEXT(type));
            return;
        case RECORD_TYPE:
        case UNION_TYPE:
            if (CLASS_TYPE_P(type) && CLASSTYPE_TEMPLATE_INFO(type) != NULL_TREE) {
                note_member_of(reads, type);
            }
            return;
        case POINTER_TYPE:
        case REFERENCE_TYPE:
        case ARRAY_TYPE:
            scan_type(reads, TREE_TYPE(type), depth + 1);
            return;
        case OFFSET_TYPE:
            scan_type(reads, TYPE_OFFSET_BASETYPE(type), depth + 1);
            scan_type(reads, TREE_TYPE(type), depth + 1);
            return;
        case FUNCTION_TYPE:
        case METHOD_TYPE:
            scan_type(reads, TREE_TYPE(type), depth + 1);
            for (tree parameter = TYPE_ARG_TYPES(type); parameter != NULL_TREE && parameter != void_list_node;
                 parameter = TREE_CHAIN(parameter)) {
                scan_type(reads, TREE_VALUE(parameter), depth + 1);
            }
            return;
        case DECLTYPE_TYPE: {
            tree expression = DECLTYPE_TYPE_EXPR(type);
            cp_walk_tree_without_duplicates(&expression, visit_gate, &reads);
            return;
        }
        default:
            return;
    }
}

tree visit_gate(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    auto& reads = *static_cast<GateReads*>(data);
    switch (TREE_CODE(node)) {
        case TEMPLATE_ID_EXPR:
            note_template_name(reads, TREE_OPERAND(node, 0), TREE_OPERAND(node, 1));
            break;
        case OVERLOAD:
            note_template_name(reads, node, NULL_TREE);
            break;
        case SCOPE_REF:
            note_member_of(reads, TREE_OPERAND(node, 0));
            break;
        case TYPENAME_TYPE:
            note_member_of(reads, TYPE_CONTEXT(node));
            break;
        case VAR_DECL:
        case FUNCTION_DECL:
            if (DECL_LANG_SPECIFIC(node) != nullptr && DECL_TEMPLATE_INFO(node) != NULL_TREE
                && TREE_CODE(DECL_TI_TEMPLATE(node)) == TEMPLATE_DECL && PRIMARY_TEMPLATE_P(DECL_TI_TEMPLATE(node))) {
                note_open(reads, DECL_TI_TEMPLATE(node), DECL_TI_ARGS(node));
            }
            if (DECL_CLASS_SCOPE_P(node)) {
                note_member_of(reads, DECL_CONTEXT(node));
            }
            *walk_subtrees = 0;
            break;
        default:
            if (TYPE_P(node) && reads.is_definition) {
                scan_type(reads, node, 0);
            }
            if (DECL_P(node) || TYPE_P(node)) {
                *walk_subtrees = 0;
            }
            break;
    }
    return NULL_TREE;
}

// ── The definitions of the sealed templates ─────────────────────────────

void walk_class_definition(GateReads& reads, tree type, int depth);

// The type and the initializer of a variable or of a data member, and the
// reads of its initializer before a fold (THE FOLDED SPECIALIZATIONS).
void walk_variable(GateReads& reads, tree variable) {
    scan_type(reads, TREE_TYPE(variable), 0);
    tree initial = DECL_INITIAL(variable);
    if (initial != NULL_TREE && initial != error_mark_node && TREE_CODE(initial) != DEFERRED_PARSE) {
        cp_walk_tree_without_duplicates(&initial, visit_gate, &reads);
    }
    auto folded = folded_reads.find(variable);
    if (folded == folded_reads.end()) {
        return;
    }
    for (tree read : folded->second) {
        if (TYPE_P(read)) {
            note_member_of(reads, read);
        } else {
            note_open(reads, read, NULL_TREE);
        }
    }
}

// True when OBJECT is an object of THE FOLDED SPECIALIZATIONS: an explicit
// specialization of a variable template, or a static data member of an
// explicit specialization of a class template.
bool is_folded_specialization(tree object) {
    if (object == NULL_TREE || !VAR_P(object) || DECL_LANG_SPECIFIC(object) == nullptr) {
        return false;
    }
    if (DECL_TEMPLATE_INFO(object) != NULL_TREE && DECL_TEMPLATE_SPECIALIZATION(object)) {
        return true;
    }
    tree scope = DECL_CONTEXT(object);
    return scope != NULL_TREE && CLASS_TYPE_P(scope) && CLASSTYPE_TEMPLATE_SPECIALIZATION(scope);
}

// PLUGIN_FOLDED_INITIALIZER: the reads of the initializer of an object of THE
// FOLDED SPECIALIZATIONS, before the fold.
void on_folded_initializer_home(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->original == error_mark_node
        || !is_specialization_home_active() || !is_folded_specialization(data->object)) {
        return;
    }
    GateReads reads;
    reads.is_deferred = true;
    reads.is_definition = true;
    tree original = data->original;
    cp_walk_tree_without_duplicates(&original, visit_gate, &reads);
    if (!reads.deferred.empty()) {
        std::vector<tree>& kept = folded_reads[data->object];
        kept.insert(kept.end(), reads.deferred.begin(), reads.deferred.end());
    }
}

// The signature, the constraint and the noexcept specification of a
// function, and its body when a constant evaluation or the deduction of its
// return type reads the body.  A gate reads only the signature of each other
// function.
void walk_function(GateReads& reads, tree function) {
    if (DECL_ARTIFICIAL(function)) {
        return;
    }
    scan_type(reads, TREE_TYPE(function), 0);
    tree raises = TYPE_RAISES_EXCEPTIONS(TREE_TYPE(function));
    if (raises != NULL_TREE && TREE_PURPOSE(raises) != NULL_TREE && TREE_CODE(TREE_PURPOSE(raises)) != DEFERRED_NOEXCEPT) {
        tree specification = TREE_PURPOSE(raises);
        cp_walk_tree_without_duplicates(&specification, visit_gate, &reads);
    }
    tree info = get_constraints(function);
    if (info != NULL_TREE && TREE_CODE(info) == CONSTRAINT_INFO && CI_DECLARATOR_REQS(info) != NULL_TREE) {
        tree requirement = CI_DECLARATOR_REQS(info);
        cp_walk_tree_without_duplicates(&requirement, visit_gate, &reads);
    }
    bool is_read_at_compile_time =
        DECL_DECLARED_CONSTEXPR_P(function) || DECL_IMMEDIATE_FUNCTION_P(function) || FNDECL_USED_AUTO(function);
    if (is_read_at_compile_time && DECL_SAVED_TREE(function) != NULL_TREE) {
        cp_walk_tree_without_duplicates(&DECL_SAVED_TREE(function), visit_gate, &reads);
    }
}

// One member of a class of the definition of a sealed template.
void walk_member(GateReads& reads, tree member, tree main_type, int depth) {
    switch (TREE_CODE(member)) {
        case FIELD_DECL:
        case VAR_DECL:
            walk_variable(reads, member);
            return;
        case FUNCTION_DECL:
            walk_function(reads, member);
            return;
        case TYPE_DECL:
            if (DECL_IMPLICIT_TYPEDEF_P(member)) {
                if (TREE_TYPE(member) != main_type) {
                    walk_class_definition(reads, TREE_TYPE(member), depth + 1);
                }
                return;
            }
            scan_type(reads, DECL_ORIGINAL_TYPE(member) != NULL_TREE ? DECL_ORIGINAL_TYPE(member) : TREE_TYPE(member),
                      0);
            return;
        case TEMPLATE_DECL: {
            tree result = DECL_TEMPLATE_RESULT(member);
            if (result == NULL_TREE) {
                return;
            }
            if (TREE_CODE(result) == FUNCTION_DECL) {
                walk_function(reads, result);
            } else if (VAR_P(result)) {
                walk_variable(reads, result);
            } else if (TREE_CODE(result) == TYPE_DECL && CLASS_TYPE_P(TREE_TYPE(result))) {
                walk_class_definition(reads, TREE_TYPE(result), depth + 1);
            } else if (TREE_CODE(result) == TYPE_DECL) {
                scan_type(reads, TREE_TYPE(result), 0);
            }
            return;
        }
        default:
            return;
    }
}

// The bases, the members and the friends that a class defines.  A class that
// the unit only declares has none.  Complexity: linear in the members.
void walk_class_definition(GateReads& reads, tree type, int depth) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || !COMPLETE_TYPE_P(main_type) || depth >= kTypeDepth) {
        return;
    }
    if (tree binfo = TYPE_BINFO(main_type)) {
        for (unsigned index = 0; index < BINFO_N_BASE_BINFOS(binfo); ++index) {
            scan_type(reads, BINFO_TYPE(BINFO_BASE_BINFO(binfo, index)), 0);
        }
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        walk_member(reads, member, main_type, depth);
    }
    tree decl = TYPE_MAIN_DECL(main_type);
    for (tree friends = decl != NULL_TREE ? DECL_FRIENDLIST(decl) : NULL_TREE; friends != NULL_TREE;
         friends = TREE_CHAIN(friends)) {
        for (tree item = TREE_VALUE(friends); item != NULL_TREE; item = TREE_CHAIN(item)) {
            tree friend_decl = TREE_VALUE(item);
            if (friend_decl != NULL_TREE && TREE_CODE(friend_decl) == TEMPLATE_DECL) {
                friend_decl = DECL_TEMPLATE_RESULT(friend_decl);
            }
            if (friend_decl != NULL_TREE && TREE_CODE(friend_decl) == FUNCTION_DECL) {
                walk_function(reads, friend_decl);
            }
        }
    }
}

// The definition of the sealed template GENERAL: its pattern and each
// explicit and partial specialization of it in the unit.
void walk_definition(GateReads& reads, tree general) {
    tree result = DECL_TEMPLATE_RESULT(general);
    bool is_class = is_class_template(general);
    if (is_class) {
        walk_class_definition(reads, TREE_TYPE(result), 0);
    } else {
        walk_variable(reads, result);
    }
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial == NULL_TREE || TREE_CODE(partial) != TEMPLATE_DECL || DECL_TEMPLATE_RESULT(partial) == NULL_TREE) {
            continue;
        }
        if (is_class) {
            walk_class_definition(reads, TREE_TYPE(DECL_TEMPLATE_RESULT(partial)), 0);
        } else {
            walk_variable(reads, DECL_TEMPLATE_RESULT(partial));
        }
    }
    for (tree entry = DECL_TEMPLATE_INSTANTIATIONS(general); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree value = TREE_VALUE(entry);
        if (value == NULL_TREE) {
            continue;
        }
        if (is_class && CLASS_TYPE_P(value) && CLASSTYPE_TEMPLATE_SPECIALIZATION(value)) {
            walk_class_definition(reads, value, 0);
        } else if (!is_class && VAR_P(value) && DECL_LANG_SPECIFIC(value) != nullptr
                   && DECL_TEMPLATE_SPECIALIZATION(value)) {
            walk_variable(reads, value);
        }
    }
}

// The open leaves of the sealed template GENERAL: each open template that
// its definition reads, at each depth (THE SEALED TEMPLATES).  A template
// whose walk has not finished gives no leaf, and a walk that met one keeps
// no result, unless the template is GENERAL itself.  A chain longer than
// kSealDepth gives GENERAL as its own leaf, so the walk fails closed.
// Complexity: linear in the nodes of each definition, one time for each
// template of the unit.
std::vector<tree> leaves_of(tree general) {
    auto cached = sealed_leaves.find(general);
    if (cached != sealed_leaves.end()) {
        return cached->second;
    }
    auto unfinished = sealing.find(general);
    if (unfinished != sealing.end()) {
        lowest_unfinished = std::min(lowest_unfinished, unfinished->second);
        return {};
    }
    int depth = static_cast<int>(sealing.size());
    if (depth >= kSealDepth) {
        return {general};
    }
    sealing.emplace(general, depth);
    int outer_lowest = lowest_unfinished;
    lowest_unfinished = kNoUnfinished;
    GateReads inner;
    inner.is_crucible = true;
    inner.is_definition = true;
    inner.owners = owner_patterns(general);
    walk_definition(inner, general);
    sealing.erase(general);
    int inner_lowest = lowest_unfinished;
    lowest_unfinished = std::min(outer_lowest, inner_lowest < depth ? inner_lowest : kNoUnfinished);
    if (inner_lowest >= depth) {
        sealed_leaves.emplace(general, inner.templates);
    }
    return inner.templates;
}

// One gate of the declaration DECL: each open template that EXPRESSION reads
// gives one open-read at the place of the gate.
void read_gate(tree expression, location_t location, tree decl) {
    if (expression == NULL_TREE || expression == error_mark_node) {
        return;
    }
    location_t gate_location = EXPR_P(expression) && EXPR_HAS_LOCATION(expression) ? EXPR_LOCATION(expression)
                                                                                    : location;
    Place place = site_place(gate_location);
    if (place.file == nullptr) {
        return;
    }
    GateReads reads;
    reads.is_crucible = has_prefix(read_as(place.file->relative), kCrucibleTree);
    reads.owners = owner_patterns(decl);
    cp_walk_tree_without_duplicates(&expression, visit_gate, &reads);
    for (tree template_decl : reads.templates) {
        add_home_finding(Kind::trait_open_read, place, qualified_name(template_decl));
    }
}

// True when the gates of the file of LOCATION count: a header of the three
// layers or its check file, read in this unit (THE SCOPE).
bool is_gate_file(location_t location) {
    const FileEntry* file = file_at(location);
    if (!is_scope_file(file)) {
        return false;
    }
    std::string path = read_as(file->relative);
    return has_any_prefix(path, {kLayerTrees[0], kLayerTrees[1], kCrucibleTree});
}

// True when a gate file can be a scope file of this unit: the main file or the
// sentinel is a header of the three layers or its check file, or the unit
// reads a header of the three layers that cannot compile alone (THE SCOPE).
// Most units compile a file outside them, and then read no gate.  The first
// call comes from the walk at the end of the parse, so state.entries holds each
// file that the unit read.  Complexity: linear in the files of the unit, one
// time.
bool has_gate_scope() {
    static const bool has_scope = [] {
        auto is_gate_path = [](const std::string& path) {
            return has_any_prefix(read_as(path), {kLayerTrees[0], kLayerTrees[1], kCrucibleTree});
        };
        const FileEntry* main_file = main_input_filename != nullptr ? &classify_file(main_input_filename) : nullptr;
        if ((main_file != nullptr && is_gate_path(main_file->relative))
            || (!state.sentinel.empty() && is_gate_path(state.sentinel))) {
            return true;
        }
        for (const auto& item : state.entries) {
            if (item.second.cannot_compile_alone && is_gate_path(item.second.relative)) {
                return true;
            }
        }
        return false;
    }();
    return has_scope;
}

// The gates of one declaration: a concept, the requires-clause of a template,
// and the requires-clause after the declarator of a function.  The tests of
// the trees come first, because a file test costs more.
void read_gates_of(tree decl) {
    if (!has_gate_scope()) {
        return;
    }
    tree concept_body = NULL_TREE;
    tree template_clause = NULL_TREE;
    tree trailing_clause = NULL_TREE;
    location_t location = DECL_SOURCE_LOCATION(decl);
    if (TREE_CODE(decl) == TEMPLATE_DECL) {
        tree result = DECL_TEMPLATE_RESULT(decl);
        tree levels = DECL_TEMPLATE_PARMS(decl);
        if (result != NULL_TREE && TREE_CODE(result) == CONCEPT_DECL) {
            concept_body = DECL_INITIAL(result);
            location = DECL_SOURCE_LOCATION(result);
        } else if (levels != NULL_TREE && TREE_CODE(levels) == TREE_LIST) {
            template_clause = TEMPLATE_PARMS_CONSTRAINTS(levels);
        }
    }
    if (concept_body == NULL_TREE && (TREE_CODE(decl) == TEMPLATE_DECL || TREE_CODE(decl) == FUNCTION_DECL)) {
        tree info = get_constraints(decl);
        if (info != NULL_TREE && TREE_CODE(info) == CONSTRAINT_INFO) {
            trailing_clause = CI_DECLARATOR_REQS(info);
        }
    }
    if ((concept_body == NULL_TREE && template_clause == NULL_TREE && trailing_clause == NULL_TREE)
        || !is_gate_file(location)) {
        return;
    }
    read_gate(concept_body, location, decl);
    read_gate(template_clause, location, decl);
    read_gate(trailing_clause, location, decl);
}

// The gates of the partial specializations of a class template or a variable
// template, which are no members of a scope.
void read_partial_gates(tree template_decl) {
    if (!has_gate_scope() || (!is_class_template(template_decl) && !is_variable_template(template_decl))) {
        return;
    }
    for (tree entry = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); entry != NULL_TREE; entry = TREE_CHAIN(entry)) {
        tree partial = TREE_VALUE(entry);
        if (partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL) {
            read_gates_of(partial);
        }
    }
}

// ── The walk ────────────────────────────────────────────────────────────

void walk_class(tree type, const HomeRow* scope_row, tree scope_named);

// The location of the definition of a declaration: the pattern of a class
// template, and the declaration itself otherwise.
location_t site_location(tree decl) {
    if (TREE_CODE(decl) == TEMPLATE_DECL && is_class_template(decl)) {
        return DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(TREE_TYPE(DECL_TEMPLATE_RESULT(decl))));
    }
    return DECL_SOURCE_LOCATION(decl);
}

// The site of DECL under ROW, which names the scope NAMED (THE SITES).
// IS_CLASS says that DECL is a class.
void read_row_site(tree decl, const HomeRow& row, tree named, bool is_class) {
    Home home;
    home.row = &row;
    home.home_class = row.home_class;
    home.entity = entity_text(named);
    bool is_complete = !is_class || COMPLETE_TYPE_P(TREE_TYPE(decl));
    switch (row.home_class) {
        case HomeClass::owner:
            // The definition of a class or of a class template, named by
            // itself.  The lists of a class template give its specializations.
            if (is_class && is_complete) {
                home.entity = qualified_name(decl);
                record_site(home, DECL_SOURCE_LOCATION(decl));
            } else if (TREE_CODE(decl) == TEMPLATE_DECL && is_class_template(decl)
                       && COMPLETE_TYPE_P(TREE_TYPE(DECL_TEMPLATE_RESULT(decl)))) {
                home.entity = qualified_name(decl);
                record_site(home, site_location(decl));
            }
            break;
        case HomeClass::federation: {
            // The entity, each member and each class inside it.
            bool is_pattern = TREE_CODE(decl) == TEMPLATE_DECL && is_class_template(decl);
            if (is_pattern && !COMPLETE_TYPE_P(TREE_TYPE(DECL_TEMPLATE_RESULT(decl)))) {
                break;
            }
            if (is_complete && (!DECL_ARTIFICIAL(decl) || is_class)) {
                record_site(home, site_location(decl));
            }
            break;
        }
        case HomeClass::trait:
            // Each declaration of a namespace of a row.  The row of a template
            // governs its specializations, and not its own declaration.
            if (TREE_CODE(named) == NAMESPACE_DECL && (!DECL_ARTIFICIAL(decl) || is_class)) {
                record_site(home, site_location(decl));
            }
            break;
        default:
            break;
    }
}

// How the walk reads the members of a class.
enum class MemberWalk : std::uint8_t {
    none,
    templates,  // only the member templates, which the rule specialization-home reads
    full,  // each member: a row governs the class, or a scope file defines it
};

// The member walk of a class (THE SCOPE).  A class of the tree needs the walk
// of its member templates while the rule specialization-home reads the
// templates with no row (THE TEMPLATES WITH NO ROW).
MemberWalk member_walk_of(tree type, const HomeRow* row) {
    if (row != nullptr) {
        return MemberWalk::full;
    }
    const FileEntry* file = file_at(DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(type)));
    if (file == nullptr) {
        return MemberWalk::none;
    }
    if (is_scope_file(file)) {
        return MemberWalk::full;
    }
    return is_specialization_home_active() ? MemberWalk::templates : MemberWalk::none;
}

// One declaration of a scope whose row is SCOPE_ROW, named at SCOPE_NAMED.  A
// declaration with a row of its own follows its own row (THE ROWS).
void read_declaration(tree decl, const HomeRow* scope_row, tree scope_named) {
    tree named = decl;
    const HomeRow* row = own_row(decl);
    if (row == nullptr) {
        row = scope_row;
        named = scope_named;
    }
    bool is_class = TREE_CODE(decl) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl));
    if (is_class && LAMBDA_TYPE_P(TREE_TYPE(decl))) {
        return;
    }
    read_derived_name(decl);
    read_gates_of(decl);
    if (TREE_CODE(decl) == TEMPLATE_DECL) {
        read_partial_gates(decl);
        read_specializations(decl);
    }
    if (row != nullptr) {
        read_row_site(decl, *row, named, is_class);
    }
    tree type = NULL_TREE;
    if (is_class) {
        type = TREE_TYPE(decl);
    } else if (TREE_CODE(decl) == TEMPLATE_DECL && is_class_template(decl)) {
        type = TREE_TYPE(DECL_TEMPLATE_RESULT(decl));
    }
    if (type != NULL_TREE) {
        walk_class(type, row, named);
    }
}

// The members of a class: each member function, static data member, nested
// class and member template.  A row of the class governs each member.
void walk_class(tree type, const HomeRow* scope_row, tree scope_named) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || TYPE_MAIN_DECL(main_type) == NULL_TREE || !walked.insert(main_type).second) {
        return;
    }
    MemberWalk walk = member_walk_of(main_type, scope_row);
    if (walk == MemberWalk::none) {
        return;
    }
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        if (walk == MemberWalk::templates) {
            if (TREE_CODE(member) == TEMPLATE_DECL) {
                read_partial_gates(member);
                read_specializations(member);
            }
            continue;
        }
        switch (TREE_CODE(member)) {
            case FIELD_DECL:
                read_derived_name(member);
                break;
            case TYPE_DECL:
                if (DECL_IMPLICIT_TYPEDEF_P(member) && TREE_TYPE(member) == main_type) {
                    break;
                }
                read_declaration(member, scope_row, scope_named);
                break;
            case FUNCTION_DECL:
            case VAR_DECL:
            case TEMPLATE_DECL:
                if (!DECL_ARTIFICIAL(member) || TREE_CODE(member) == TEMPLATE_DECL) {
                    read_declaration(member, scope_row, scope_named);
                }
                break;
            default:
                break;
        }
    }
}

void walk_namespace(tree ns, const HomeRow* scope_row, tree scope_named);

void walk_namespace_member(tree decl, const HomeRow* scope_row, tree scope_named) {
    switch (TREE_CODE(decl)) {
        case NAMESPACE_DECL:
            if (DECL_NAMESPACE_ALIAS(decl) == NULL_TREE && !in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
                read_derived_name(decl);
                walk_namespace(decl, scope_row, scope_named);
            }
            return;
        case TREE_LIST:
            if (TREE_VALUE(decl) != NULL_TREE && TREE_CODE(TREE_VALUE(decl)) != TREE_LIST) {
                walk_namespace_member(TREE_VALUE(decl), scope_row, scope_named);
            }
            return;
        case OVERLOAD:
            for (ovl_iterator candidate(decl, true); candidate; ++candidate) {
                if (TREE_CODE(*candidate) != OVERLOAD) {
                    walk_namespace_member(*candidate, scope_row, scope_named);
                }
            }
            return;
        default:
            break;
    }
    if (!DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl) || is_erroneous_decl(decl)) {
        return;
    }
    // A friend that a class template instance injects is no declaration of
    // the namespace.
    if (TREE_CODE(decl) == FUNCTION_DECL && DECL_LANG_SPECIFIC(decl) != nullptr && DECL_TEMPLATE_INFO(decl) != NULL_TREE
        && !DECL_TEMPLATE_SPECIALIZATION(decl)) {
        return;
    }
    read_declaration(decl, scope_row, scope_named);
}

void walk_namespace(tree ns, const HomeRow* scope_row, tree scope_named) {
    if (!walked.insert(ns).second) {
        return;
    }
    if (DECL_NAMESPACE_INLINE_P(ns) == 0) {
        if (const HomeRow* row = own_row(ns)) {
            scope_row = row;
            scope_named = ns;
        }
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        walk_namespace_member(decl, scope_row, scope_named);
    }
}

// ── The hooks ───────────────────────────────────────────────────────────

// True for an instantiation of a template: the family reads its pattern, and
// GCC calls the hooks for each instantiation too.
bool is_instantiation(tree decl) {
    return DECL_LANG_SPECIFIC(decl) != nullptr && DECL_TEMPLATE_INSTANTIATION(decl);
}

// PLUGIN_FINISH_DECL: an explicit specialization of a member, and a
// declaration with a derived name of combine_ids.
void on_finish_decl(void* gcc_data, void*) {
    tree decl = static_cast<tree>(gcc_data);
    if (decl == NULL_TREE || !DECL_P(decl) || (VAR_OR_FUNCTION_DECL_P(decl) && is_instantiation(decl))) {
        return;
    }
    build_index();
    read_member_specialization(decl);
    read_derived_name(decl);
}

// True when GCC marks FUNCTION as an instantiation, and the source wrote it.
// A member template that the source defines for one implicit instantiation,
// such as `template <> template <class U> void X<Fake>::f(U) {}`, has the mark
// of an instantiation, and its place is not the place of the general template.
// The place of a true instantiation is the place of its pattern.
bool is_written_instantiation(tree function) {
    tree context = DECL_CONTEXT(function);
    if (context == NULL_TREE || !CLASS_TYPE_P(context) || !CLASSTYPE_IMPLICIT_INSTANTIATION(context)
        || DECL_TEMPLATE_INFO(function) == NULL_TREE || TREE_CODE(DECL_TI_TEMPLATE(function)) != TEMPLATE_DECL) {
        return false;
    }
    tree general = most_general_template(DECL_TI_TEMPLATE(function));
    return general != NULL_TREE
        && DECL_SOURCE_LOCATION(function) != DECL_SOURCE_LOCATION(DECL_TEMPLATE_RESULT(general));
}

// PLUGIN_FINISH_PARSE_FUNCTION: each function definition, also of a template.
// An instantiation and an implicit member are no definition of the source.
void on_finish_function(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL
        || (DECL_ARTIFICIAL(function) && !LAMBDA_FUNCTION_P(function))) {
        return;
    }
    if (is_instantiation(function)) {
        if (is_written_instantiation(function)) {
            build_index();
            read_member_specialization(function);
        }
        return;
    }
    build_index();
    read_member_specialization(function);
    read_derived_name(function);
    read_combine_definition(function);
    read_body_shape(function);
}

// PLUGIN_FINISH_TYPE: each class definition, for an explicit specialization of
// a member class and a derived name.
void on_finish_type(void* gcc_data, void*) {
    tree type = static_cast<tree>(gcc_data);
    if (type == NULL_TREE || type == error_mark_node || !CLASS_TYPE_P(type)) {
        return;
    }
    build_index();
    tree decl = TYPE_MAIN_DECL(type);
    read_member_specialization(decl);
    read_derived_name(decl);
}

void on_collection(void*, void*) {
    template_homes.clear();
    gate_kinds.clear();
    walked.clear();
    sealed_leaves.clear();
    sealing.clear();
}

}  // namespace

// ── The place, for the two families ────────────────────────────────────

// The directory of the check files.
constexpr char kCheckTree[] = "test/layer/checks/";

// The place of a site (THE PLACE), or a place with no file for a site outside
// the source root.
Place site_place(location_t location) {
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
    if (entry.file_class == FileClass::outside) {
        return place;
    }
    place.file_class = entry.file_class;
    place.file = &entry;
    place.line = where.line;
    place.column = where.column;
    place.spelling = point;
    return place;
}

// The file of the place of LOCATION, or null outside the source root.  It
// reads the line map and no line or column, so it costs less than
// site_place(), and the tests of a file use it.
const FileEntry* file_at(location_t location) {
    if (location == UNKNOWN_LOCATION || location <= BUILTINS_LOCATION) {
        return nullptr;
    }
    const line_map_ordinary* map = nullptr;
    linemap_resolve_location(line_table, location, LRK_MACRO_EXPANSION_POINT, &map);
    if (map == nullptr) {
        return nullptr;
    }
    const FileEntry& entry = classify_file(ORDINARY_MAP_FILE_NAME(map));
    return entry.file_class == FileClass::outside ? nullptr : &entry;
}

// The path that a home compares: the header of a check file, and the path of
// each other file.
std::string read_as(const std::string& relative) {
    std::size_t tree_length = sizeof kCheckTree - 1;
    if (has_prefix(relative, kCheckTree) && relative.size() > tree_length + 4
        && relative.compare(relative.size() - 4, 4, ".cpp") == 0) {
        return std::string(kHeaderTree) + relative.substr(tree_length, relative.size() - tree_length - 4) + ".h";
    }
    return relative;
}

// The location that defines a class template or a variable template: its
// pattern when the unit defines it, and its declaration otherwise.
location_t defining_location(tree template_decl) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result == NULL_TREE) {
        return DECL_SOURCE_LOCATION(template_decl);
    }
    if (TREE_CODE(result) == TYPE_DECL && CLASS_TYPE_P(TREE_TYPE(result))) {
        return DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(TREE_TYPE(result)));
    }
    return DECL_SOURCE_LOCATION(result);
}

// The walk of the family at the end of the parse (THE SITES).
void walk_home() {
    build_index();
    walked.clear();
    walk_namespace(global_namespace, nullptr, NULL_TREE);
}

void register_home(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_TYPE, on_finish_type, nullptr);
    register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer_home, nullptr);
    register_callback(plugin_name, PLUGIN_GGC_START, on_collection, nullptr);
}

}  // namespace crucible_plugin
