// The library entities and the admitted list: the names of a declaration, the
// library of a declaration, the admit row of each use, and the library classes
// that a type holds.  The other sources of the plugin ask these questions for
// each name and each type that they read.
//
// In this file, a file of the language is each file of language_of of
// plugin.h: a file of a language row, and a base file in its sentinel unit,
// which the rule of the library names reads with the admit rows of the
// language (the base kinds of plugin.h).

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The admit row of a qualified name: the row of the name, or of the first
// enclosing name that has a row.  IS_EXACT tells whether the row names the
// name itself.  Returns null when no row admits the name.
const AdmitRow* admit_row_of_name(const std::string& name, bool& is_exact) {
    auto found = state.admitted_names.find(name);
    if (found != state.admitted_names.end()) {
        is_exact = true;
        return found->second;
    }
    is_exact = false;
    for (std::size_t colon = name.find("::"); colon != std::string::npos; colon = name.find("::", colon + 2)) {
        auto enclosing = state.admitted_names.find(name.substr(0, colon));
        if (enclosing != state.admitted_names.end()) {
            return enclosing->second;
        }
    }
    return nullptr;
}

// The parameter count of a function, without `this`, or -1 when DECL is no
// function.
int parameter_count(tree decl) {
    if (decl != NULL_TREE && TREE_CODE(decl) == TEMPLATE_DECL) {
        decl = DECL_TEMPLATE_RESULT(decl);
    }
    if (decl == NULL_TREE || TREE_CODE(decl) != FUNCTION_DECL) {
        return -1;
    }
    int count = 0;
    for (tree parameter = TYPE_ARG_TYPES(TREE_TYPE(decl)); parameter != NULL_TREE && parameter != void_list_node;
         parameter = TREE_CHAIN(parameter)) {
        ++count;
    }
    return DECL_IOBJ_MEMBER_FUNCTION_P(decl) ? count - 1 : count;
}

// Closed tags
//     A class that carries the annotation foundation::core::closed_tag of
//     include/foundation/core/Choice.h holds the tag of a sum: a word that
//     names one alternative of a closed set.  Each class that holds the tag
//     of a sum slot carries it, so each sum of the Choice family holds one,
//     and the rule names no sum.  A bit copy can write a tag that names no
//     alternative.  So the restriction plain-result refuses a result type
//     that holds such a class, as it refuses a bool and an enumeration.  And
//     a bit copy into such a type is the finding forged-tag, an error in each
//     file under the root and in each mode: a call of a function whose admit
//     row has plain-result, such as std::bit_cast, and a __builtin_bit_cast.
//     Three more routes to the bytes are the same finding:
//       a copy of bytes  a call of memcpy, memmove, mempcpy or memset, or of
//                        their checked forms, whose first argument points at
//                        such a type before its conversion to void*.  A copy
//                        from an object of the same type copies a value that
//                        a constructor made, and it is no finding.
//       a view of bytes  a conversion of a pointer to such a type into a
//                        pointer to bytes or to void that can write (the byte
//                        views), with the exceptions that the byte views name.
//       a union member   a member of such a type of a union that has a second
//                        member of bytes, so a write of the second member can
//                        write the tag.  The union of the storage of a sum
//                        carries the annotation closed_tag itself, because the
//                        tag of its slot names its active member, and its
//                        members are no finding.
//     The entity of a finding of these three routes starts with the text of
//     its route, and the entity of a bit copy is the type alone.  The rule
//     reads the bodies that is_body_read_here of plugin.h gives, so a copy
//     whose type a template parameter gives is judged in each instance.  The
//     event PLUGIN_FOLDED_INITIALIZER gives an initializer before the front
//     end folds it.  A pointer of bytes that a parameter, a member or a call
//     gives cannot point into such a type, because the view of bytes refuses
//     each conversion that makes one.  foundation/Lifetime.h refuses a
//     lifetime start over such a type, and the Scalar family refuses a bit
//     copy and a decode into it.  So no copy of bytes writes the tag of a sum
//     outside a region of the class PROBE, and a read of the tag needs no
//     test at run time.

// The bits of the answer of unnamed_bit_parts.
inline constexpr std::uint8_t kBoolOrEnumeration = 1;
inline constexpr std::uint8_t kClosedTag = 2;
inline constexpr std::uint8_t kEachPart = kBoolOrEnumeration | kClosedTag;

// True when the class TYPE carries the annotation closed_tag (the closed
// tags).  Linear in the attributes of the class.
bool carries_closed_tag(tree type) {
    static tree const closed_tag = get_identifier("closed_tag");
    static tree const core = get_identifier("core");
    return carries_base_annotation(TYPE_ATTRIBUTES(type), closed_tag, core);
}

// The parts of TYPE whose bits can name no value: kBoolOrEnumeration for a
// bool or an enumeration, and kClosedTag for a class that carries the
// annotation closed_tag (the closed tags).  The walk reads the type itself,
// an element of an array, a vector or a complex type, and each data member
// and base of a class or a union.  A pointer holds an address, so the walk
// does not follow it, also when its target is dependent.  Each other
// dependent type and an incomplete class give each part, so each use fails
// closed.  The walk keeps the answer of each class, so a class that holds the
// same class in many members costs one visit.  Complexity: O(distinct types
// in the type tree).
std::uint8_t unnamed_bit_parts(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth) {
        return kEachPart;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case OFFSET_TYPE:
            return 0;
        case BOOLEAN_TYPE:
        case ENUMERAL_TYPE:
            return kBoolOrEnumeration;
        case ARRAY_TYPE:
        case VECTOR_TYPE:
        case COMPLEX_TYPE:
            return unnamed_bit_parts(TREE_TYPE(type), depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE: {
            tree main_type = TYPE_MAIN_VARIANT(type);
            if (TYPE_PTRMEMFUNC_P(main_type)) {
                return 0;
            }
            if (uses_template_parms(main_type) || !COMPLETE_TYPE_P(main_type)) {
                return kEachPart;
            }
            auto cached = state.unnamed_bit_parts_of.find(main_type);
            if (cached != state.unnamed_bit_parts_of.end()) {
                return cached->second;
            }
            std::uint8_t parts = carries_closed_tag(main_type) ? kClosedTag : 0;
            for (tree field = TYPE_FIELDS(main_type); field != NULL_TREE && parts != kEachPart;
                 field = DECL_CHAIN(field)) {
                if (TREE_CODE(field) == FIELD_DECL) {
                    parts = static_cast<std::uint8_t>(parts | unnamed_bit_parts(TREE_TYPE(field), depth + 1));
                }
            }
            state.unnamed_bit_parts_of.emplace(main_type, parts);
            return parts;
        }
        default:
            return uses_template_parms(type) ? kEachPart : 0;
    }
}

// True when DECL is a class that only a constant evaluation can hold, or a
// member of one: a class that holds a std::meta::info, such as
// std::vector<std::meta::info>.  The language permits an object of such a
// class only in a consteval context.  The general template of a class is no
// such class, and consteval_only_p says false for a dependent type, so the
// restriction consteval-only fails closed.
bool is_consteval_only_entity(tree decl) {
    tree type = TREE_CODE(decl) == TYPE_DECL && DECL_IMPLICIT_TYPEDEF_P(decl) ? TREE_TYPE(decl) : CP_DECL_CONTEXT(decl);
    return type != NULL_TREE && CLASS_TYPE_P(type) && consteval_only_p(type);
}

// A row admits DECL with its restrictions.  Each path after `in` is a base
// path, so a row with `in` admits nothing in a quarantined file, and admits in
// a file of the language under one of its paths.  RESULT is the result type of
// the call when explicit template arguments give it, or null.  LANGUAGE names
// the file of the language of the use, or is null.
bool row_admits(const AdmitRow& row, tree decl, bool is_exact, Use use, tree result, const FileEntry* language) {
    if (!row.in_paths.empty()) {
        bool is_held = false;
        for (const std::string& in_path : row.in_paths) {
            is_held = is_held || (language != nullptr && row_holds(in_path, language->relative));
        }
        if (!is_held) {
            return false;
        }
    }
    if (row.arity >= 0 && parameter_count(decl) != row.arity) {
        return false;
    }
    if (row.is_parameters_only && is_exact && use != Use::parameter) {
        return false;
    }
    if (row.is_consteval_only && !is_consteval_only_entity(decl)) {
        return false;
    }
    if (row.is_plain_result) {
        tree function = TREE_CODE(decl) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(decl) : decl;
        if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
            return false;
        }
        tree type = result != NULL_TREE ? result : TREE_TYPE(TREE_TYPE(function));
        if (unnamed_bit_parts(type, 0) != 0) {
            return false;
        }
    }
    return !row.is_concepts_only || concept_definition_p(decl);
}

