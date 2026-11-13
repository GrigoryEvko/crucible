// The family definition home: each entity that a header of the base declares
// has its definition in that header.  A header that declares an entity and
// leaves its definition to a different file lets a unit that includes only
// that header give a definition of its own.  A definition of a member or of a
// friend has the access of the class, so it can read and write the private
// state of the class, such as a pointer that a bound check guards.  So the
// rule definition-home refuses each such declaration in a header of the base.
//
// THE UNIT
//     The rule reads one header in one unit: the header that the argument
//     sentinel names (THE SENTINEL of plugin.h).  A header of include/ has one
//     such unit in the target all, its sentinel unit or its check file.  So
//     each finding counts in one unit.  A unit whose main file is a source
//     file of src/foundation/ or src/fixy/ reads the base source files of the
//     unit: the main file and each header of those directories.  It gives the
//     records of THE CLASSES, and the finding elsewhere of THE OUT-OF-LINE
//     ENTITIES.  Each other unit reads nothing.
//
// THE KINDS
//     friend        a friend declaration of a class that the header defines
//                   names an entity that the header does not define: a class,
//                   a class template, a function or a function template.  A
//                   friend that a template parameter names, such as
//                   `friend Authority;`, is no finding, because the argument
//                   of the class names it.  A friend specialization names its
//                   explicit specialization, or the one partial
//                   specialization that matches its arguments.  A friend
//                   that matches more than one, or a constrained partial
//                   specialization whose constraints are not those of the
//                   class template that names the friend, is a finding,
//                   because the rule cannot tell which one it names.  A friend template with no primary
//                   definition, whose partial and explicit specializations
//                   the header defines, is no finding, because a friend
//                   cannot name a partial specialization.  It gives the
//                   record declared of THE CLASSES, and so does a friend
//                   class that is no template and has no definition in the
//                   unit.  A friend of THE OUT-OF-LINE ENTITIES and a friend
//                   of THE HOMES is no finding.  Its place is the class, and
//                   its entity is the friend
//     elsewhere     an entity that the header declares has its definition in
//                   a different file of the unit.  Its place is the
//                   declaration in the header.  A definition in the header of
//                   a member or a friend of a class of a different file is
//                   the kind too, at the definition
//     undefined     an entity that the header declares has no definition in
//                   the unit, other than a class of THE CLASSES.  Its place
//                   is the declaration in the header
//     The entities are each function, function template, class, class
//     template and variable of a namespace of the base, and each member
//     function, member template, static data member and member class of a
//     class that the header defines.  A deleted and a defaulted function is
//     defined where it is declared, and so is an inline or a constexpr
//     variable.  An explicit specialization, an instantiation, an artificial
//     declaration, a deduction guide and a friend that a class declares in
//     its body is no entity.  The rule reads the members and the friends of
//     a partial or an explicit specialization of a class template that the
//     header defines, as it reads a class.  It reads each friend before each
//     other entity, so a friend gives one finding and no second one.
//
// THE CLASSES
//     A class or a class template of a namespace, which no class of the
//     header names as a friend, can have no definition in any file.  A tag
//     that only names a type has none, and neither has a class template whose
//     partial and explicit specializations give each body.  The rule
//     specialization-home holds each such specialization to the home of its
//     template.  The unit of the header cannot see a definition in a file
//     that it does not include.  So the rule gives five records, and the
//     module of the rule joins them over the build:
//       declared    a class that the header declares and the unit does not
//                   define, at its declaration.  The entity of a class
//                   template has the word template after its name
//       defined     a class that the header or a base source file of the
//                   unit defines, at its definition
//       header      the header of the unit, at its first line
//       homed       a friend that a home row of the rule table names, with
//                   the paths of the row (THE HOMES)
//       body        an out-of-line function or static data member that a
//                   base source file of the unit defines, by its symbol
//     The module gives the finding elsewhere for a declared class that a
//     header defines, that more than one file defines, or that a source file
//     defines when the class is a template.  A class with its one definition
//     in a base source file is admitted, as THE OUT-OF-LINE ENTITIES are.  The
//     module also gives the finding for a class and for a symbol of the
//     records defined and body that two files define.  It refuses a build in
//     which a header of include/foundation/ or include/fixy/ has no record
//     header.
//
// THE OUT-OF-LINE ENTITIES
//     A function with external linkage, no inline, no constexpr, no consteval
//     and no template has one definition in the program, in a source file.
//     So has a static data member with no inline and no constexpr, and a
//     variable of a namespace that the header declares with extern and no
//     initializer.  §XV rule 3 and the door rule R4 put such a body in
//     src/foundation/ or src/fixy/.  Such an entity is no finding when the
//     unit has no definition of it, or when its definition stands in a base
//     source file.  This holds for a member, for a friend and for an entity
//     of a namespace.  Four checks fence the body:
//       - the rule base-namespace refuses each definition of an entity of the
//         base outside the base, so the rule admits such an entity only when
//         the compile runs that rule too (is_base_namespace_active)
//       - a header, or a file of src/ with inline, constexpr or a template,
//         that defines a member or a friend of a class of a different file
//         is the finding elsewhere, at the definition
//       - a symbol that two base source files define is the finding
//         elsewhere (the record body)
//       - a duplicate symbol in the objects of one link is a link error.
//
// THE HOMES
//     A friend that a home row of the class federation or owner names is no
//     finding.  The layer order puts its definition above the header, and
//     the rule home refuses each definition outside the paths of its row.
//     The record homed names the row.
//
// THE READS
//     GCC keeps one location for an entity: the location moves to the
//     definition, and a friend declaration moves it to the friend.  So the
//     family reads each place when the parser completes it:
//       PLUGIN_FINISH_DECL            the declaration of a function with no
//                                     body, of a static data member and of an
//                                     extern variable, in the header.  A
//                                     deleted or a defaulted function, the
//                                     definition of a static data member
//                                     outside its class and the definition of
//                                     a variable are definitions
//       PLUGIN_FINISH_PARSE_FUNCTION  the definition of each function
//       the walk at the end           each class, class template, function
//                                     template and member, at the location
//                                     that GCC keeps.  The template of a class
//                                     keeps the location of its first
//                                     declaration, and the pattern moves to
//                                     the definition
//     A place in the check file of the header stands where the header stands
//     (home.cpp, THE PLACE).
//
// WHAT THE FAMILY DOES NOT READ
//     A forward declaration of a class, or a declaration of a function
//     template that is no member, gives no event, and GCC moves its location
//     to the definition.  So such a declaration is no finding when the unit
//     of the header reads the definition from a different file.  A header
//     that includes the file of the definition gives each unit that includes
//     it the definition, so no unit can give a second one.  A check file that
//     includes the file of the definition after its header hides the
//     declaration.  The control control-known-gaps of
//     utils/plants/definition-home pins the gap.  The records of THE CLASSES
//     name a class by its scopes and its name, so a definition of a class of
//     the same name in a function scope does not join.  A header of src/ is
//     no header of a sentinel, and the rule does not read it.
//
// THE ACTIVATION
//     The argument definition-home=on turns on the rule (plugin.cpp), and
//     Quarantine.cmake gives it to each compile of the tree.  A compile with
//     the argument sentinel reads its header, and a compile of a base source
//     file gives the records defined.  Each other compile reads nothing.

