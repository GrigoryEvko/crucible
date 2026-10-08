// The family unit fact: a fact of the whole unit or of the options of one
// function, and no fact of an expression.  Each finding of the family is an
// error in each build and each mode, as the contract rule is, because no
// enforce mode and no ledger row changes such a fact.  An opt-out region
// turns a finding of a function into opted-out.
//
// THE CONTRACT SEMANTIC (the rule contract-semantic)
//     GCC uses the last -fcontract-evaluation-semantic of the command line.
//     CRUCIBLE_PRE and CRUCIBLE_POST cannot read the semantic.  The macro
//     CRUCIBLE_CONTRACT_SEMANTIC_IGNORE tells them the semantic (CLAUDE.md
//     section XII).  With the macro, each precondition gives its condition to
//     the optimizer as an assumption.  A semantic that does the checks can
//     call a handler that returns, and the assumption of a condition that does
//     not hold is then undefined behavior.  Without the macro, the ignore
//     semantic keeps no hint.  The macro and the semantic ignore must come
//     together.  The plugin reads the macro when the main file starts, after
//     the command line and the files of -include.  The finding has the place
//     of the main file.
//
// THE FAST-MATH OPTIONS (the rule fast-math-override)
//     An optimize attribute and a GCC optimize pragma give a function options
//     of its own (DECL_FUNCTION_SPECIFIC_OPTIMIZATION).  CLAUDE.md section V
//     bans each option of the fast-math family and each option that cancels a
//     flag of the floor of cmake/FpStrict.cmake.  A function of a file of the
//     root gets a finding when its own options turn on such an option and the
//     command line of the unit does not.  PLUGIN_FINISH_DECL gives each
//     declaration of a function, and PLUGIN_FINISH_PARSE_FUNCTION each
//     definition.  A header can open a pragma and not close it.  The pragma
//     then gives its options to each function after the include, and each
//     such function gets a finding.  A pragma that no function follows
//     changes no code and is no finding.  The plugin reads only the arms that
//     a build compiles.  An attribute or a pragma in another arm, or in a
//     macro body that no unit expands, is no finding (decision 5 of
//     misc/01_10_2026_quarantine.md).

#include "plugin.h"

namespace crucible_plugin {

namespace {

// The macro that tells CRUCIBLE_PRE and CRUCIBLE_POST the ignore semantic.
constexpr char kIgnoreDefine[] = "CRUCIBLE_CONTRACT_SEMANTIC_IGNORE";

// The place of the main file of the unit, with the line 0, or a place with no
// file when the main file is not under the root or in the build directory.
Place main_file_place() {
    Place place;
    if (main_input_filename == nullptr) {
        return place;
    }
    const FileEntry& entry = classify_file(main_input_filename);
    if (entry.relative.empty()) {
        return place;
    }
    place.file_class = entry.file_class;
    place.file = &entry;
    return place;
}

// The names of the fast-math options that the options NODE turn on and the
// options BASE do not: the fast-math family, and the cancel of each flag of
// the floor of cmake/FpStrict.cmake.  The names are those of the
// optimize attribute, in one order.
std::string fast_math_options(const cl_optimization& node, const cl_optimization& base) {
    std::string names;
    auto add = [&names](bool is_on, const char* name) {
        if (is_on) {
            names += names.empty() ? name : std::string(",") + name;
        }
    };
    add(node.x_optimize_fast && !base.x_optimize_fast, "Ofast");
    add(node.x_flag_unsafe_math_optimizations && !base.x_flag_unsafe_math_optimizations, "unsafe-math-optimizations");
    add(node.x_flag_associative_math && !base.x_flag_associative_math, "associative-math");
    add(node.x_flag_reciprocal_math && !base.x_flag_reciprocal_math, "reciprocal-math");
    add(node.x_flag_finite_math_only && !base.x_flag_finite_math_only, "finite-math-only");
    add(!node.x_flag_signed_zeros && base.x_flag_signed_zeros, "no-signed-zeros");
    add(node.x_flag_fp_contract_mode == FP_CONTRACT_FAST && base.x_flag_fp_contract_mode != FP_CONTRACT_FAST,
        "fp-contract=fast");
    add(node.x_flag_fp_contract_mode == FP_CONTRACT_ON && base.x_flag_fp_contract_mode == FP_CONTRACT_OFF,
        "fp-contract=on");
    add(!node.x_flag_trapping_math && base.x_flag_trapping_math, "no-trapping-math");
    add(!node.x_flag_signaling_nans && base.x_flag_signaling_nans, "no-signaling-nans");
    add(!node.x_flag_rounding_math && base.x_flag_rounding_math, "no-rounding-math");
    return names;
}

// The options of FUNCTION, when it has options of its own (THE FAST-MATH
// OPTIONS).  Each function counts one time.
void check_function_options(tree function) {
    if (function == NULL_TREE || TREE_CODE(function) != FUNCTION_DECL) {
        return;
    }
    tree options = DECL_FUNCTION_SPECIFIC_OPTIMIZATION(function);
    if (options == NULL_TREE || options == optimization_default_node
        || !state.optimized_functions.insert(DECL_UID(function)).second) {
        return;
    }
    Place place = place_of(DECL_SOURCE_LOCATION(function), Scope::tree);
    if (place.file == nullptr) {
        return;
    }
    std::string names =
        fast_math_options(*TREE_OPTIMIZATION(options), *TREE_OPTIMIZATION(optimization_default_node));
    if (!names.empty()) {
        add_finding(Kind::fast_math_override, nullptr, place, qualified_name(function) + " (" + names + ")");
    }
}

void on_finish_declaration(void* gcc_data, void*) { check_function_options(static_cast<tree>(gcc_data)); }

void on_finish_function(void* gcc_data, void*) { check_function_options(static_cast<tree>(gcc_data)); }

}  // namespace

// The main file starts: read the define of the command line (THE CONTRACT
// SEMANTIC).
void note_main_started() {
    state.is_ignore_defined =
        cpp_defined(parse_in, reinterpret_cast<const unsigned char*>(kIgnoreDefine), sizeof kIgnoreDefine - 1);
}

// The facts of the whole unit, for the report: the contract semantic.
void report_unit_facts() {
    Place place = main_file_place();
    if (place.file == nullptr) {
        return;
    }
    bool is_ignore = flag_contract_evaluation_semantic == CES_IGNORE;
    if (state.is_ignore_defined && !is_ignore) {
        add_finding(Kind::contract_semantic, nullptr, place,
                    std::string(kIgnoreDefine) + " with a semantic other than ignore");
    } else if (is_ignore && !state.is_ignore_defined) {
        add_finding(Kind::contract_semantic, nullptr, place,
                    std::string("the semantic ignore with no ") + kIgnoreDefine);
    }
}

// Registers the hooks of the options of each function.
void register_unit_facts(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_FINISH_DECL, on_finish_declaration, nullptr);
    register_callback(plugin_name, PLUGIN_FINISH_PARSE_FUNCTION, on_finish_function, nullptr);
}

}  // namespace crucible_plugin