// Friends of a class
//     A friend function that a class defines, such as operator< of
//     std::strong_ordering, is a function of the namespace of the class, so
//     its qualified name is std::operator<.  A row of that name admits the
//     operator< of each class and each function template of the namespace.
//     A row admits such a friend through the name of its class and its own
//     name, std::strong_ordering::operator<, and only when the row names it
//     exactly: the row of the class does not admit its friends, and neither
//     does a row of an enclosing name.

// The name of DECL as a friend of the class that defines it, such as
// std::strong_ordering::operator<, or an empty text when DECL is no friend
// function that a class defines (the friends of a class).
std::string friend_name(tree decl) {
    tree function = TREE_CODE(decl) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(decl) : decl;
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_LANG_SPECIFIC(function) == nullptr
        || !DECL_NAMESPACE_SCOPE_P(function)) {
        return {};
    }
    tree befriending = DECL_FRIEND_CONTEXT(function);
    return befriending != NULL_TREE && CLASS_TYPE_P(befriending)
             ? qualified_name(befriending) + "::" + simple_name(function)
             : std::string{};
}

// In a file of the language, a row with `until` admits only with `in`: the
// base does not use a name that one of its families replaces, except in the
// files that the row names, such as the door of the family.
const AdmitRow* find_admitting_row(tree decl, Use use, tree result, const FileEntry* language) {
    bool is_exact = false;
    const AdmitRow* row = admit_row_of_name(qualified_name(decl), is_exact);
    const AdmitRow* found = nullptr;
    if (row != nullptr && row_admits(*row, decl, is_exact, use, result, language)) {
        found = row;
    } else {
        const char* file = DECL_SOURCE_FILE(decl);
        auto header = file != nullptr ? state.admitted_header_paths.find(classify_file(file).real)
                                      : state.admitted_header_paths.end();
        if (header != state.admitted_header_paths.end()
            && row_admits(*header->second, decl, false, use, result, language)) {
            found = header->second;
        }
    }
    if (found == nullptr) {
        std::string entry_of_friend = friend_name(decl);
        auto exact = entry_of_friend.empty() ? state.admitted_names.end() : state.admitted_names.find(entry_of_friend);
        if (exact != state.admitted_names.end() && row_admits(*exact->second, decl, true, use, result, language)) {
            found = exact->second;
        }
    }
    bool is_replaced_name =
        language != nullptr && found != nullptr && !found->pending_kind.empty() && found->in_paths.empty();
    return is_replaced_name ? nullptr : found;
}

// The most general template of a class template specialization, or null.
tree general_template_of(tree class_type) {
    if (!CLASS_TYPE_P(class_type) || CLASSTYPE_TEMPLATE_INFO(class_type) == NULL_TREE) {
        return NULL_TREE;
    }
    tree template_decl = CLASSTYPE_TI_TEMPLATE(class_type);
    return template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL
             ? most_general_template(template_decl)
             : NULL_TREE;
}

// A default that depends on an earlier parameter, such as std::allocator<T>,
// matches each specialization of the same template.
bool matches_default(tree argument, tree default_argument) {
    if (TYPE_P(argument) && TYPE_P(default_argument)) {
        if (!uses_template_parms(default_argument)) {
            return same_type_p(argument, default_argument);
        }
        tree argument_template = general_template_of(TYPE_MAIN_VARIANT(argument));
        return argument_template != NULL_TREE
            && argument_template == general_template_of(TYPE_MAIN_VARIANT(default_argument));
    }
    if (TREE_CODE(argument) == TEMPLATE_DECL && TREE_CODE(default_argument) == TEMPLATE_DECL) {
        return most_general_template(argument) == most_general_template(default_argument);
    }
    return false;
}

// The template arguments of a class that is not a library class.  An
// argument equal to the default of its parameter is skipped: the declaration
// did not write it, so a fixy default such as the storage of AppendOnly is not
// a finding at each object.
LibraryClasses library_class_in_template_arguments(tree class_type, int depth, const FileEntry* language) {
    LibraryClasses found;
    tree info = CLASSTYPE_TEMPLATE_INFO(class_type);
    if (TI_ARGS(info) == NULL_TREE) {
        return found;
    }
    tree arguments = INNERMOST_TEMPLATE_ARGS(TI_ARGS(info));
    tree parameters = NULL_TREE;
    tree template_decl = TI_TEMPLATE(info);
    if (template_decl != NULL_TREE && TREE_CODE(template_decl) == TEMPLATE_DECL) {
        tree general = most_general_template(template_decl);
        if (general != NULL_TREE && TREE_CODE(general) == TEMPLATE_DECL) {
            parameters = INNERMOST_TEMPLATE_PARMS(DECL_TEMPLATE_PARMS(general));
        }
    }
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC) {
        return found;
    }
    for (int index = 0; index < TREE_VEC_LENGTH(arguments) && !found.is_full(); ++index) {
        tree argument = TREE_VEC_ELT(arguments, index);
        if (parameters != NULL_TREE && index < TREE_VEC_LENGTH(parameters)) {
            tree parameter = TREE_VEC_ELT(parameters, index);
            if (parameter != NULL_TREE && parameter != error_mark_node && TREE_PURPOSE(parameter) != NULL_TREE
                && matches_default(argument, TREE_PURPOSE(parameter))) {
                continue;
            }
        }
        found.add(library_class_in_argument(argument, depth, language));
    }
    return found;
}

// The library classes that the function type TYPE names: in its return type
// and in each parameter type, also the class of a member function.  A
// function type makes no object, so a refused class of it is a name of the
// declaration and no object.
LibraryClasses library_class_in_function_type(tree type, int depth, const FileEntry* language) {
    LibraryClasses found = library_class_in(TREE_TYPE(type), depth + 1, Use::other, language);
    for (tree parameter = TYPE_ARG_TYPES(type); parameter != NULL_TREE && parameter != void_list_node;
         parameter = TREE_CHAIN(parameter)) {
        found.add(library_class_in(TREE_VALUE(parameter), depth + 1, Use::parameter, language));
    }
    found.is_refused_name = found.refused != NULL_TREE;
    return found;
}

}  // namespace

// ── Names ───────────────────────────────────────────────────────────────

std::string identifier_text(tree identifier) {
    return identifier != NULL_TREE ? std::string(IDENTIFIER_POINTER(identifier)) : std::string{};
}

std::string simple_name(tree decl) {
    tree name = DECL_NAME(decl);
    if (TREE_CODE(decl) == NAMESPACE_DECL) {
        return name != NULL_TREE ? identifier_text(name) : std::string("(anonymous namespace)");
    }
    if (name == NULL_TREE || TREE_CODE(name) != IDENTIFIER_NODE || IDENTIFIER_ANON_P(name)) {
        return "(unnamed)";
    }
    if (IDENTIFIER_LAMBDA_P(name)) {
        return "(lambda)";
    }
    if (IDENTIFIER_CONV_OP_P(name)) {
        return "operator (conversion)";
    }
    return identifier_text(name);
}

