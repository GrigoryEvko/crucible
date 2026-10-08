// The opt-out regions.
//
// CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason") of foundation/Quarantine.h
// opens a region, and CRUCIBLE_END_I_KNOW_WHAT_IM_DOING closes it.  The macros
// expand to #pragma quarantine I_KNOW_WHAT_IM_DOING("CLASS: reason") and
// #pragma quarantine END_I_KNOW_WHAT_IM_DOING.  The plugin refuses such a
// pragma of a file under the root that the macros do not make, a use of a
// macro inside another macro, a reason that is not one plain string literal in
// the call, and a reason that does not start with its class.  A finding inside
// a region is reported as opted-out and is not an error.  The report gives each
// region of the unit a line of the kind region, and the ledger of the rule
// quarantine counts the regions of each file and reason.

#include "plugin.h"

namespace crucible_plugin {

namespace {

constexpr char kBeginPragma[] = "I_KNOW_WHAT_IM_DOING";
constexpr char kEndPragma[] = "END_I_KNOW_WHAT_IM_DOING";
// The classes of the reason of a region (CLAUDE.md section XXII, R7).
constexpr const char* kReasonClasses[] = {"ABI: ", "C-HEADER: ", "PROBE: ", "ORACLE: ", "MEASURE: "};

// The expansion point of the macro MACRO that made the pragma at LOCATION,
// or UNKNOWN_LOCATION when no such macro made it, or when a quarantined file
// defines the macro.  The front end gives each token of a _Pragma the location
// of the _Pragma operator, so the maps of the macro expansion lead to the
// macro.  Complexity: O(depth of the macro expansion).
location_t region_macro_expansion(location_t location, const char* macro) {
    location_t current = location;
    for (int depth = 0; depth < kMacroDepth && linemap_location_from_macro_expansion_p(line_table, current); ++depth) {
        const line_map* map = linemap_lookup(line_table, current);
        const line_map_macro* macro_map = linemap_check_macro(map);
        cpp_hashnode* node = MACRO_MAP_MACRO(macro_map);
        if (node != nullptr && cpp_user_macro_p(node)
            && std::strcmp(reinterpret_cast<const char*>(NODE_NAME(node)), macro) == 0) {
            expanded_location definition = expand_location(cpp_macro_definition_location(node));
            if (definition.file == nullptr) {
                return UNKNOWN_LOCATION;
            }
            FileClass file_class = classify_file(definition.file).file_class;
            bool is_trusted = file_class != FileClass::quarantined && file_class != FileClass::unclassified;
            return is_trusted ? macro_map->get_expansion_point_location() : UNKNOWN_LOCATION;
        }
        current = linemap_unwind_toward_expansion(line_table, current, &map);
    }
    return UNKNOWN_LOCATION;
}

// The place of a region pragma at LOCATION: the place where the file uses
// MACRO.  A pragma of a file outside the source root gives no place and no
// error.  A pragma of a file under the root that MACRO does not make is an
// error, which the function reports.  A use of MACRO inside another macro is
// an error too: the place of the region would then be the definition of that
// macro, and the ledger would count the region in that file.  EXPANSION gets
// the expansion point of MACRO.  Returns a place with no file when the pragma
// opens or closes nothing.
Place region_place(location_t location, const char* macro, const char* pragma, location_t& expansion) {
    Place place = place_of(location, Scope::tree);
    if (place.file == nullptr) {
        return place;
    }
    expansion = region_macro_expansion(location, macro);
    if (expansion == UNKNOWN_LOCATION) {
        error_at(location,
                 "%<#pragma quarantine %s%> comes only from the macro %qs of %<foundation/Quarantine.h%>; use the "
                 "macro, which the plugin knows, and not the pragma",
                 pragma, macro);
        return Place{};
    }
    if (linemap_location_from_macro_expansion_p(line_table, expansion)) {
        error_at(expansion,
                 "the macro %qs is used inside another macro.  Write it directly in the file, so that the ledger "
                 "counts the region and its end in the file that uses them",
                 macro);
        return Place{};
    }
    return place_of(expansion, Scope::tree);
}

// True when the source spells the call of the begin macro at EXPANSION as
// CRUCIBLE_I_KNOW_WHAT_IM_DOING("REASON") on one line, with only spaces and
// tabs between the tokens.  The ledger counts each region by its reason, and a
// search of the source finds a region by its reason only in this form.
// Complexity: O(length of the line).
bool is_plain_region_call(location_t expansion, const std::string& reason) {
    expanded_location where = expand_location(expansion);
    if (where.file == nullptr || where.line <= 0 || where.column <= 0) {
        return false;
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(where.file, where.line);
    if (!line) {
        return false;
    }
    std::string text(line.get_buffer(), line.length());
    std::size_t at = static_cast<std::size_t>(where.column - 1);
    auto take = [&text, &at](const std::string& word) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
            ++at;
        }
        if (text.compare(at, word.size(), word) != 0) {
            return false;
        }
        at += word.size();
        return true;
    };
    return take(kBeginMacro) && take("(") && take("\"" + reason + "\"") && take(")");
}

bool has_reason_class(const std::string& reason) {
    for (const char* reason_class : kReasonClasses) {
        if (has_prefix(reason, reason_class)) {
            return true;
        }
    }
    return false;
}

void handle_begin_pragma(cpp_reader*) {
    tree value = NULL_TREE;
    location_t open_location = UNKNOWN_LOCATION;
    location_t token_location = UNKNOWN_LOCATION;
    bool has_reason = false;
    std::string reason;
    if (pragma_lex(&value, &open_location) == CPP_OPEN_PAREN) {
        cpp_ttype token = pragma_lex(&value, &token_location);
        if (token == CPP_STRING && value != NULL_TREE && TREE_CODE(value) == STRING_CST
            && TREE_STRING_LENGTH(value) > 1) {
            reason.assign(TREE_STRING_POINTER(value), static_cast<std::size_t>(TREE_STRING_LENGTH(value) - 1));
            has_reason = pragma_lex(&value, &token_location) == CPP_CLOSE_PAREN
                      && pragma_lex(&value, &token_location) == CPP_EOF;
        }
    }
    location_t location = open_location != UNKNOWN_LOCATION ? open_location : input_location;
    if (!has_reason) {
        error_at(location,
                 "%qs takes one string that gives the reason: write "
                 "%<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"CLASS: reason\")%>",
                 kBeginMacro);
        return;
    }
    // A pragma of a file outside the source root and the build directory
    // opens no region.  A pragma of a generated file opens one, because the
    // contract rule applies there.
    location_t expansion = UNKNOWN_LOCATION;
    Place place = region_place(location, kBeginMacro, kBeginPragma, expansion);
    if (place.file == nullptr) {
        return;
    }
    if (!is_plain_region_call(expansion, reason)) {
        error_at(expansion,
                 "write the reason of a region as one plain string literal in the call: "
                 "%<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"CLASS: reason\")%>.  The ledger counts each region by this "
                 "reason, and the reason is %qs",
                 reason.c_str());
        return;
    }
    if (!has_reason_class(reason)) {
        error_at(location,
                 "the reason of a region starts with its class: %<ABI:%>, %<C-HEADER:%>, %<PROBE:%>, %<ORACLE:%> "
                 "or %<MEASURE:%> (CLAUDE.md section XXII); the reason is %qs",
                 reason.c_str());
        return;
    }
    if (!state.regions.empty() && state.regions.back().end_line == 0) {
        error_at(location, "a %qs region is open at line %d; close it with %qs before you open a new region",
                 kBeginMacro, state.regions.back().begin_line, kEndMacro);
        return;
    }
    Region region;
    region.file = place.file;
    region.begin_line = place.line;
    region.begin = location;
    region.reason = std::move(reason);
    state.regions.push_back(std::move(region));
}

void handle_end_pragma(cpp_reader*) {
    tree value = NULL_TREE;
    location_t location = UNKNOWN_LOCATION;
    cpp_ttype token = pragma_lex(&value, &location);
    if (location == UNKNOWN_LOCATION) {
        location = input_location;
    }
    if (token != CPP_EOF) {
        error_at(location, "%qs takes no argument", kEndMacro);
        return;
    }
    // A pragma of a file outside the source root closes nothing, as the begin
    // pragma of such a file opens nothing.
    location_t expansion = UNKNOWN_LOCATION;
    Place place = region_place(location, kEndMacro, kEndPragma, expansion);
    if (place.file == nullptr) {
        return;
    }
    if (state.regions.empty() || state.regions.back().end_line != 0) {
        error_at(location, "%qs has no open region; open one with %<CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"CLASS: reason\")%>",
                 kEndMacro);
        return;
    }
    Region& region = state.regions.back();
    if (place.file != region.file) {
        error_at(location, "%qs closes a region that another file opened; close each region in the file that opens it",
                 kEndMacro);
        return;
    }
    region.end_line = place.line;
}

void register_pragmas(void*, void*) {
    c_register_pragma("quarantine", kBeginPragma, handle_begin_pragma);
    c_register_pragma("quarantine", kEndPragma, handle_end_pragma);
}

}  // namespace

bool is_opted_out(const Finding& finding) {
    for (const Region& region : state.regions) {
        if (region.file == finding.file && region.begin_line <= finding.line
            && (region.end_line == 0 || finding.line <= region.end_line)) {
            return true;
        }
    }
    return false;
}

// An error for each region that the unit does not close.
void report_open_regions() {
    for (const Region& region : state.regions) {
        if (region.end_line == 0) {
            error_at(region.begin, "this %qs region has no %qs", kBeginMacro, kEndMacro);
        }
    }
}

// Registers the two pragmas of a region.
void register_regions(const char* plugin_name) {
    register_callback(plugin_name, PLUGIN_PRAGMAS, register_pragmas, nullptr);
}

}  // namespace crucible_plugin
