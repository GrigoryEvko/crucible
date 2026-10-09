// The opt-out regions.
//
// CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason") of foundation/Quarantine.h
// opens a region, and CRUCIBLE_END_I_KNOW_WHAT_IM_DOING closes it.  The macros
// expand to #pragma quarantine I_KNOW_WHAT_IM_DOING("CLASS: reason") and
// #pragma quarantine END_I_KNOW_WHAT_IM_DOING.  The plugin refuses such a
// pragma of a file under the root that the macros do not make, a use of a
// macro inside another macro, a reason that is not one plain string literal in
// the call, and a reason that does not start with its class and a word.  It
// also refuses a macro with other code on its line, because a region holds
// each line from the line of its begin macro to the line of its end macro.  A
// file of the language holds no region, because each finding there is an
// error in each build.  A finding inside a region is reported as opted-out and
// is not an error.  The report gives each region of the unit a line of the
// kind region, and the ledger of the rule quarantine counts the regions of
// each file and reason.  A unit that writes no report and no section gives
// each region a note at the use of its begin macro, so that a census of the
// notes gives each opted-out finding the reason of its region.

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

// The line of the source that holds EXPANSION, and the index of the column of
// EXPANSION in it.  Returns false when the plugin cannot read the line.
bool region_line_at(location_t expansion, std::string& text, std::size_t& at) {
    expanded_location where = expand_location(expansion);
    if (where.file == nullptr || where.line <= 0 || where.column <= 0) {
        return false;
    }
    diagnostics::char_span line = global_dc->get_file_cache().get_source_line(where.file, where.line);
    if (!line) {
        return false;
    }
    text.assign(line.get_buffer(), line.length());
    at = static_cast<std::size_t>(where.column - 1);
    return true;
}

bool is_blank(char byte) { return byte == ' ' || byte == '\t' || byte == '\r'; }

// True when the source spells the call of the begin macro at EXPANSION as
// CRUCIBLE_I_KNOW_WHAT_IM_DOING("REASON") on one line, with only spaces and
// tabs between the tokens.  The ledger counts each region by its reason, and a
// search of the source finds a region by its reason only in this form.
// Complexity: O(length of the line).
bool is_plain_region_call(location_t expansion, const std::string& reason) {
    std::string text;
    std::size_t at = 0;
    if (!region_line_at(expansion, text, at)) {
        return false;
    }
    auto take = [&text, &at](const std::string& word) {
        while (at < text.size() && is_blank(text[at])) {
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

// True when the line of EXPANSION holds only the call CALL that starts at
// EXPANSION, with blanks before and after it.  A region holds each line from
// the line of its begin macro to the line of its end macro, so code on the
// line of a macro would be inside the region.  Complexity: O(length of the
// line).
bool is_alone_on_line(location_t expansion, const std::string& call) {
    std::string text;
    std::size_t at = 0;
    if (!region_line_at(expansion, text, at) || text.compare(at, call.size(), call) != 0) {
        return false;
    }
    for (std::size_t index = 0; index < at; ++index) {
        if (!is_blank(text[index])) {
            return false;
        }
    }
    for (std::size_t index = at + call.size(); index < text.size(); ++index) {
        if (!is_blank(text[index])) {
            return false;
        }
    }
    return true;
}

// True when REASON starts with a reason class and holds a letter or a digit
// after it.  A class with no word after it gives no reason.
bool is_classed_reason(const std::string& reason) {
    for (const char* reason_class : kReasonClasses) {
        if (!has_prefix(reason, reason_class)) {
            continue;
        }
        for (std::size_t index = std::strlen(reason_class); index < reason.size(); ++index) {
            char byte = reason[index];
            if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')) {
                return true;
            }
        }
        return false;
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
    if (!is_classed_reason(reason)) {
        error_at(location,
                 "the reason of a region starts with its class: %<ABI:%>, %<C-HEADER:%>, %<PROBE:%>, %<ORACLE:%> "
                 "or %<MEASURE:%>, and words after the class give the reason (CLAUDE.md section XXII); the reason "
                 "is %qs",
                 reason.c_str());
        return;
    }
    // An error that keeps the region, so that its end macro closes it.
    if (!is_alone_on_line(expansion, std::string(kBeginMacro) + "(\"" + reason + "\")")) {
        error_at(expansion,
                 "write %qs alone on its line: the region holds each line from its begin to its end, so code on the "
                 "line of the macro is inside the region",
                 kBeginMacro);
    }
    if (place.file->language_layer >= 0) {
        error_at(expansion,
                 "a file of the language holds no %qs region: each finding there is an error in each build, and no "
                 "region opts it out (CLAUDE.md section XXII)",
                 kBeginMacro);
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
    region.use = expansion;
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
    // An error that keeps the end, so that it closes its region.
    if (!is_alone_on_line(expansion, kEndMacro)) {
        error_at(expansion,
                 "write %qs alone on its line: the region holds each line from its begin to its end, so code on the "
                 "line of the macro is inside the region",
                 kEndMacro);
    }
    // The begin macro of a file of the language gave the error, and it opened
    // no region.
    if (place.file->language_layer >= 0) {
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