// The qualified name of a declaration, without template arguments and
// without inline namespaces: std::__cxx11::basic_string<char> gives
// std::basic_string.
std::string qualified_name(tree decl) {
    std::vector<std::string> parts;
    tree current = decl;
    while (current != NULL_TREE && current != global_namespace && parts.size() < 32) {
        if (TYPE_P(current)) {
            tree main_type = TYPE_MAIN_VARIANT(current);
            tree type_decl = TYPE_NAME(main_type);
            if (type_decl == NULL_TREE || TREE_CODE(type_decl) != TYPE_DECL) {
                parts.emplace_back("(unnamed)");
                break;
            }
            parts.push_back(LAMBDA_TYPE_P(main_type) ? std::string("(lambda)") : simple_name(type_decl));
            current = CP_TYPE_CONTEXT(main_type);
        } else if (TREE_CODE(current) == NAMESPACE_DECL) {
            if (!DECL_NAMESPACE_INLINE_P(current)) {
                parts.push_back(simple_name(current));
            }
            current = CP_DECL_CONTEXT(current);
        } else if (DECL_P(current)) {
            parts.push_back(simple_name(current));
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    std::string joined;
    for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
        if (!joined.empty()) {
            joined += "::";
        }
        joined += *part;
    }
    return joined;
}

// ── The printed text of a type or a declaration ─────────────────────────
//
// Text of a key
//     A key of a ledger holds the text that the plugin prints from a type or
//     a declaration, such as the parameter types of a function.  Each unit
//     must print the same text for one type, so the printer has two rules:
//       - GCC prints a default template argument only when the first
//         specialization of the unit with those arguments named it.  One
//         header gave std::span<unsigned char> in one unit and
//         std::span<unsigned char, 18446744073709551615> in a different unit.
//         The flag of kPrintFlags makes GCC print each template argument.
//       - GCC prints a template parameter with the name that its declaration
//         spells.  A dependent specialization, such as Box<T>, holds the
//         parameters of the first template of the unit that made it.  So one
//         declaration printed Box<Other> in one unit and Box<T> in a different
//         unit, and one text could name two different parameters with one
//         name.  While it prints a text, the printer gives each template
//         parameter of the text the name T<DEPTH>_<POSITION> (ParameterNames).
//     DEPTH is the level of the template of the parameter, from 0 for the
//     outermost template.  POSITION is the index of the parameter in the
//     list of that template, from 0.  GCC compares two template parameters by
//     these two numbers, so the name of a parameter does not change with the
//     sequence of the specializations of a unit.  The entity of an object
//     finding holds no printed type at all (declaration.cpp, the entity of an
//     object).

namespace {

constexpr int kPrintFlags = TFF_NO_OMIT_DEFAULT_TEMPLATE_ARGUMENTS;

// The template parameters that the print of ROOT can name, with the name
// T<DEPTH>_<POSITION> while the object lives (the text of a key).  The walk
// reads the nodes that the printer of GCC reads: the template arguments of
// each class and alias, the parameter list of a class template whose pattern
// the text names, the enclosing class of each type, the types of a function
// and of a declaration, and each expression, such as the operand of a
// decltype.  The destructor gives each declaration its own name again.  A
// print makes no garbage collection, so each tree stays at its address.
// Complexity: O(nodes of the type tree of ROOT), each node one time.
class ParameterNames {
public:
    explicit ParameterNames(tree root) { walk(root); }

    // The destructor gives each name back one time, so no copy holds the
    // names.  The plugin compiles as C++17, with no reason text on a delete.
    ParameterNames(const ParameterNames&) = delete;
    ParameterNames& operator=(const ParameterNames&) = delete;

    ~ParameterNames() {
        for (auto renamed = names_.rbegin(); renamed != names_.rend(); ++renamed) {
            DECL_NAME(renamed->first) = renamed->second;
        }
    }

private:
    void walk(tree node) {
        if (node != NULL_TREE) {
            cp_walk_tree(&node, visit, this, &visited_);
        }
    }

    // Gives DECL, a declaration of a template parameter, the name of the
    // depth and the position of PARAMETER: a TEMPLATE_TYPE_PARM, a
    // TEMPLATE_TEMPLATE_PARM, a BOUND_TEMPLATE_TEMPLATE_PARM or a
    // TEMPLATE_PARM_INDEX.  A placeholder such as auto is no parameter that
    // the code spells.
    void rename(tree decl, tree parameter) {
        if (decl == NULL_TREE || !DECL_P(decl) || DECL_NAME(decl) == NULL_TREE || parameter == NULL_TREE) {
            return;
        }
        bool is_index = TREE_CODE(parameter) == TEMPLATE_PARM_INDEX;
        bool is_type = TREE_CODE(parameter) == TEMPLATE_TYPE_PARM || TREE_CODE(parameter) == TEMPLATE_TEMPLATE_PARM
                    || TREE_CODE(parameter) == BOUND_TEMPLATE_TEMPLATE_PARM;
        if ((!is_index && !is_type) || (is_type && is_auto(parameter)) || !renamed_.insert(decl).second) {
            return;
        }
        int level = is_index ? TEMPLATE_PARM_LEVEL(parameter) : TEMPLATE_TYPE_LEVEL(parameter);
        int position = is_index ? TEMPLATE_PARM_IDX(parameter) : TEMPLATE_TYPE_IDX(parameter);
        std::string name = "T" + std::to_string(level - 1) + "_" + std::to_string(position);
        names_.emplace_back(decl, DECL_NAME(decl));
        DECL_NAME(decl) = get_identifier(name.c_str());
    }

    // A non-type parameter has two declarations: the CONST_DECL of its
    // index, which an expression prints, and the PARM_DECL of the parameter
    // list, which the print of a class template pattern names.
    void rename_parameter(tree parameter) {
        tree decl = TREE_CODE(parameter) == TEMPLATE_PARM_INDEX ? TEMPLATE_PARM_DECL(parameter) : TYPE_NAME(parameter);
        rename(decl, parameter);
    }

    // The printer names a class template pattern, such as the scope of a
    // member typedef, with the declarations of the innermost parameter list
    // of the template, and not with its template arguments.
    void rename_pattern_parameters(tree type) {
        tree info = CLASSTYPE_TEMPLATE_INFO(type);
        if (info == NULL_TREE || CLASSTYPE_USE_TEMPLATE(type) != 0 || TREE_CODE(TI_TEMPLATE(info)) != TEMPLATE_DECL) {
            return;
        }
        tree levels = DECL_TEMPLATE_PARMS(TI_TEMPLATE(info));
        tree parameters = levels != NULL_TREE && TREE_CODE(levels) == TREE_LIST ? TREE_VALUE(levels) : NULL_TREE;
        for (int index = 0; parameters != NULL_TREE && index < TREE_VEC_LENGTH(parameters); ++index) {
            tree entry = TREE_VEC_ELT(parameters, index);
            tree decl = entry != NULL_TREE && entry != error_mark_node ? TREE_VALUE(entry) : NULL_TREE;
            if (decl == NULL_TREE || !DECL_P(decl)) {
                continue;
            }
            bool is_value = TREE_CODE(decl) == PARM_DECL || TREE_CODE(decl) == CONST_DECL;
            rename(decl, is_value ? DECL_INITIAL(decl) : TREE_TYPE(decl));
        }
    }

    // cp_walk_tree reads the template arguments of a type, the types of a
    // function type and the operands of an expression.  The visit adds the
    // other nodes that the printer reads.
    static tree visit(tree* node, int*, void* data) {
        auto& names = *static_cast<ParameterNames*>(data);
        tree current = *node;
        if (TYPE_P(current)) {
            names.walk(TYPE_CONTEXT(current));
            // The printer names a typedef with the scope of its declaration,
            // such as the class template of a member typedef.  The type of
            // the typedef keeps the scope of the type that it names.
            if (tree name = TYPE_NAME(current); name != NULL_TREE && TREE_CODE(name) == TYPE_DECL) {
                names.walk(DECL_CONTEXT(name));
            }
        }
        switch (TREE_CODE(current)) {
            case TEMPLATE_TYPE_PARM:
            case TEMPLATE_TEMPLATE_PARM:
            case BOUND_TEMPLATE_TEMPLATE_PARM:
            case TEMPLATE_PARM_INDEX:
                names.rename_parameter(current);
                break;
            case RECORD_TYPE:
            case UNION_TYPE:
                if (CLASS_TYPE_P(current)) {
                    names.rename_pattern_parameters(current);
                }
                break;
            case FUNCTION_TYPE:
            case METHOD_TYPE:
                if (tree specification = TYPE_RAISES_EXCEPTIONS(current)) {
                    names.walk(TREE_PURPOSE(specification));
                }
                break;
            // The argument of a template template parameter.
            case TEMPLATE_DECL:
                if (DECL_TEMPLATE_TEMPLATE_PARM_P(current)) {
                    names.walk(TREE_TYPE(current));
                }
                break;
            case FUNCTION_DECL:
            case VAR_DECL:
                names.walk(TREE_TYPE(current));
                names.walk(DECL_CONTEXT(current));
                if (DECL_LANG_SPECIFIC(current) != nullptr && DECL_TEMPLATE_INFO(current) != NULL_TREE) {
                    names.walk(DECL_TI_ARGS(current));
                }
                break;
            default:
                break;
        }
        return NULL_TREE;
    }

    hash_set<tree> visited_;
    std::unordered_set<tree> renamed_;
    std::vector<std::pair<tree, tree>> names_;  // each renamed declaration and its own name
};

}  // namespace

std::string type_text(tree type) {
    ParameterNames names(type);
    const char* printed = type_as_string(type, kPrintFlags);
    return printed != nullptr ? std::string(printed) : std::string("(type)");
}

std::string declaration_text(tree decl) {
    ParameterNames names(decl);
    const char* printed = decl_as_string(decl, kPrintFlags | TFF_NO_TEMPLATE_BINDINGS);
    return printed != nullptr ? std::string(printed) : qualified_name(decl);
}

// ── The library and the admitted list ───────────────────────────────────

// The namespace directly below the global namespace that holds DECL, or the
// global namespace itself.
tree top_namespace(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        if (TREE_CODE(current) == NAMESPACE_DECL) {
            tree context = CP_DECL_CONTEXT(current);
            if (context == global_namespace) {
                return current;
            }
            current = context;
        } else if (TYPE_P(current)) {
            current = CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current));
        } else if (DECL_P(current)) {
            current = CP_DECL_CONTEXT(current);
        } else {
            break;
        }
    }
    return global_namespace;
}