#include "plugin.h"

namespace crucible_plugin {

namespace {

bool is_active = false;

// The classes that the header defines, in the sequence of the walk, and each
// class of a namespace that the header declares and does not define, with
// its place (THE KINDS, THE CLASSES).
std::vector<tree> header_classes;
struct DeclaredClass {
    tree decl = NULL_TREE;
    location_t place = UNKNOWN_LOCATION;
};
std::vector<DeclaredClass> declared_classes;

// The place of the first declaration of each function and variable that the
// header declares with no definition (THE READS).
std::unordered_map<tree, location_t> declaration_places;

// The place of the definition of each function and variable, at its event
// (THE READS).
std::unordered_map<tree, location_t> definition_places;

// The entities that the rule judged, so that each one gives one finding.
std::unordered_set<tree> judged;

// The classes whose members and friends the rule read.
std::unordered_set<tree> read_classes;

// True when FILE stands where the header of the argument sentinel stands: the
// header, and its check file (THE READS).
bool is_header(const FileEntry* file) {
    if (file == nullptr || file->file_class != FileClass::base) {
        return false;
    }
    return file->relative == state.sentinel || read_as(file->relative) == state.sentinel;
}

bool is_header_at(location_t location) { return location > BUILTINS_LOCATION && is_header(file_at(location)); }

// True when FILE is a source file of the base, where an out-of-line entity has
// its definition (THE OUT-OF-LINE ENTITIES).
bool is_base_source(const FileEntry* file) {
    return file != nullptr && file->file_class == FileClass::base
        && (has_prefix(file->relative, "src/foundation/") || has_prefix(file->relative, "src/fixy/"));
}

// What the rule reads in this unit (THE UNIT): the header of the argument
// sentinel, the base source file of the main file, or nothing.
enum class Mode : std::uint8_t {
    off,
    header,
    source,
};

bool is_mode_known = false;
Mode unit_mode = Mode::off;
const FileEntry* source_main = nullptr;

// The file of the main file of the unit when it is a base source file, or
// null (THE UNIT).
const FileEntry* base_source_main() {
    if (main_input_filename == nullptr) {
        return nullptr;
    }
    std::string main_real = real_path(main_input_filename);
    const FileEntry* entry = &entry_of_real_path(main_real);
    return is_base_source(entry) ? entry : nullptr;
}

// The mode of the unit, known at the first request, when the main file is.
Mode mode() {
    if (!is_active) {
        return Mode::off;
    }
    if (!is_mode_known) {
        is_mode_known = true;
        if (!state.sentinel.empty()) {
            unit_mode = Mode::header;
        } else {
            source_main = base_source_main();
            unit_mode = source_main != nullptr ? Mode::source : Mode::off;
        }
    }
    return unit_mode;
}

// True when DECL is an entity of the base: a declaration of a namespace of the
// base, or a member of a class of such a namespace.
bool is_base_entity(tree decl) {
    tree class_scope = NULL_TREE;
    return in_base_namespace(enclosing_namespace_of(decl, class_scope));
}

// True when DECL is no entity of THE KINDS: an erroneous, an artificial or a
// builtin declaration, a clone, a deduction guide, an instantiation and an
// explicit specialization.
bool is_no_entity(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || is_erroneous_decl(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)
        || DECL_ARTIFICIAL(decl)) {
        return true;
    }
    if (TREE_CODE(decl) == FUNCTION_DECL && (DECL_CLONED_FUNCTION_P(decl) || deduction_guide_p(decl))) {
        return true;
    }
    if (VAR_OR_FUNCTION_DECL_P(decl) && DECL_LANG_SPECIFIC(decl) != nullptr
        && (DECL_TEMPLATE_SPECIALIZATION(decl) || DECL_TEMPLATE_INSTANTIATION(decl))) {
        return true;
    }
    return false;
}

// True when DECL is defined where it is declared: a deleted or a defaulted
// function, and an inline or a constexpr variable.
bool is_defined_at_declaration(tree decl) {
    if (TREE_CODE(decl) == FUNCTION_DECL) {
        return DECL_LANG_SPECIFIC(decl) != nullptr && (DECL_DELETED_FN(decl) || DECL_DEFAULTED_FN(decl));
    }
    if (VAR_P(decl)) {
        return DECL_DECLARED_CONSTEXPR_P(decl) || (DECL_LANG_SPECIFIC(decl) != nullptr && DECL_INLINE_VAR_P(decl));
    }
    return false;
}

// The place of the definition of the function or the variable DECL in the
// unit, or UNKNOWN_LOCATION.
location_t definition_of_decl(tree decl) {
    auto found = definition_places.find(decl);
    if (found != definition_places.end()) {
        return found->second;
    }
    if (is_defined_at_declaration(decl)) {
        return DECL_SOURCE_LOCATION(decl);
    }
    return UNKNOWN_LOCATION;
}

// The place of the definition of the class TYPE, or UNKNOWN_LOCATION.
location_t definition_of_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!COMPLETE_TYPE_P(main_type) || TYPE_MAIN_DECL(main_type) == NULL_TREE) {
        return UNKNOWN_LOCATION;
    }
    return DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(main_type));
}

// The class of a class template, or NULL_TREE.  An alias template names a type
// and declares no class, and the closure of a lambda in a template is no
// class that a declaration names.
tree pattern_class(tree template_decl) {
    tree result = DECL_TEMPLATE_RESULT(template_decl);
    if (result == NULL_TREE || TREE_CODE(result) != TYPE_DECL || !DECL_IMPLICIT_TYPEDEF_P(result)
        || !CLASS_TYPE_P(TREE_TYPE(result)) || LAMBDA_TYPE_P(TREE_TYPE(result))) {
        return NULL_TREE;
    }
    return TREE_TYPE(result);
}

// The place of the definition of the entity DECL: a function, a variable, a
// class or a template, or UNKNOWN_LOCATION.
location_t definition_of(tree decl) {
    if (TREE_CODE(decl) == TEMPLATE_DECL) {
        tree type = pattern_class(decl);
        if (type != NULL_TREE) {
            return definition_of_class(type);
        }
        tree result = DECL_TEMPLATE_RESULT(decl);
        return result != NULL_TREE ? definition_of_decl(result) : UNKNOWN_LOCATION;
    }
    if (TREE_CODE(decl) == TYPE_DECL) {
        return definition_of_class(TREE_TYPE(decl));
    }
    return definition_of_decl(decl);
}

