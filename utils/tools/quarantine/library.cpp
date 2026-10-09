// The library entities and the admitted list: the names of a declaration, the
// library of a declaration, the admit row of each use, and the library classes
// that a type holds.  The other sources of the plugin ask these questions for
// each name and each type that they read.

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

// True when TYPE holds a bool or an enumeration: the type itself, an element
// of an array, a vector or a complex type, or a data member or a base of a
// class or a union.  A pointer holds an address, so the walk does not follow
// it, also when its target is dependent.  Each other dependent type and an
// incomplete class count as types that hold them, so the restriction
// plain-result fails closed.  The walk keeps the answer of each class, so a
// class that holds the same class in many members costs one visit.
// Complexity: O(distinct types in the type tree).
bool holds_bool_or_enumeration(tree type, int depth) {
    if (type == NULL_TREE || type == error_mark_node || depth > kTypeDepth) {
        return true;
    }
    switch (TREE_CODE(type)) {
        case POINTER_TYPE:
        case OFFSET_TYPE:
            return false;
        case BOOLEAN_TYPE:
        case ENUMERAL_TYPE:
            return true;
        case ARRAY_TYPE:
        case VECTOR_TYPE:
        case COMPLEX_TYPE:
            return holds_bool_or_enumeration(TREE_TYPE(type), depth + 1);
        case RECORD_TYPE:
        case UNION_TYPE: {
            tree main_type = TYPE_MAIN_VARIANT(type);
            if (TYPE_PTRMEMFUNC_P(main_type)) {
                return false;
            }
            if (uses_template_parms(main_type) || !COMPLETE_TYPE_P(main_type)) {
                return true;
            }
            auto cached = state.holds_bool_of.find(main_type);
            if (cached != state.holds_bool_of.end()) {
                return cached->second;
            }
            bool holds = false;
            for (tree field = TYPE_FIELDS(main_type); field != NULL_TREE && !holds; field = DECL_CHAIN(field)) {
                holds = TREE_CODE(field) == FIELD_DECL && holds_bool_or_enumeration(TREE_TYPE(field), depth + 1);
            }
            state.holds_bool_of.emplace(main_type, holds);
            return holds;
        }
        default:
            return uses_template_parms(type);
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
        if (holds_bool_or_enumeration(type, 0)) {
            return false;
        }
    }
    return !row.is_concepts_only || concept_definition_p(decl);
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
// THE TEXT OF A KEY
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
//     finding holds no printed type at all (declaration.cpp, THE ENTITY OF AN
//     OBJECT).

namespace {

constexpr int kPrintFlags = TFF_NO_OMIT_DEFAULT_TEMPLATE_ARGUMENTS;

// The template parameters that the print of ROOT can name, with the name
// T<DEPTH>_<POSITION> while the object lives (THE TEXT OF A KEY).  The walk
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
// std::tuple_element_t need their own entries.  A header entry admits each
// entity that the header itself declares: the file that an #include of that
// exact name enters, and not a header of the same last name in a different
// directory, such as experimental/type_traits.  A restriction of the row
// narrows what it admits (row_admits).  RESULT is the result type that the
// explicit template arguments of a call give, or null.  LANGUAGE names the
// file of the language of the use, or is null.  The cache holds the answers of
// a quarantined file only.
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

}  // namespace crucible_plugin