// A declaration of namespace std is standard, wherever it is.  Any other
// declaration of a library file (in_library_file_at), a header outside the
// root, is standard when a namespace holds it, as __gnu_cxx does, and part of
// the C library when it is global.  A header of a third-party library is a
// library file too, also when no system flag marks it.
Library library_of(tree decl) {
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return Library::none;
    }
    tree top = top_namespace(decl);
    if (top == std_node) {
        return Library::standard;
    }
    if (!in_library_file_at(DECL_SOURCE_LOCATION(decl))) {
        return Library::none;
    }
    return top == global_namespace ? Library::c_library : Library::standard;
}

// The result type of a call of the function template FUNCTION with the
// explicit template ARGUMENTS of a template-id, such as std::bit_cast<To>(x)
// in a template.  It is known when the result type is one template parameter
// of the innermost level and an explicit argument gives that parameter.
// Returns null otherwise.
tree result_of_explicit_arguments(tree function, tree arguments) {
    if (arguments == NULL_TREE || TREE_CODE(arguments) != TREE_VEC || DECL_LANG_SPECIFIC(function) == nullptr
        || DECL_TEMPLATE_INFO(function) == NULL_TREE) {
        return NULL_TREE;
    }
    tree result = TREE_TYPE(TREE_TYPE(function));
    tree template_decl = DECL_TI_TEMPLATE(function);
    if (result == NULL_TREE || TREE_CODE(result) != TEMPLATE_TYPE_PARM || template_decl == NULL_TREE
        || TREE_CODE(template_decl) != TEMPLATE_DECL
        || TEMPLATE_TYPE_LEVEL(result) != TMPL_PARMS_DEPTH(DECL_TEMPLATE_PARMS(template_decl))
        || TEMPLATE_TYPE_IDX(result) >= TREE_VEC_LENGTH(arguments)) {
        return NULL_TREE;
    }
    tree argument = TREE_VEC_ELT(arguments, TEMPLATE_TYPE_IDX(result));
    return argument != NULL_TREE && TYPE_P(argument) ? argument : NULL_TREE;
}

// The row that admits a dependent qualified name, which names no declaration
// yet, or null.  A row with `consteval-only` needs the declaration, so it
// refuses the name, and the other restrictions of a declaration do not apply.
// A row with `in` admits only in a file of the language under one of its
// paths.  LANGUAGE names the file of the language of the use, or is null.  In
// a file of the language, a row with `until` admits only with `in`.
const AdmitRow* admitting_row_of_name(const std::string& name, const FileEntry* language) {
    bool is_exact = false;
    const AdmitRow* row = admit_row_of_name(name, is_exact);
    if (row == nullptr || row->is_consteval_only) {
        return nullptr;
    }
    bool is_held = row->in_paths.empty();
    for (const std::string& in_path : row->in_paths) {
        is_held = is_held || (language != nullptr && row_holds(in_path, language->relative));
    }
    bool is_replaced_name = language != nullptr && !row->pending_kind.empty() && row->in_paths.empty();
    return is_held && !is_replaced_name ? row : nullptr;
}

// The row that admits DECL, or null when no row admits it.  A name entry
// admits the entity that it names and each name nested in it, so
// std::tuple_size admits std::tuple_size<T>::value.  It admits no other entity
// whose name starts with the same text: std::tuple_size_v and
// std::tuple_element_t need their own entries.  A name entry admits a friend
// function that a class defines only when it names the class and the friend
// exactly, such as std::strong_ordering::operator< (the friends of a class).
// A header entry admits each entity that the header itself declares: the
// file that an #include of that exact name enters, and not a header of the
// same last name in a different directory, such as experimental/type_traits.
// A restriction of the row narrows what it admits (row_admits).  RESULT is the
// result type that the explicit template arguments of a call give, or null.
// LANGUAGE names the file of the language of the use, or is null.  The cache
// holds the answers of a quarantined file only.
const AdmitRow* admitting_row(tree decl, Use use, tree result, const FileEntry* language) {
    if (result != NULL_TREE || language != nullptr) {
        return find_admitting_row(decl, use, result, language);
    }
    std::unordered_map<tree, const AdmitRow*>& cache = state.admitting_row_of[static_cast<int>(use)];
    auto cached = cache.find(decl);
    if (cached != cache.end()) {
        return cached->second;
    }
    const AdmitRow* row = find_admitting_row(decl, use, NULL_TREE, nullptr);
    cache.emplace(decl, row);
    return row;
}

bool is_plainly_admitted(const AdmitRow* row) { return row != nullptr && row->pending_kind.empty(); }

// The finding of a use of the standard library entity DECL at LOCATION, as the
// admitted list decides it: none when a row admits it, the kind of the row
// when a row with `until` admits it, and std-entity when no row admits it.
// LANGUAGE names the file of the language of a declaration, or is null.  When
// it is null and no row plainly admits DECL, a use in a file of the language
// takes the answer of that file: a row with `in` admits there, and a row with
// `until` refuses there unless it has `in`.  Returns true when it made a
// finding.
bool record_standard_use(tree decl, location_t location, const std::string& entity, Use use, tree result,
                         const FileEntry* language) {
    const AdmitRow* row = admitting_row(decl, use, result, language);
    if (language == nullptr && !is_plainly_admitted(row)) {
        language = language_file_of(location);
        if (language != nullptr) {
            row = admitting_row(decl, use, result, language);
        }
    }
    if (row == nullptr) {
        record_declared(Kind::std_entity, location, entity, language);
        return true;
    }
    if (row->pending_kind.empty()) {
        return false;
    }
    record_pending(*row, location, entity);
    return true;
}

// ── The library class inside a type ─────────────────────────────────────

// LANGUAGE names the file of the language of the declaration, or is null.
LibraryClasses library_class_in_argument(tree argument, int depth, const FileEntry* language) {
    LibraryClasses found;
    if (argument == NULL_TREE || argument == error_mark_node) {
        return found;
    }
    if (ARGUMENT_PACK_P(argument)) {
        tree arguments = ARGUMENT_PACK_ARGS(argument);
        for (int index = 0; index < TREE_VEC_LENGTH(arguments) && !found.is_full(); ++index) {
            found.add(library_class_in_argument(TREE_VEC_ELT(arguments, index), depth + 1, language));
        }
        return found;
    }
    if (TYPE_P(argument)) {
        return library_class_in(argument, depth + 1, Use::other, language);
    }
    tree result = TREE_CODE(argument) == TEMPLATE_DECL ? DECL_TEMPLATE_RESULT(argument) : NULL_TREE;
    if (result == NULL_TREE || TREE_CODE(result) != TYPE_DECL || library_of(argument) == Library::none) {
        return found;
    }
    const AdmitRow* row = admitting_row(argument, Use::other, NULL_TREE, language);
    if (row == nullptr) {
        found.refused = TREE_TYPE(result);
    } else if (!row->pending_kind.empty()) {
        found.pending = TREE_TYPE(result);
        found.pending_row = row;
    }
    return found;
}