// True when DECL has one definition in the program by its form: a function
// with external linkage and no inline, constexpr, consteval or template, a
// static data member with no inline, constexpr or template, and an extern
// variable of a namespace.  A member, a friend and an entity of a namespace
// can have the form.
bool has_one_body_form(tree decl) {
    if (decl_linkage(decl) != lk_external) {
        return false;
    }
    if (TREE_CODE(decl) == FUNCTION_DECL) {
        return DECL_LANG_SPECIFIC(decl) == nullptr
            || (!DECL_DECLARED_INLINE_P(decl) && !DECL_DECLARED_CONSTEXPR_P(decl) && !DECL_IMMEDIATE_FUNCTION_P(decl)
                && DECL_TEMPLATE_INFO(decl) == NULL_TREE && DECL_FRIEND_CONTEXT(decl) == NULL_TREE
                && !DECL_DELETED_FN(decl) && !DECL_DEFAULTED_FN(decl));
    }
    if (!VAR_P(decl) || is_defined_at_declaration(decl)
        || (DECL_LANG_SPECIFIC(decl) != nullptr && DECL_TEMPLATE_INFO(decl) != NULL_TREE)) {
        return false;
    }
    return DECL_CLASS_SCOPE_P(decl) || DECL_EXTERNAL(decl);
}

// True when DECL is an out-of-line entity (THE OUT-OF-LINE ENTITIES): an
// entity of the form of has_one_body_form, in a compile that runs the rule
// base-namespace too.
bool is_out_of_line(tree decl) { return is_base_namespace_active() && has_one_body_form(decl); }

// One finding of KIND at LOCATION for ENTITY.
void record_site(Kind kind, location_t location, const std::string& entity) {
    Place place = site_place(location);
    if (place.file != nullptr) {
        add_finding(kind, nullptr, place, entity);
    }
}

// The name of the entity DECL for a line: its scopes and its name.  GCC names
// a constructor and a destructor with an internal name, so the line gives the
// name of the class, and a ~ before it for a destructor.
std::string entity_name(tree decl) {
    if (TREE_CODE(decl) == TYPE_DECL && CLASS_TYPE_P(TREE_TYPE(decl))
        && CLASSTYPE_TEMPLATE_INFO(TREE_TYPE(decl)) != NULL_TREE) {
        return type_text(TREE_TYPE(decl));
    }
    if (TREE_CODE(decl) == FUNCTION_DECL && DECL_CLASS_SCOPE_P(decl)
        && (DECL_CONSTRUCTOR_P(decl) || DECL_DESTRUCTOR_P(decl))) {
        tree class_decl = TYPE_MAIN_DECL(DECL_CONTEXT(decl));
        std::string class_name = qualified_name(class_decl);
        std::string simple = DECL_NAME(class_decl) != NULL_TREE ? IDENTIFIER_POINTER(DECL_NAME(class_decl)) : "";
        return class_name + (DECL_DESTRUCTOR_P(decl) ? "::~" : "::") + simple;
    }
    return qualified_name(decl);
}

// The name of the class TYPE for a record of THE CLASSES: the name of its
// template for the pattern of a class template, so that a declaration and a
// definition of one template give one name.
std::string class_entity(tree type) {
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    if (info != NULL_TREE && TI_TEMPLATE(info) != NULL_TREE && TREE_CODE(TI_TEMPLATE(info)) == TEMPLATE_DECL
        && PRIMARY_TEMPLATE_P(TI_TEMPLATE(info))) {
        return qualified_name(most_general_template(TI_TEMPLATE(info)));
    }
    return qualified_name(TYPE_MAIN_DECL(type));
}

// The text of a definition at LOCATION for an entity of a finding.
std::string defined_in(location_t location) {
    const FileEntry* file = file_at(location);
    return file != nullptr ? " defined in " + file->relative : std::string(" defined outside the root");
}

// Judges the entity DECL that the header declares at DECLARED_AT, one time
// (THE KINDS).  MAY_BE_OUT_OF_LINE admits the cases of THE OUT-OF-LINE
// ENTITIES: it holds for a function and a variable, and not for a class.
void judge(tree decl, location_t declared_at, bool may_be_out_of_line) {
    if (!judged.insert(decl).second) {
        return;
    }
    bool is_exempt = may_be_out_of_line && is_out_of_line(decl);
    location_t defined_at = definition_of(decl);
    if (defined_at == UNKNOWN_LOCATION) {
        if (!is_exempt) {
            record_site(Kind::definition_undefined, declared_at, entity_name(decl));
        }
        return;
    }
    if (is_header_at(defined_at) || (is_exempt && is_base_source(file_at(defined_at)))) {
        return;
    }
    record_site(Kind::definition_elsewhere, declared_at, entity_name(decl) + defined_in(defined_at));
}

// The template that a friend template declaration names.  GCC gives a friend
// declaration of a class template inside that template a template of its own,
// so the lookup of its name finds the template.
tree friend_template(tree template_decl) {
    tree context = CP_DECL_CONTEXT(template_decl);
    if (context == NULL_TREE || DECL_NAME(template_decl) == NULL_TREE) {
        return template_decl;
    }
    tree found = lookup_qualified_name(context, DECL_NAME(template_decl));
    return found != NULL_TREE && TREE_CODE(found) == TEMPLATE_DECL ? found : template_decl;
}

// Each function of the overload set that a friend function declaration names,
// other than the friend declaration itself.  A friend declaration in a class
// template stays a declaration of its own until an instantiation, so the
// declaration that it names is one of the functions of its name.
std::vector<tree> named_functions(tree friend_decl) {
    std::vector<tree> functions;
    tree context = CP_DECL_CONTEXT(friend_decl);
    if (context == NULL_TREE || DECL_NAME(friend_decl) == NULL_TREE) {
        return functions;
    }
    tree found = lookup_qualified_name(context, DECL_NAME(friend_decl));
    if (found != NULL_TREE && TREE_CODE(found) == BASELINK) {
        found = BASELINK_FUNCTIONS(found);
    }
    if (found == NULL_TREE || found == error_mark_node) {
        return functions;
    }
    if (TREE_CODE(found) != OVERLOAD && TREE_CODE(found) != FUNCTION_DECL && TREE_CODE(found) != TEMPLATE_DECL) {
        return functions;
    }
    for (ovl_iterator candidate(found, true); candidate; ++candidate) {
        tree function = *candidate;
        if (function != friend_decl && (TREE_CODE(function) == FUNCTION_DECL || TREE_CODE(function) == TEMPLATE_DECL)) {
            functions.push_back(function);
        }
    }
    return functions;
}

// The text of a friend for a finding: its name and the place of its
// definition.
std::string friend_text(tree entity, location_t defined_at) {
    return entity_name(entity)
         + (defined_at == UNKNOWN_LOCATION ? std::string(" with no definition in the unit") : defined_in(defined_at));
}