// The first class or enumeration of the standard library or of a C header
// that TYPE holds, and that no admit row admits, and the first one that a row
// with `until` admits.  The walk looks through typedefs, pointers, references,
// arrays, function types, template arguments and the enclosing class, and
// stops at a library class or enumeration that no row admits.  So a typedef of
// fixy that names std::memory_order gives an object of a library type.  A
// class that a function type names is a name and not an object: a callback
// that takes a std::string holds no std::string.  A row admits a library class
// and not its template arguments, so std::initializer_list<std::string> holds
// a refused std::string.  USE applies to the class at the outer level, through
// pointers and references.  A template argument and an enclosing class are
// other uses.  LANGUAGE names the file of the language of the declaration, or
// is null.  The caches keep the answer of each type for each use, and of a
// file of the language apart.  A type nested deeper than kTypeDepth is
// refused, so the bound hides no library class.
LibraryClasses library_class_in(tree type, int depth, Use use, const FileEntry* language) {
    LibraryClasses found;
    if (type == NULL_TREE || type == error_mark_node) {
        return found;
    }
    if (depth > kTypeDepth) {
        found.refused = type;
        return found;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case REFERENCE_TYPE:
            return library_class_in(TREE_TYPE(type), depth + 1, use, language);
        case FUNCTION_TYPE:
        case METHOD_TYPE:
            return library_class_in_function_type(type, depth, language);
        case ARRAY_TYPE:
            return library_class_in(TREE_TYPE(type), depth + 1, use, language);
        case TYPENAME_TYPE:
            return library_class_in(TYPE_CONTEXT(type), depth + 1, Use::other, language);
        case TYPE_PACK_EXPANSION:
            return library_class_in(PACK_EXPANSION_PATTERN(type), depth + 1, use, language);
        case RECORD_TYPE:
        case UNION_TYPE:
        case ENUMERAL_TYPE:
            break;
        default:
            return found;
    }
    if (TYPE_PTRMEMFUNC_P(type)) {
        return library_class_in(TYPE_PTRMEMFUNC_FN_TYPE(type), depth + 1, use, language);
    }
    tree main_type = TYPE_MAIN_VARIANT(type);
    std::unordered_map<tree, LibraryClasses>& cache = language == nullptr
                                                        ? state.library_class_of[static_cast<int>(use)]
                                                        : state.language_class_of[static_cast<int>(use)][language];
    auto cached = cache.find(main_type);
    if (cached != cache.end()) {
        return cached->second;
    }
    tree decl = TYPE_MAIN_DECL(main_type);
    if (decl != NULL_TREE && !LAMBDA_TYPE_P(main_type)) {
        if (library_of(decl) != Library::none) {
            const AdmitRow* row = admitting_row(decl, use, NULL_TREE, language);
            if (row == nullptr) {
                found.refused = main_type;
            } else {
                if (!row->pending_kind.empty()) {
                    found.pending = main_type;
                    found.pending_row = row;
                }
                if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
                    found.add(library_class_in_template_arguments(main_type, depth, language));
                }
            }
        } else {
            if (CLASS_TYPE_P(main_type) && CLASSTYPE_TEMPLATE_INFO(main_type) != NULL_TREE) {
                found = library_class_in_template_arguments(main_type, depth, language);
            }
            tree context = CP_TYPE_CONTEXT(main_type);
            if (!found.is_full() && context != NULL_TREE && TYPE_P(context)) {
                found.add(library_class_in(context, depth + 1, Use::other, language));
            }
        }
    }
    cache.emplace(main_type, found);
    return found;
}

// The library name that TYPE spells at its outer level, when the type is not
// a class or an enumeration: a typedef or an alias template of namespace std,
// such as std::size_t or std::tuple_element_t.  The walk looks through
// pointers, references and arrays, and it stops at a typedef that is not of
// the library: a project alias of std::size_t names no library entity where
// the code uses it, because the type itself is a fundamental type.  Returns
// the declaration of the alias template or of the typedef, or null when a row
// without `until` admits it.  A template argument loses its typedef, so a
// typedef inside a template argument is no finding.  library_class_in finds an
// enumeration.  LANGUAGE names the file of the language of the declaration,
// or is null.
tree library_type_name_in(tree type, int depth, const FileEntry* language) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth) {
        return NULL_TREE;
    }
    if (TREE_CODE(type) == POINTER_TYPE || TREE_CODE(type) == REFERENCE_TYPE) {
        return FUNC_OR_METHOD_TYPE_P(TREE_TYPE(type)) ? NULL_TREE
                                                      : library_type_name_in(TREE_TYPE(type), depth + 1, language);
    }
    if (TREE_CODE(type) == ARRAY_TYPE) {
        return library_type_name_in(TREE_TYPE(type), depth + 1, language);
    }
    tree name = TYPE_NAME(type);
    if (name != NULL_TREE && TREE_CODE(name) == TYPE_DECL && DECL_ORIGINAL_TYPE(name) != NULL_TREE) {
        tree decl = name;
        if (tree info = TYPE_ALIAS_TEMPLATE_INFO(type); info != NULL_TREE && TI_TEMPLATE(info) != NULL_TREE) {
            decl = TI_TEMPLATE(info);
        }
        bool is_admitted = is_plainly_admitted(admitting_row(decl, Use::other, NULL_TREE, language));
        return library_of(decl) == Library::standard && !is_admitted ? decl : NULL_TREE;
    }
    return NULL_TREE;
}

bool is_standard_class(tree type) { return type != NULL_TREE && library_of(TYPE_MAIN_DECL(type)) == Library::standard; }

// The finding of a class that CLASSES gives as pending, at LOCATION.  SUFFIX
// follows the name of the class in the entity, such as the member of a
// qualified name.
void record_pending_class(const LibraryClasses& classes, location_t location, const std::string& suffix) {
    if (classes.pending != NULL_TREE && classes.pending_row != nullptr) {
        record_pending(*classes.pending_row, location, qualified_name(classes.pending) + suffix);
    }
}

// The library classes of TYPE for a use at LOCATION.  The answer of a
// quarantined file comes first, because the cache holds it, and a type with no
// refused and no pending class there has none in a file of the language
// either.  Otherwise a use in a file of the language takes the answer of that
// file, whose admit rows differ (row_admits), and LANGUAGE names that file.
LibraryClasses library_classes_at(tree type, location_t location, Use use, const FileEntry*& language) {
    LibraryClasses classes = library_class_in(type, 0, use);
    if (classes.refused == NULL_TREE && classes.pending == NULL_TREE) {
        return classes;
    }
    language = language_file_of(location);
    return language != nullptr ? library_class_in(type, 0, use, language) : classes;
}

// The library entities that TYPE names, where the source spells TYPE: a class
// of namespace std inside it, or else the library name at its outer level,
// and each class that a row with `until` admits.  LANGUAGE names the file of
// the language of a declaration, or is null, and then the file that holds
// LOCATION decides.
void record_named_entities(tree type, location_t location, const FileEntry* language) {
    note_walked_type(type, location, false);
    LibraryClasses classes = language != nullptr ? library_class_in(type, 0, Use::other, language)
                                                 : library_classes_at(type, location, Use::other, language);
    if (classes.refused != NULL_TREE) {
        if (is_standard_class(classes.refused)) {
            record_library(Kind::std_entity, location, qualified_name(TYPE_MAIN_DECL(classes.refused)));
        }
    } else if (tree named = library_type_name_in(type, 0, language)) {
        record_standard_use(named, location, qualified_name(named), Use::other, NULL_TREE, language);
    }
    record_pending_class(classes, location);
}

namespace {

// True when FUNCTION is a bit copy of the closed tags: a function whose admit
// row has plain-result, such as std::bit_cast.
bool is_bit_copy_function(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return false;
    }
    bool is_exact = false;
    const AdmitRow* row = admit_row_of_name(qualified_name(function), is_exact);
    return row != nullptr && row->is_plain_result;
}

// The texts that start the entity of a copy of bytes, of a view of bytes and
// of a union member (the closed tags).  report_forged_tag reads them.
constexpr const char* kByteCopyRoute = "copy of bytes into ";
constexpr const char* kByteViewRoute = "view of bytes of ";
constexpr const char* kUnionMemberRoute = "union member ";