// True when the class template TEMPLATE_DECL has no primary definition in
// the unit, and the header defines each of its partial and explicit
// specializations that the unit defines, at least one (THE KINDS).  A friend
// declaration cannot name a partial specialization, so a class that gives a
// specialization access names the template.  Complexity: linear in the
// specializations of the template.
bool is_closed_template(tree template_decl) {
    tree type = pattern_class(template_decl);
    if (type == NULL_TREE || COMPLETE_TYPE_P(type)) {
        return false;
    }
    bool has_specialization = false;
    for (tree item = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree partial = TREE_VALUE(item);
        tree partial_type =
            partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL ? pattern_class(partial) : NULL_TREE;
        location_t partial_at = partial_type != NULL_TREE ? definition_of_class(partial_type) : UNKNOWN_LOCATION;
        if (partial_at == UNKNOWN_LOCATION) {
            continue;
        }
        if (!is_header_at(partial_at)) {
            return false;
        }
        has_specialization = true;
    }
    for (tree item = DECL_TEMPLATE_INSTANTIATIONS(template_decl); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree instance = TREE_VALUE(item);
        if (instance == NULL_TREE || !CLASS_TYPE_P(instance) || !CLASSTYPE_TEMPLATE_SPECIALIZATION(instance)) {
            continue;
        }
        location_t instance_at = definition_of_class(instance);
        if (instance_at == UNKNOWN_LOCATION) {
            continue;
        }
        if (!is_header_at(instance_at)) {
            return false;
        }
        has_specialization = true;
    }
    return has_specialization;
}

// The home row of the class federation or owner that names the entity DECL,
// or null (THE HOMES).
const HomeRow* home_row_of(tree decl) {
    std::string name = qualified_name(decl);
    for (const HomeRow& row : state.table.homes) {
        if (!row.is_one_name && !row.is_args && row.entity == name
            && (row.home_class == HomeClass::federation || row.home_class == HomeClass::owner)) {
            return &row;
        }
    }
    return nullptr;
}

// The text of the record homed: the friend, the class and the line of its
// home row, and the paths of the row.
std::string homed_text(tree entity, const HomeRow& row) {
    std::string text =
        qualified_name(entity) + " home " + home_class_name(row.home_class) + " line " + std::to_string(row.line);
    for (const std::string& path : row.paths) {
        text += " " + path;
    }
    return text;
}

// True when the friend function FUNCTION, whose definition stands at
// DEFINED_AT or nowhere in the unit, is admitted: a definition in the header,
// or an out-of-line entity with no definition in the unit or with its
// definition in a base source file (THE OUT-OF-LINE ENTITIES).
bool is_admitted_friend_function(tree function, location_t defined_at) {
    if (defined_at != UNKNOWN_LOCATION && is_header_at(defined_at)) {
        return true;
    }
    tree decl = TREE_CODE(function) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(function) : function;
    return decl != NULL_TREE && TREE_CODE(decl) == FUNCTION_DECL && is_out_of_line(decl)
        && (defined_at == UNKNOWN_LOCATION || is_base_source(file_at(defined_at)));
}

// The place of a record of a friend class: its declaration in the header, or
// the place of the class that names it.
location_t friend_record_place(tree entity, location_t class_place) {
    location_t declared_at = DECL_SOURCE_LOCATION(entity);
    return is_header_at(declared_at) ? declared_at : class_place;
}

// One friend of a class that the header defines, at the place of the class
// (THE KINDS).  ENTITY is the declaration of the friend.  A friend of THE
// HOMES gives the record homed.  A friend template whose specializations the
// header defines, and whose primary has no definition in the unit, gives the
// record declared of THE CLASSES, and so does a friend class that is no
// template and has no definition in the unit.  A friend function with no
// definition of its own names a function of its name, and the rule admits it
// only when it admits each function of that name, because the rule cannot
// tell which one it names.
void judge_friend(tree entity, location_t class_place) {
    if (entity == NULL_TREE || !is_base_entity(entity)) {
        return;
    }
    judged.insert(entity);
    if (TREE_CODE(entity) == TEMPLATE_DECL) {
        tree result = DECL_TEMPLATE_RESULT(entity);
        if (result != NULL_TREE) {
            judged.insert(result);
        }
    }
    bool is_function = TREE_CODE(entity) == FUNCTION_DECL
                    || (TREE_CODE(entity) == TEMPLATE_DECL && pattern_class(entity) == NULL_TREE);
    location_t defined_at = definition_of(entity);
    if ((defined_at != UNKNOWN_LOCATION && is_header_at(defined_at))
        || (is_function && is_admitted_friend_function(entity, defined_at))) {
        return;
    }
    if (const HomeRow* row = home_row_of(entity); row != nullptr) {
        record_site(Kind::definition_homed, class_place, homed_text(entity, *row));
        return;
    }
    if (defined_at != UNKNOWN_LOCATION) {
        if (!is_header_at(defined_at)) {
            record_site(Kind::definition_friend, class_place, friend_text(entity, defined_at));
        }
        return;
    }
    bool is_plain_class = TREE_CODE(entity) == TYPE_DECL && CLASS_TYPE_P(TREE_TYPE(entity))
                       && CLASSTYPE_TEMPLATE_INFO(TREE_TYPE(entity)) == NULL_TREE;
    if (is_closed_template(entity) || is_plain_class) {
        tree type = TREE_CODE(entity) == TEMPLATE_DECL ? pattern_class(entity) : TREE_TYPE(entity);
        bool is_template = TREE_CODE(entity) == TEMPLATE_DECL;
        record_site(Kind::definition_declared, friend_record_place(entity, class_place),
                    class_entity(type) + (is_template ? " template" : ""));
        return;
    }
    std::vector<tree> functions = is_function ? named_functions(entity) : std::vector<tree>{};
    if (functions.empty()) {
        record_site(Kind::definition_friend, class_place, friend_text(entity, UNKNOWN_LOCATION));
        return;
    }
    for (tree function : functions) {
        judged.insert(function);
    }
    for (tree function : functions) {
        location_t function_at = definition_of(function);
        if (!is_admitted_friend_function(function, function_at)) {
            record_site(Kind::definition_friend, class_place, friend_text(function, function_at));
            return;
        }
    }
}

// True when the constraints of the partial specialization PARTIAL hold for
// each instance of the class OWNER that names it: OWNER is a template with
// constraints equivalent to those of PARTIAL, such as
// `template <IsBrand Brand> class BrandedIndex` and the partial
// specialization `template <IsBrand Brand> struct niche<BrandedIndex<Brand>>`.
bool holds_for_owner(tree partial, tree owner) {
    tree info = owner != NULL_TREE && CLASS_TYPE_P(owner) ? CLASSTYPE_TEMPLATE_INFO(owner) : NULL_TREE;
    tree owner_template = info != NULL_TREE ? TI_TEMPLATE(info) : NULL_TREE;
    return owner_template != NULL_TREE && TREE_CODE(owner_template) == TEMPLATE_DECL
        && equivalently_constrained(partial, owner_template);
}