// Byte views
//     A pointer to bytes or to void that can write is a door to the bytes of
//     the object that it points into: a copy of bytes, read(), recv() and each
//     other door that takes such a pointer write through it.  So a conversion
//     of a pointer to a type that holds a closed tag into a pointer to bytes
//     or to void that is not const is the finding view of bytes.  The type is
//     the target of the pointer before each conversion of the expression, so
//     a const round trip in one expression is a view too.  A bit copy of such
//     a pointer into a pointer to bytes is the same finding.  Two kinds of
//     such conversions are no finding:
//       a copy of bytes  the destination of a call of memcpy, memmove,
//                        mempcpy or memset in the same expression, which the
//                        route of a copy of bytes judges
//       a language door  an argument of a door of the language that starts,
//                        frees or ends an object: the place of a placement
//                        new that builds the type of the place, whose
//                        constructor writes the object, the pointer of an
//                        operator delete, which frees the storage of an
//                        ended object, and the object of the registration
//                        of the end of a static or a thread local object.
//                        The front end also builds such calls itself, in
//                        the cleanup of a new-expression and in the first
//                        run of the declaration of a static local.  Two
//                        marks of foundation/ByteSeal.h on a function of a
//                        base file name the doors of the base of the same
//                        kind: each argument of a call of a function with
//                        the mark free_door, which frees the storage of an
//                        ended object as an operator delete does, and the
//                        conversion of a parameter in the body of a function
//                        with the mark erasure_door, whose erased address
//                        keeps the pointer private.
//     A conversion to a pointer to const bytes only reads the bytes, so it is
//     no finding.  So no pointer that can write the bytes of a sum exists
//     outside these forms, and a write through a pointer of bytes that a
//     parameter, a member or a call gives writes no sum.

// The conversions of the body that walk_bit_copies reads that are no view of
// bytes (the byte views): the conversions of the destination of a copy of
// bytes and of each argument of a language door.  The walk visits a call
// before its arguments, so the call marks them first.
std::unordered_set<tree> exempt_conversions;

// True when TYPE is a type of bytes: void, a character type or std::byte.  A
// pointer to such a type names no object type.
bool is_byte_type(tree type) {
    if (type == NULL_TREE) {
        return false;
    }
    type = TYPE_MAIN_VARIANT(type);
    if (VOID_TYPE_P(type) || type == char_type_node || type == signed_char_type_node || type == unsigned_char_type_node
        || type == char8_type_node) {
        return true;
    }
    if (TREE_CODE(type) != ENUMERAL_TYPE) {
        return false;
    }
    static tree const byte = get_identifier("byte");
    tree name = TYPE_NAME(type);
    return name != NULL_TREE && TREE_CODE(name) == TYPE_DECL && DECL_NAME(name) == byte
        && decl_in_std_namespace_p(name);
}

// True when NODE is a conversion of one value to a different type.
bool is_conversion(tree node) {
    return CONVERT_EXPR_P(node) || TREE_CODE(node) == VIEW_CONVERT_EXPR || TREE_CODE(node) == NON_LVALUE_EXPR;
}

// The type that the pointer value NODE points at, before each conversion to
// a different pointer type, or null when NODE is no pointer to an object
// type, such as a void* that the source spells.
tree pointee_of(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        if (!is_conversion(node)) {
            break;
        }
        node = TREE_OPERAND(node, 0);
    }
    tree type = node != NULL_TREE ? TREE_TYPE(node) : NULL_TREE;
    if (type == NULL_TREE || !TYPE_PTR_P(type) || VOID_TYPE_P(TREE_TYPE(type))) {
        return NULL_TREE;
    }
    return TREE_TYPE(type);
}

// Marks each conversion of the pointer value NODE as no view of bytes (the
// byte views).
void exempt_conversions_of(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        exempt_conversions.insert(node);
        tree bare = tree_strip_any_location_wrapper(node);
        if (bare != node) {
            exempt_conversions.insert(bare);
        }
        if (!is_conversion(bare)) {
            return;
        }
        node = TREE_OPERAND(bare, 0);
    }
}

// The type that the pointer value NODE points at before each conversion of a
// pointer: the target of the innermost pointer or reference under the
// conversions of NODE, or null when NODE is no pointer.  A conversion from a
// value that is no pointer or reference ends the walk.
tree origin_pointee(tree node) {
    tree found = NULL_TREE;
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        tree type = TREE_TYPE(node);
        if (type == NULL_TREE || (!TYPE_PTR_P(type) && !TYPE_REF_P(type))) {
            break;
        }
        found = TREE_TYPE(type);
        if (!is_conversion(node)) {
            break;
        }
        node = TREE_OPERAND(node, 0);
    }
    return found;
}

// The object of a view of bytes (the byte views): OBJECT, when TO is a
// pointer to bytes or to void that is not const, and OBJECT is no type of
// bytes and holds a closed tag.  Each other pair gives null.
tree byte_view_of(tree to, tree object) {
    if (to == NULL_TREE || !TYPE_PTR_P(to) || !is_byte_type(TREE_TYPE(to)) || TYPE_READONLY(TREE_TYPE(to))) {
        return NULL_TREE;
    }
    if (object == NULL_TREE || is_byte_type(object)) {
        return NULL_TREE;
    }
    return (unnamed_bit_parts(object, 0) & kClosedTag) != 0 ? object : NULL_TREE;
}

// The object of the view of bytes that the conversion NODE makes (the byte
// views), or null.  The object is the target of the pointer before each
// conversion of the chain, so a const round trip in one expression, such as
// a const_cast of a view of const bytes, is a view of bytes too.
tree byte_view_object(tree node) {
    if (!is_conversion(node) || exempt_conversions.count(node) != 0) {
        return NULL_TREE;
    }
    return byte_view_of(TREE_TYPE(node), origin_pointee(TREE_OPERAND(node, 0)));
}

// The object of the view of bytes that the bit copy NODE of a pointer of the
// type TO makes (the byte views), or null: a __builtin_bit_cast of a pointer,
// or a call of a bit copy function such as std::bit_cast, whose argument is
// a reference to the pointer.
tree bit_copy_view_object(tree node, tree to) {
    if (TREE_CODE(node) == BIT_CAST_EXPR) {
        return byte_view_of(to, origin_pointee(TREE_OPERAND(node, 0)));
    }
    if (TREE_CODE(node) != CALL_EXPR || call_expr_nargs(node) == 0) {
        return NULL_TREE;
    }
    tree argument = TREE_TYPE(CALL_EXPR_ARG(node, 0));
    tree from = argument != NULL_TREE && TYPE_REF_P(argument) ? TREE_TYPE(argument) : argument;
    return from != NULL_TREE && TYPE_PTR_P(from) ? byte_view_of(to, TREE_TYPE(from)) : NULL_TREE;
}

// The destination of the copy of bytes CALL (the closed tags): the type that
// the first argument of a call of memcpy, memmove, mempcpy or memset points
// at, or null.  A copy from an object of the same type gives null, because
// it copies a value that a constructor made.  The conversions of the first
// argument are no view of bytes (the byte views).
tree byte_copy_destination(tree call) {
    tree callee = cp_get_callee_fndecl_nofold(call);
    if (callee == NULL_TREE || call_expr_nargs(call) < 2) {
        return NULL_TREE;
    }
    bool is_copy = fndecl_built_in_p(callee, BUILT_IN_MEMCPY, BUILT_IN_MEMMOVE, BUILT_IN_MEMPCPY, BUILT_IN_MEMCPY_CHK,
                                     BUILT_IN_MEMMOVE_CHK, BUILT_IN_MEMPCPY_CHK);
    bool is_fill = fndecl_built_in_p(callee, BUILT_IN_MEMSET, BUILT_IN_MEMSET_CHK);
    if (!is_copy && !is_fill) {
        return NULL_TREE;
    }
    exempt_conversions_of(CALL_EXPR_ARG(call, 0));
    tree destination = pointee_of(CALL_EXPR_ARG(call, 0));
    if (destination == NULL_TREE || !is_copy) {
        return destination;
    }
    tree source = pointee_of(CALL_EXPR_ARG(call, 1));
    bool is_same_type = source != NULL_TREE && same_type_ignoring_top_level_qualifiers_p(source, destination);
    return is_same_type ? NULL_TREE : destination;
}

// True when FUNCTION registers the end of a static or a thread local object
// (the byte views): the functions of the C++ ABI, with C linkage, that the
// front end calls with the address of the object and its destructor.
bool is_end_registration(tree function) {
    static tree const cxa_atexit = get_identifier("__cxa_atexit");
    static tree const cxa_thread_atexit = get_identifier("__cxa_thread_atexit");
    static tree const aeabi_atexit = get_identifier("__aeabi_atexit");
    tree name = DECL_NAME(function);
    bool is_abi_name = name == cxa_atexit || name == cxa_thread_atexit || name == aeabi_atexit;
    return is_abi_name && DECL_LANG_SPECIFIC(function) != nullptr && DECL_EXTERN_C_P(function);
}

// True when FUNCTION carries the mark NAME of foundation/ByteSeal.h on a
// declaration in a base file (the byte views).  A mark on a function outside
// the base frees no conversion.
bool carries_byte_view_mark(tree function, tree name) {
    static tree const core = get_identifier("core");
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL
        || !function_carries_base_annotation(function, name, core)) {
        return false;
    }
    return place_of(DECL_SOURCE_LOCATION(function), Scope::quarantine).file_class == FileClass::base;
}

// True when FUNCTION frees the storage of an ended object: it carries the
// mark free_door (the byte views).
bool is_free_door(tree function) {
    static tree const free_door = get_identifier("free_door");
    return carries_byte_view_mark(function, free_door);
}

// True when FUNCTION erases the type of the address of an object: it carries
// the mark erasure_door (the byte views).
bool is_erasure_door(tree function) {
    static tree const erasure_door = get_identifier("erasure_door");
    return carries_byte_view_mark(function, erasure_door);
}

// The operand under each conversion, address and indirection of NODE, such
// as the parameter of static_cast<void*>(__builtin_addressof(parameter)).
tree innermost_operand(tree node) {
    for (int depth = 0; node != NULL_TREE && depth < kTypeDepth; ++depth) {
        node = tree_strip_any_location_wrapper(node);
        if (!is_conversion(node) && TREE_CODE(node) != ADDR_EXPR && TREE_CODE(node) != INDIRECT_REF) {
            return node;
        }
        node = TREE_OPERAND(node, 0);
    }
    return node;
}

// A placement new of a body (the byte views): its place, and the type that
// it builds there, or null when the walk finds no such type.  IS_MIXED tells
// that the walk found two different types.
struct PlacementNew {
    tree place = NULL_TREE;
    tree built = NULL_TREE;
    bool is_mixed = false;
};

// The language doors of a body (the byte views): each placement new, the
// arguments of each other door, and the initializer of each temporary.  The
// front end puts the place of a placement new in a temporary, and gives the
// temporary to the operator new.  It puts the pointer of the new in a second
// temporary, and converts that pointer to a pointer to the built type.
// NEW_OF_RESULT maps a call of the placement operator new, and the slot of
// the temporary of its pointer, to the placement new.  IS_ERASURE_DOOR tells
// that the body is the body of an erasure door.
struct LanguageDoors {
    std::unordered_map<tree, tree> initializer_of;
    std::unordered_map<tree, std::size_t> new_of_result;
    std::vector<PlacementNew> placement_news;
    std::vector<tree> arguments;
    bool is_erasure_door = false;
};

// The placement new of the call CALL of the placement operator new, which a
// new entry holds when the walk did not see the call before.
std::size_t placement_new_of(LanguageDoors& found, tree call) {
    auto known = found.new_of_result.find(call);
    if (known != found.new_of_result.end()) {
        return known->second;
    }
    found.placement_news.push_back(PlacementNew{CALL_EXPR_ARG(call, 1)});
    found.new_of_result.emplace(call, found.placement_news.size() - 1);
    return found.placement_news.size() - 1;
}

// Records BUILT as the type that the placement new ENTRY builds.
void note_built_type(PlacementNew& entry, tree built) {
    if (entry.built == NULL_TREE) {
        entry.built = built;
    } else if (!same_type_ignoring_top_level_qualifiers_p(entry.built, built)) {
        entry.is_mixed = true;
    }
}

// True when NODE is a call of the placement operator new of the standard
// library.
bool is_placement_new_call(tree node) {
    if (TREE_CODE(node) != CALL_EXPR || call_expr_nargs(node) != 2) {
        return false;
    }
    tree callee = cp_get_callee_fndecl_nofold(node);
    return callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL && std_placement_new_fn_p(callee);
}

// The callback of cp_walk_tree that collects the LanguageDoors of a body:
// each placement new of a call of the placement operator new of the
// standard library, and of a new-expression of a template with one place,
// the type that each converts its pointer to, each argument of each call of
// an operator delete, of an end registration and of a free door, and in the
// body of an erasure door, each conversion of a parameter to a pointer.
tree collect_language_doors(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    auto* found = static_cast<LanguageDoors*>(data);
    if (TREE_CODE(node) == TARGET_EXPR) {
        tree initializer = TARGET_EXPR_INITIAL(node);
        found->initializer_of.emplace(TARGET_EXPR_SLOT(node), initializer);
        tree bare = initializer != NULL_TREE ? tree_strip_any_location_wrapper(initializer) : NULL_TREE;
        if (bare != NULL_TREE && is_placement_new_call(bare)) {
            found->new_of_result.emplace(TARGET_EXPR_SLOT(node), placement_new_of(*found, bare));
        }
        return NULL_TREE;
    }
    if (TREE_CODE(node) == NEW_EXPR) {
        tree place = TREE_OPERAND(node, 0);
        if (place != NULL_TREE && TREE_CHAIN(place) == NULL_TREE) {
            found->placement_news.push_back(PlacementNew{TREE_VALUE(place), TREE_OPERAND(node, 1)});
        }
        return NULL_TREE;
    }
    bool is_pointer_conversion = is_conversion(node) && TREE_TYPE(node) != NULL_TREE && TYPE_PTR_P(TREE_TYPE(node));
    if (is_pointer_conversion && found->is_erasure_door
        && TREE_CODE(innermost_operand(TREE_OPERAND(node, 0))) == PARM_DECL) {
        found->arguments.push_back(node);
        return NULL_TREE;
    }
    if (is_pointer_conversion && !is_byte_type(TREE_TYPE(TREE_TYPE(node)))) {
        auto result = found->new_of_result.find(tree_strip_any_location_wrapper(TREE_OPERAND(node, 0)));
        if (result != found->new_of_result.end()) {
            note_built_type(found->placement_news[result->second], TREE_TYPE(TREE_TYPE(node)));
        }
        return NULL_TREE;
    }
    if (is_placement_new_call(node)) {
        placement_new_of(*found, node);
        return NULL_TREE;
    }
    tree callee = TREE_CODE(node) == CALL_EXPR ? cp_get_callee_fndecl_nofold(node) : NULL_TREE;
    if (callee != NULL_TREE && TREE_CODE(callee) == FUNCTION_DECL
        && (DECL_IS_OPERATOR_DELETE_P(callee) || is_end_registration(callee) || is_free_door(callee))) {
        for (int index = 0; index < call_expr_nargs(node); ++index) {
            found->arguments.push_back(CALL_EXPR_ARG(node, index));
        }
    }
    return NULL_TREE;
}

// Marks the arguments of the language doors of BODY, the body of FUNCTION or
// an initializer in it, as no view of bytes (the byte views): each argument
// of an operator delete, of an end registration and of a free door, each
// conversion of a parameter in the body of an erasure door, and the place of
// each placement new that builds the type of its place.  The place of a
// placement new that builds a different type in a type that holds a closed
// tag, or a type that the walk did not find, stays a view of bytes.  The
// place can come from a temporary, so its initializer counts as the place.
// Complexity: one visit for each node of the body.
void exempt_language_doors(tree* body, tree function) {
    LanguageDoors found;
    found.is_erasure_door = is_erasure_door(function);
    cp_walk_tree_without_duplicates(body, collect_language_doors, &found);
    for (tree argument : found.arguments) {
        exempt_conversions_of(argument);
    }
    for (PlacementNew const& entry : found.placement_news) {
        tree bare = entry.place;
        while (bare != NULL_TREE && is_conversion(tree_strip_any_location_wrapper(bare))) {
            bare = TREE_OPERAND(tree_strip_any_location_wrapper(bare), 0);
        }
        bare = bare != NULL_TREE ? tree_strip_any_location_wrapper(bare) : NULL_TREE;
        auto initializer = found.initializer_of.find(bare);
        tree source = initializer != found.initializer_of.end() ? initializer->second : NULL_TREE;
        tree origin = origin_pointee(source != NULL_TREE ? source : entry.place);
        bool is_sum = origin != NULL_TREE && !is_byte_type(origin) && (unnamed_bit_parts(origin, 0) & kClosedTag) != 0;
        bool builds_own_type = entry.built != NULL_TREE && !entry.is_mixed && origin != NULL_TREE
                            && same_type_ignoring_top_level_qualifiers_p(origin, entry.built);
        if (is_sum && !builds_own_type) {
            continue;
        }
        exempt_conversions_of(entry.place);
        if (source != NULL_TREE) {
            exempt_conversions_of(source);
        }
    }
}