// The partial specialization of the template GENERAL that the dependent
// arguments ARGUMENTS of a friend of the class OWNER name, the template
// GENERAL when none does, or error_mark_node when the rule cannot tell: a
// match of more than one, or of a constrained partial specialization whose
// constraints do not hold for each instance of OWNER (holds_for_owner).
// Complexity: linear in the partial specializations of the template.
tree dependent_partial(tree general, tree arguments, tree owner) {
    tree found = NULL_TREE;
    for (tree item = DECL_TEMPLATE_SPECIALIZATIONS(general); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree partial = TREE_VALUE(item);
        if (partial == NULL_TREE || TREE_CODE(partial) != TEMPLATE_DECL
            || !comp_template_args(TREE_PURPOSE(item), arguments)) {
            continue;
        }
        if (found != NULL_TREE || (get_constraints(partial) != NULL_TREE && !holds_for_owner(partial, owner))) {
            return error_mark_node;
        }
        found = partial;
    }
    return found != NULL_TREE ? found : general;
}

// The partial specialization of the template GENERAL that the overload
// resolution of the language selects for the class TYPE, the template GENERAL
// when none matches, or error_mark_node when two match.
tree selected_partial(tree type, tree general) {
    tree selected = most_specialized_partial_spec(type, tf_none);
    if (selected == error_mark_node) {
        return error_mark_node;
    }
    if (selected == NULL_TREE) {
        return general;
    }
    if (TREE_CODE(selected) == TEMPLATE_INFO) {
        return TI_TEMPLATE(selected);
    }
    return TREE_CODE(selected) == TREE_LIST ? TREE_VALUE(selected) : error_mark_node;
}

// The declaration that a friend class TYPE names (THE KINDS).  An explicit
// specialization is its own class, also when the unit does not define it.  A
// specialization with dependent arguments names the partial specialization
// that matches them, such as `friend struct niche<View<Element, Count>>`, also
// when GCC gives the friend the class of that partial specialization.  A
// different specialization names the partial specialization that the
// language selects for it.  Each names its template when no partial
// specialization matches.  The result is error_mark_node when the rule cannot
// tell.  OWNER is the class that names the friend.
tree friend_class(tree type, tree owner) {
    tree info = CLASSTYPE_TEMPLATE_INFO(type);
    if (info == NULL_TREE || TI_TEMPLATE(info) == NULL_TREE || TREE_CODE(TI_TEMPLATE(info)) != TEMPLATE_DECL) {
        return TYPE_MAIN_DECL(type);
    }
    tree general = most_general_template(TI_TEMPLATE(info));
    tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(info));
    if (uses_template_parms(arguments)) {
        return dependent_partial(general, arguments, owner);
    }
    if (CLASSTYPE_TEMPLATE_SPECIALIZATION(type)) {
        return TYPE_MAIN_DECL(type);
    }
    return selected_partial(type, general);
}

// The entity of a friend of an unknown shape: a finding, so that the rule
// fails closed (THE KINDS).
std::string unknown_friend(tree value) {
    return "a friend of the tree code " + std::string(get_tree_code_name(TREE_CODE(value)));
}

// Each friend of the class TYPE that the header defines.  A friend that a
// template parameter or a dependent name gives is no finding, and each other
// shape that the rule cannot read is one (THE KINDS).
void judge_friends(tree type, location_t class_place) {
    for (tree item = CLASSTYPE_FRIEND_CLASSES(type); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree value = TREE_VALUE(item);
        if (value == NULL_TREE) {
            continue;
        }
        if (TREE_CODE(value) == TEMPLATE_DECL) {
            judge_friend(friend_template(value), class_place);
        } else if (TYPE_P(value) && CLASS_TYPE_P(value)) {
            tree named = friend_class(TYPE_MAIN_VARIANT(value), type);
            if (named == error_mark_node) {
                record_site(Kind::definition_friend, class_place,
                            "a friend specialization of " + type_text(value)
                                + " that matches a constrained partial specialization, or more than one");
            } else {
                judge_friend(named, class_place);
            }
        } else if (!TYPE_P(value)) {
            record_site(Kind::definition_friend, class_place, unknown_friend(value));
        }
    }
    tree decl = TYPE_MAIN_DECL(type);
    for (tree friends = decl != NULL_TREE ? DECL_FRIENDLIST(decl) : NULL_TREE; friends != NULL_TREE;
         friends = TREE_CHAIN(friends)) {
        for (tree item = TREE_VALUE(friends); item != NULL_TREE; item = TREE_CHAIN(item)) {
            tree value = TREE_VALUE(item);
            if (value != NULL_TREE && (TREE_CODE(value) == FUNCTION_DECL || TREE_CODE(value) == TEMPLATE_DECL)) {
                judge_friend(value, class_place);
            } else if (value != NULL_TREE) {
                record_site(Kind::definition_friend, class_place, unknown_friend(value));
            }
        }
    }
}

// The place of the declaration of the member DECL in the header: the place of
// its event, its location when it stays in the header, or the place of its
// class.
location_t member_place(tree decl, location_t class_place) {
    auto found = declaration_places.find(decl);
    if (found != declaration_places.end()) {
        return found->second;
    }
    return is_header_at(DECL_SOURCE_LOCATION(decl)) ? DECL_SOURCE_LOCATION(decl) : class_place;
}

// True when the header defines the class TYPE.
bool is_defined_in_header(tree type) {
    location_t defined_at = definition_of_class(type);
    return defined_at != UNKNOWN_LOCATION && is_header_at(defined_at);
}

// The class that the member MEMBER of the class OWNER declares: a member
// class or the pattern of a member class template, or NULL_TREE.
tree member_class_of(tree member, tree owner) {
    tree type = NULL_TREE;
    if (TREE_CODE(member) == TEMPLATE_DECL) {
        type = pattern_class(member);
    } else if (TREE_CODE(member) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(member) && CLASS_TYPE_P(TREE_TYPE(member))) {
        type = TREE_TYPE(member);
    }
    if (type == NULL_TREE || TYPE_MAIN_VARIANT(type) == owner || LAMBDA_TYPE_P(TYPE_MAIN_VARIANT(type))
        || TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(type)) == NULL_TREE) {
        return NULL_TREE;
    }
    return TYPE_MAIN_VARIANT(type);
}

// The class TYPE, which the header defines, and each member class that the
// header defines, into header_classes one time each (THE KINDS).
void collect_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    if (!CLASS_TYPE_P(main_type) || TYPE_MAIN_DECL(main_type) == NULL_TREE || !read_classes.insert(main_type).second) {
        return;
    }
    header_classes.push_back(main_type);
    for (tree member = TYPE_FIELDS(main_type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_type = member_class_of(member, main_type);
        if (member_type != NULL_TREE && is_defined_in_header(member_type)) {
            collect_class(member_type);
        }
    }
}

// The place of the class TYPE: the place of its definition.
location_t class_place_of(tree type) { return DECL_SOURCE_LOCATION(TYPE_MAIN_DECL(type)); }