// The type of the member that the COMPONENT_REF NODE names, when its object
// is a union with no annotation closed_tag and with a second member of bytes,
// or null (the closed tags).  A member of an empty class holds no byte, so
// no write of it reaches the tag.  Complexity: linear in the members of the
// union.
tree union_member_type(tree node) {
    tree object = TREE_OPERAND(node, 0);
    tree field = TREE_OPERAND(node, 1);
    tree union_type = object != NULL_TREE ? TREE_TYPE(object) : NULL_TREE;
    if (union_type == NULL_TREE || TREE_CODE(union_type) != UNION_TYPE || field == NULL_TREE
        || TREE_CODE(field) != FIELD_DECL || carries_closed_tag(TYPE_MAIN_VARIANT(union_type))) {
        return NULL_TREE;
    }
    for (tree other = TYPE_FIELDS(TYPE_MAIN_VARIANT(union_type)); other != NULL_TREE; other = DECL_CHAIN(other)) {
        bool is_other_member = TREE_CODE(other) == FIELD_DECL && other != field;
        if (is_other_member && TREE_TYPE(other) != NULL_TREE && !is_empty_class(TREE_TYPE(other))) {
            return TREE_TYPE(field);
        }
    }
    return NULL_TREE;
}

// The callback of cp_walk_tree: each copy of bytes whose result holds a closed
// tag is the finding forged-tag (the closed tags).  DATA is the location of a
// node with no location of its own.
tree walk_bit_copies(tree* node_pointer, int* walk_subtrees, void* data) {
    tree node = *node_pointer;
    if (node == NULL_TREE) {
        return NULL_TREE;
    }
    if (TYPE_P(node)) {
        *walk_subtrees = 0;
        return NULL_TREE;
    }
    tree result = NULL_TREE;
    const char* route = "";
    // A conversion to a pointer to a const type reads through its operand
    // only, so the conversions of the operand are no view of bytes either.
    if (is_conversion(node) && TREE_TYPE(node) != NULL_TREE && TYPE_PTR_P(TREE_TYPE(node))
        && TYPE_READONLY(TREE_TYPE(TREE_TYPE(node)))) {
        exempt_conversions_of(TREE_OPERAND(node, 0));
    }
    if (TREE_CODE(node) == BIT_CAST_EXPR) {
        result = TREE_TYPE(node);
    } else if (TREE_CODE(node) == CALL_EXPR || TREE_CODE(node) == AGGR_INIT_EXPR) {
        tree callee = cp_get_callee_fndecl_nofold(node);
        result = is_bit_copy_function(callee) ? TREE_TYPE(TREE_TYPE(callee)) : NULL_TREE;
        if (result == NULL_TREE && TREE_CODE(node) == CALL_EXPR) {
            result = byte_copy_destination(node);
            route = kByteCopyRoute;
        }
    } else if (TREE_CODE(node) == COMPONENT_REF) {
        result = union_member_type(node);
        route = kUnionMemberRoute;
    } else {
        result = byte_view_object(node);
        route = kByteViewRoute;
    }
    // A bit copy whose result is a pointer copies the address, so its
    // finding is the view of bytes of that pointer.
    bool is_bit_copy = route[0] == '\0' && result != NULL_TREE;
    if (is_bit_copy && TYPE_PTR_P(result)) {
        result = bit_copy_view_object(node, result);
        route = kByteViewRoute;
    }
    if (result == NULL_TREE || (unnamed_bit_parts(result, 0) & kClosedTag) == 0) {
        return NULL_TREE;
    }
    // Each inner conversion of a view of bytes is a part of the same view.
    if (route == kByteViewRoute && is_conversion(node)) {
        exempt_conversions_of(TREE_OPERAND(node, 0));
    }
    location_t location = EXPR_HAS_LOCATION(node) ? EXPR_LOCATION(node) : *static_cast<location_t*>(data);
    Place place = place_of(location, Scope::quarantine);
    if (place.file != nullptr) {
        add_finding(Kind::forged_tag, nullptr, place, route + type_text(result));
    }
    return NULL_TREE;
}

// PLUGIN_PRE_GENERICIZE: the body of each function that is not a template,
// and of each instance of a template.  A clone of a constructor or a
// destructor gets a second event on a lowered copy of a body, so the walk
// skips it.
void on_pre_genericize_tags(void* gcc_data, void*) {
    tree function = static_cast<tree>(gcc_data);
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL || DECL_SAVED_TREE(function) == NULL_TREE
        || DECL_CLONED_FUNCTION_P(function) || !is_body_read_here(function, UNKNOWN_LOCATION)) {
        return;
    }
    location_t fallback = DECL_SOURCE_LOCATION(function);
    tree body = DECL_SAVED_TREE(function);
    exempt_language_doors(&body, function);
    cp_walk_tree_without_duplicates(&body, walk_bit_copies, &fallback);
    exempt_conversions.clear();
}

// PLUGIN_FOLDED_INITIALIZER: an initializer before the front end folds it, in
// the function current_function_decl or at namespace scope.
void on_folded_initializer_tags(void* gcc_data, void*) {
    const auto* data = static_cast<const plugin_folded_initializer_data*>(gcc_data);
    if (data == nullptr || data->original == NULL_TREE || data->original == error_mark_node
        || !is_body_read_here(current_function_decl, data->location)) {
        return;
    }
    location_t fallback = data->location;
    tree original = data->original;
    exempt_language_doors(&original, current_function_decl);
    cp_walk_tree_without_duplicates(&original, walk_bit_copies, &fallback);
    exempt_conversions.clear();
}

}  // namespace

void report_forged_tag(const Finding& finding) {
    const std::string byte_copy(kByteCopyRoute);
    const std::string byte_view(kByteViewRoute);
    const std::string union_member(kUnionMemberRoute);
    bool const is_view = finding.entity.compare(0, byte_view.size(), byte_view) == 0;
    if (is_view) {
        error_at(finding.spelling,
                 "this conversion makes a pointer that can write the bytes of a %qs, whose bytes hold the tag of a "
                 "sum, and a write through the pointer, such as a copy of bytes, %<read%> or %<recv%>, can write a "
                 "tag that names no alternative",
                 finding.entity.substr(byte_view.size()).c_str());
    } else if (finding.entity.compare(0, byte_copy.size(), byte_copy) == 0) {
        error_at(finding.spelling,
                 "this copy of bytes writes a %qs from bytes of a different type, and the bytes of the type hold the "
                 "tag of a sum, so the copy can write a tag that names no alternative",
                 finding.entity.substr(byte_copy.size()).c_str());
    } else if (finding.entity.compare(0, union_member.size(), union_member) == 0) {
        error_at(finding.spelling,
                 "this member of a union is a %qs, whose bytes hold the tag of a sum, and a write of a different "
                 "member of the union can write a tag that names no alternative",
                 finding.entity.substr(union_member.size()).c_str());
    } else {
        error_at(finding.spelling,
                 "this bit copy makes a %qs, whose bytes hold the tag of a sum, and a bit copy can write a tag that "
                 "names no alternative",
                 finding.entity.c_str());
    }
    inform(finding.spelling,
           "a class that carries the annotation %<foundation::core::closed_tag%> holds the tag, and only a "
           "constructor of the sum writes it");
    inform(finding.spelling,
           "build the sum from its value, or decode each part with %<foundation::core::decode%> and build the sum "
           "from the parts");
    if (is_view) {
        inform(finding.spelling,
               "a pointer to const bytes reads the bytes, a copy of bytes takes the pointer in the same expression, "
               "and %<foundation::core::mint_erased_address%> erases the type of an object with no pointer to bytes");
    }
    inform(finding.spelling,
           "a test of the copy itself puts it in a %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: reason\")%> region of "
           "%<foundation/Quarantine.h%>");
}

void register_closed_tags(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRE_GENERICIZE, on_pre_genericize_tags, nullptr);
    register_callback(plugin_name, PLUGIN_FOLDED_INITIALIZER, on_folded_initializer_tags, nullptr);
}

}  // namespace crucible_plugin