// Each member of the class TYPE, which the header defines.  A member class
// that the header defines has a read of its own (collect_class).
void read_members(tree type) {
    location_t class_place = class_place_of(type);
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_type = member_class_of(member, type);
        if (member_type != NULL_TREE) {
            bool is_closed = TREE_CODE(member) == TEMPLATE_DECL && is_closed_template(member);
            if (!is_defined_in_header(member_type) && !is_closed) {
                tree decl = TYPE_MAIN_DECL(member_type);
                judge(decl, member_place(decl, class_place), false);
            }
            continue;
        }
        if (TREE_CODE(member) == FUNCTION_DECL || TREE_CODE(member) == VAR_DECL) {
            if (!is_no_entity(member)) {
                judge(member, member_place(member, class_place), true);
            }
        } else if (TREE_CODE(member) == TEMPLATE_DECL && DECL_TEMPLATE_RESULT(member) != NULL_TREE
                   && TREE_CODE(DECL_TEMPLATE_RESULT(member)) == FUNCTION_DECL
                   && !is_no_entity(DECL_TEMPLATE_RESULT(member))) {
            judge(member, member_place(DECL_TEMPLATE_RESULT(member), class_place), false);
        }
    }
}

// The text of a definition of a class for an entity of a finding, or the
// words "with no definition in the unit".
std::string class_defined_in(tree type) {
    location_t defined_at = definition_of_class(type);
    return defined_at != UNKNOWN_LOCATION ? defined_in(defined_at) : std::string(" with no definition in the unit");
}

// Each member class of the class TYPE, which a different file defines, that
// the header defines out of the class: the finding elsewhere, at the
// definition (THE OUT-OF-LINE ENTITIES).  The rule reads the member classes
// of TYPE, and not the classes inside them.
void judge_foreign_member_classes(tree type) {
    for (tree member = TYPE_FIELDS(type); member != NULL_TREE; member = DECL_CHAIN(member)) {
        tree member_type = member_class_of(member, type);
        if (member_type != NULL_TREE && is_defined_in_header(member_type)) {
            record_site(Kind::definition_elsewhere, definition_of_class(member_type),
                        class_entity(member_type) + ", a member of the class " + class_entity(type)
                            + class_defined_in(type));
        }
    }
}

// One class of a namespace of the base.  A class that the header defines is
// collected.  A class that a different file defines gives the finding of
// each member class that the header defines out of the class.  A class that the header declares and the unit does not define
// waits for THE CLASSES.
void walk_class(tree type) {
    tree main_type = TYPE_MAIN_VARIANT(type);
    tree decl = TYPE_MAIN_DECL(main_type);
    if (decl == NULL_TREE || LAMBDA_TYPE_P(main_type)) {
        return;
    }
    if (!is_header_at(DECL_SOURCE_LOCATION(decl))) {
        if (COMPLETE_TYPE_P(main_type)) {
            judge_foreign_member_classes(main_type);
        }
        return;
    }
    if (COMPLETE_TYPE_P(main_type)) {
        collect_class(main_type);
    } else {
        declared_classes.push_back({decl, DECL_SOURCE_LOCATION(decl)});
    }
}

// One class template of a namespace of the base.  Its template keeps the
// location of the first declaration, and its pattern moves to the definition
// (THE READS).  Each partial and explicit specialization that the header
// defines is collected as a class.  Complexity: linear in the
// specializations of the template.
void walk_class_template(tree template_decl, tree type) {
    for (tree item = DECL_TEMPLATE_SPECIALIZATIONS(template_decl); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree partial = TREE_VALUE(item);
        tree partial_type =
            partial != NULL_TREE && TREE_CODE(partial) == TEMPLATE_DECL ? pattern_class(partial) : NULL_TREE;
        if (partial_type != NULL_TREE && is_defined_in_header(partial_type)) {
            collect_class(partial_type);
        }
    }
    for (tree item = DECL_TEMPLATE_INSTANTIATIONS(template_decl); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree instance = TREE_VALUE(item);
        if (instance != NULL_TREE && CLASS_TYPE_P(instance) && CLASSTYPE_TEMPLATE_SPECIALIZATION(instance)
            && is_defined_in_header(instance)) {
            collect_class(instance);
        }
    }
    if (is_defined_in_header(type)) {
        collect_class(type);
        return;
    }
    if (COMPLETE_TYPE_P(type)) {
        judge_foreign_member_classes(type);
    }
    if (is_header_at(DECL_SOURCE_LOCATION(template_decl))) {
        declared_classes.push_back({template_decl, DECL_SOURCE_LOCATION(template_decl)});
    }
}

// One class of a namespace that the header declares and does not define, and
// that no friend of a class of the header names (THE CLASSES).  A definition
// in a different file of the unit is the finding elsewhere, and no definition
// in the unit is the record declared.
void judge_declared_class(const DeclaredClass& declared) {
    if (!judged.insert(declared.decl).second) {
        return;
    }
    location_t defined_at = definition_of(declared.decl);
    if (defined_at == UNKNOWN_LOCATION) {
        bool is_template = TREE_CODE(declared.decl) == TEMPLATE_DECL;
        tree type = is_template ? pattern_class(declared.decl) : TREE_TYPE(declared.decl);
        record_site(Kind::definition_declared, declared.place, class_entity(type) + (is_template ? " template" : ""));
    } else if (!is_header_at(defined_at)) {
        record_site(Kind::definition_elsewhere, declared.place, qualified_name(declared.decl) + defined_in(defined_at));
    }
}

// True when the class TYPE is a named class of a named namespace, or the
// pattern of a class template of such a namespace, and no specialization: the
// classes of the record defined (THE CLASSES).  An unnamed class and a class
// of an unnamed namespace have no name that a different file can declare.
bool is_namespace_class(tree type) {
    tree decl = TYPE_MAIN_DECL(type);
    return decl != NULL_TREE && TREE_CODE(CP_DECL_CONTEXT(decl)) == NAMESPACE_DECL && !TYPE_UNNAMED_P(type)
        && !LAMBDA_TYPE_P(type) && !decl_anon_ns_mem_p(decl) && !CLASSTYPE_TEMPLATE_SPECIALIZATION(type)
        && !CLASSTYPE_IMPLICIT_INSTANTIATION(type);
}

// True when the function DECL of a namespace is a friend that a class body
// declares with its definition: a hidden friend.  A hidden friend of a class
// template stays a declaration of its own in the namespace until an
// instantiation, with no body, so the friend list of its class gives the
// declaration with the body (judge_friends).
bool is_injected_friend(tree decl) {
    if (TREE_CODE(decl) != FUNCTION_DECL || DECL_LANG_SPECIFIC(decl) == nullptr) {
        return false;
    }
    if (DECL_FRIEND_CONTEXT(decl) != NULL_TREE || DECL_UNIQUE_FRIEND_P(decl)) {
        return true;
    }
    tree info = DECL_TEMPLATE_INFO(decl);
    if (info == NULL_TREE) {
        return false;
    }
    tree template_decl = TI_TEMPLATE(info);
    return template_decl == NULL_TREE || TREE_CODE(template_decl) != TEMPLATE_DECL
        || DECL_TEMPLATE_RESULT(template_decl) != decl;
}

// The phase of the walk at the end: the classes first, so that each friend is
// judged as a friend before each other entity (THE KINDS), and each other
// entity second.
enum class Phase : std::uint8_t {
    classes,
    others,
};

void walk_namespace(tree ns, Phase phase);

// One member of the names of a namespace of the base.
void walk_member(tree decl, tree ns, Phase phase) {
    switch (TREE_CODE(decl)) {
        case OVERLOAD:
            for (ovl_iterator candidate(decl, true); candidate; ++candidate) {
                if (TREE_CODE(*candidate) != OVERLOAD) {
                    walk_member(*candidate, ns, phase);
                }
            }
            return;
        case TREE_LIST:
            return;
        default:
            break;
    }
    if (!DECL_P(decl) || CP_DECL_CONTEXT(decl) != ns) {
        return;
    }
    switch (TREE_CODE(decl)) {
        case NAMESPACE_DECL:
            if (DECL_NAMESPACE_ALIAS(decl) == NULL_TREE) {
                walk_namespace(decl, phase);
            }
            return;
        case TYPE_DECL:
            if (phase == Phase::classes && DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
                walk_class(TREE_TYPE(decl));
            }
            return;
        case TEMPLATE_DECL: {
            tree type = pattern_class(decl);
            if (type != NULL_TREE) {
                if (phase == Phase::classes) {
                    walk_class_template(decl, type);
                }
                return;
            }
            tree result = DECL_TEMPLATE_RESULT(decl);
            if (phase == Phase::others && result != NULL_TREE && TREE_CODE(result) == FUNCTION_DECL
                && !is_no_entity(result) && !is_injected_friend(result) && is_header_at(DECL_SOURCE_LOCATION(decl))) {
                judge(decl, DECL_SOURCE_LOCATION(decl), false);
            }
            return;
        }
        case FUNCTION_DECL:
        case VAR_DECL:
            if (phase == Phase::others && !is_no_entity(decl) && !is_injected_friend(decl)) {
                auto found = declaration_places.find(decl);
                if (found != declaration_places.end()) {
                    judge(decl, found->second, true);
                } else if (is_header_at(DECL_SOURCE_LOCATION(decl))) {
                    judge(decl, DECL_SOURCE_LOCATION(decl), true);
                }
            }
            return;
        default:
            return;
    }
}

// Each declaration of NS and of each namespace inside it, one time for each
// phase.  Complexity: linear in the declarations of the namespaces of the base.
void walk_namespace(tree ns, Phase phase) {
    if (NAMESPACE_LEVEL(ns) == nullptr) {
        return;
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        walk_member(decl, ns, phase);
    }
}

// ── The hooks ───────────────────────────────────────────────────────────

// The place of a declaration at its event: its location when it stands in the
// file that the parser reads, and the place of the parser for a
// redeclaration, whose location stays at the first declaration.
location_t event_place(tree decl) {
    location_t location = DECL_SOURCE_LOCATION(decl);
    return file_at(location) == file_at(input_location) ? location : input_location;
}

// The out-of-line entities that the base source files of the unit define, for
// the record body, and the other functions of the base that they define, in
// the sequence of the parse (THE CLASSES, THE OUT-OF-LINE ENTITIES).
std::vector<tree> source_bodies;
std::vector<tree> source_functions;

// In a unit of a base source file, one definition at PLACE in a base source
// file: the main file or a header of src/foundation/ or src/fixy/.  An
// out-of-line entity is a body, and each other function waits for
// judge_source_definitions.
void note_source_body(tree decl, location_t place) {
    if (!is_base_source(file_at(place)) || !is_base_entity(decl)) {
        return;
    }
    if (is_out_of_line(decl)) {
        source_bodies.push_back(decl);
    } else if (TREE_CODE(decl) == FUNCTION_DECL) {
        source_functions.push_back(decl);
    }
}

// PLUGIN_FINISH_DECL: a declaration of a function with no body, of a static
// data member or of an extern variable, and the definitions of THE READS.
void on_finish_decl(void* gcc_data, void*) {
    Mode current = mode();
    tree decl = static_cast<tree>(gcc_data);
    if (current == Mode::off || decl == NULL_TREE || !VAR_OR_FUNCTION_DECL_P(decl) || current_function_decl != NULL_TREE
        || is_no_entity(decl)) {
        return;
    }
    location_t place = event_place(decl);
    bool is_definition = false;
    if (TREE_CODE(decl) == FUNCTION_DECL) {
        is_definition = is_defined_at_declaration(decl);
    } else if (DECL_CLASS_SCOPE_P(decl)) {
        // The parser reads the initializer of a definition outside the class
        // in the scope of the class, so the scope does not tell a definition.
        // A static data member stays external until its definition.
        is_definition = is_defined_at_declaration(decl) || !DECL_EXTERNAL(decl);
    } else {
        is_definition = !DECL_EXTERNAL(decl) || DECL_INITIAL(decl) != NULL_TREE || is_defined_at_declaration(decl);
    }
    if (current == Mode::source) {
        if (is_definition && VAR_P(decl)) {
            note_source_body(decl, place);
        }
        return;
    }
    if (is_definition) {
        definition_places.emplace(decl, place);
    } else if (is_header_at(place)) {
        declaration_places.emplace(decl, place);
    }
}

// PLUGIN_FINISH_PARSE_FUNCTION: the definition of a function, also of the
// pattern of a function template.  The location is the definition.
void on_finish_function(void* gcc_data, void*) {
    Mode current = mode();
    tree function = static_cast<tree>(gcc_data);
    if (current == Mode::off || function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return;
    }
    if (current == Mode::source) {
        if (!is_no_entity(function)) {
            note_source_body(function, DECL_SOURCE_LOCATION(function));
        }
        return;
    }
    definition_places.emplace(function, DECL_SOURCE_LOCATION(function));
}

// True when the file of PLACE defines the class TYPE: the header and its check
// file for a place in the header, and the same file for a different place.
bool is_defined_beside(tree type, location_t place) {
    if (is_header_at(place)) {
        return is_defined_in_header(type);
    }
    location_t defined_at = definition_of_class(type);
    return defined_at != UNKNOWN_LOCATION && file_at(defined_at) == file_at(place);
}

// The finding of the definition DECL at PLACE when DECL is a member or a
// friend of a class that the file of PLACE does not define (THE OUT-OF-LINE
// ENTITIES).
void judge_foreign_definition(tree decl, location_t place) {
    if (DECL_CLASS_SCOPE_P(decl)) {
        tree owner = TYPE_MAIN_VARIANT(DECL_CONTEXT(decl));
        if (!is_defined_beside(owner, place)) {
            record_site(Kind::definition_elsewhere, place,
                        entity_name(decl) + ", a member of the class " + class_entity(owner) + class_defined_in(owner));
        }
        return;
    }
    if (TREE_CODE(decl) != FUNCTION_DECL || DECL_LANG_SPECIFIC(decl) == nullptr) {
        return;
    }
    for (tree item = DECL_BEFRIENDING_CLASSES(decl); item != NULL_TREE; item = TREE_CHAIN(item)) {
        tree befriender =
            TREE_VALUE(item) != NULL_TREE && TYPE_P(TREE_VALUE(item)) ? TYPE_MAIN_VARIANT(TREE_VALUE(item)) : NULL_TREE;
        if (befriender != NULL_TREE && CLASS_TYPE_P(befriender) && !is_defined_beside(befriender, place)) {
            record_site(Kind::definition_elsewhere, place,
                        entity_name(decl) + ", a friend of the class " + class_entity(befriender)
                            + class_defined_in(befriender));
            return;
        }
    }
}

// Each definition in the header of a member or a friend of a class that the
// header does not define (THE OUT-OF-LINE ENTITIES), at the definition, in
// the sequence of the places.  Complexity: O(d log d) for the d definitions
// of the unit.
void judge_foreign_definitions() {
    std::vector<std::pair<location_t, tree>> found;
    for (const auto& [decl, place] : definition_places) {
        if (is_header_at(place) && !is_no_entity(decl) && is_base_entity(decl)) {
            found.emplace_back(place, decl);
        }
    }
    std::sort(found.begin(), found.end(),
              [](const std::pair<location_t, tree>& left, const std::pair<location_t, tree>& right) {
                  return left.first < right.first;
              });
    for (const auto& [place, decl] : found) {
        judge_foreign_definition(decl, place);
    }
}

// The two namespaces of the base, as the unit declares them.
std::vector<tree> base_roots() {
    std::vector<tree> roots;
    for (const char* root : {"foundation", "fixy"}) {
        tree ns = get_namespace_binding(global_namespace, get_identifier(root));
        if (ns != NULL_TREE && TREE_CODE(ns) == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(ns) == NULL_TREE) {
            roots.push_back(ns);
        }
    }
    return roots;
}

// The record header of the unit, at the first line of the header (THE
// CLASSES).  A unit that does not read the header gives no record, and the
// module of the rule refuses the build.
void record_header() {
    for (const auto& [real, entry] : state.entries) {
        if (entry.file_class == FileClass::base && entry.relative == state.sentinel) {
            Place place;
            place.file_class = FileClass::base;
            place.file = &entry;
            place.line = 1;
            place.column = 1;
            add_finding(Kind::definition_header, nullptr, place, state.sentinel);
            return;
        }
    }
}

// The walk of the header of the argument sentinel (THE KINDS, THE CLASSES).
// Complexity: linear in the declarations of the namespaces of the base and in
// the members and friends of the classes of the header.
void read_header() {
    std::vector<tree> roots = base_roots();
    for (tree ns : roots) {
        walk_namespace(ns, Phase::classes);
    }
    for (tree type : header_classes) {
        judge_friends(type, class_place_of(type));
    }
    for (tree type : header_classes) {
        read_members(type);
    }
    for (const DeclaredClass& declared : declared_classes) {
        judge_declared_class(declared);
    }
    for (tree ns : roots) {
        walk_namespace(ns, Phase::others);
    }
    std::vector<std::pair<location_t, tree>> declared;
    for (const auto& [decl, place] : declaration_places) {
        if (!DECL_CLASS_SCOPE_P(decl) && is_base_entity(decl)) {
            declared.emplace_back(place, decl);
        }
    }
    std::sort(declared.begin(), declared.end(),
              [](const std::pair<location_t, tree>& left, const std::pair<location_t, tree>& right) {
                  return left.first < right.first;
              });
    for (const auto& [place, decl] : declared) {
        judge(decl, place, true);
    }
    judge_foreign_definitions();
    for (tree type : header_classes) {
        if (is_namespace_class(type)) {
            record_site(Kind::definition_defined, class_place_of(type), class_entity(type));
        }
    }
    record_header();
}

// Each class of NS and of each namespace inside it that the base source file
// of the unit defines, as the record defined (THE CLASSES).  A class of an
// unnamed namespace has internal linkage, so no other file defines it.
// Complexity: linear in the declarations of the namespaces of the base.
void record_source_classes(tree ns) {
    if (NAMESPACE_LEVEL(ns) == nullptr) {
        return;
    }
    for (tree decl = NAMESPACE_LEVEL(ns)->names; decl != NULL_TREE; decl = TREE_CHAIN(decl)) {
        if (!DECL_P(decl) || CP_DECL_CONTEXT(decl) != ns) {
            continue;
        }
        tree type = NULL_TREE;
        if (TREE_CODE(decl) == NAMESPACE_DECL && DECL_NAMESPACE_ALIAS(decl) == NULL_TREE && DECL_NAME(decl) != NULL_TREE
            && !decl_anon_ns_mem_p(decl)) {
            record_source_classes(decl);
        } else if (TREE_CODE(decl) == TEMPLATE_DECL) {
            type = pattern_class(decl);
        } else if (TREE_CODE(decl) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(decl) && CLASS_TYPE_P(TREE_TYPE(decl))) {
            type = TREE_TYPE(decl);
        }
        location_t defined_at = type != NULL_TREE ? definition_of_class(type) : UNKNOWN_LOCATION;
        if (defined_at != UNKNOWN_LOCATION && is_base_source(file_at(defined_at))
            && is_namespace_class(TYPE_MAIN_VARIANT(type))) {
            record_site(Kind::definition_defined, defined_at, class_entity(TYPE_MAIN_VARIANT(type)));
        }
    }
}

// The record body of each out-of-line entity that the base source file of
// the unit defines: its symbol and its name (THE CLASSES).
void record_source_bodies() {
    for (tree decl : source_bodies) {
        tree symbol = DECL_ASSEMBLER_NAME(decl);
        if (symbol != NULL_TREE) {
            record_site(Kind::definition_body, DECL_SOURCE_LOCATION(decl),
                        std::string(IDENTIFIER_POINTER(symbol)) + " " + entity_name(decl));
        }
    }
}

}  // namespace

void finish_definition_home() {
    switch (mode()) {
        case Mode::header:
            read_header();
            return;
        case Mode::source:
            for (tree ns : base_roots()) {
                record_source_classes(ns);
            }
            record_source_bodies();
            for (tree function : source_functions) {
                judge_foreign_definition(function, DECL_SOURCE_LOCATION(function));
            }
            return;
        case Mode::off:
            return;
    }
}

void register_definition_home(const char* plugin_name) {
    is_active = true;
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_decl, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
}

}  // namespace crucible_plugin
