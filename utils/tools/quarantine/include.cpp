// The family include closure: the include rules, the expansions of assert and
// the provenance of each place.
//
// The include rules apply to each #include directive that a base or a
// quarantined file holds.  A base file can include a header of its own layer
// and of each lower layer, and each header outside the root that the allow
// rows give its layer.  A door header can have only its door as includer.  A
// file of the language gets the allowance of its own layer and of the layer of
// its language row, whichever is lower.
//
// THE HOOKS
//     PLUGIN_INCLUDE_FILE    each file that the preprocessor enters.  The file
//                            that an #include directive enters is the target
//                            of that directive.
//     the include callback of libcpp
//                            each #include directive, before the preprocessor
//                            looks for the file.  A directive that enters no
//                            file names a file that the unit entered before:
//                            the plugin finds it as libcpp does, beside the
//                            includer and in the search chain of the directive.
//     the macro callback of libcpp
//                            each expansion of a macro: the expansions of assert.
//     the file change callback of libcpp
//                            THE PROVENANCE below.

#include "plugin.h"

namespace crucible_plugin {

namespace {

const std::string& layer_name(int layer) { return state.table.layers[static_cast<std::size_t>(layer)].name; }

int layer_rank(int layer) { return state.table.layers[static_cast<std::size_t>(layer)].rank; }

// The target of a directive that entered no file.  An include guard or
// #pragma once stopped it, so the unit entered the target before.  The plugin
// looks for the file as libcpp does: beside the includer for a quoted name,
// and then in each directory of the search chain of the directive, in order.
// An #include_next starts after the directory of the chain that holds the
// includer.  Returns null when no directory holds the name, which the rules
// treat as a header outside the root.  Complexity: O(directories of the chain).
const FileEntry* resolve_skipped_include(const PendingInclude& pending) {
    const std::string& includer = pending.place.file->real;
    std::string includer_directory = includer.substr(0, includer.rfind('/'));
    if (!pending.is_angle && !pending.is_next && !includer.empty()) {
        std::string beside = real_path((includer_directory + "/" + pending.spelled).c_str());
        if (!beside.empty()) {
            return &entry_of_real_path(beside);
        }
    }
    cpp_dir* first = get_added_cpp_dirs(pending.is_angle ? INC_BRACKET : INC_QUOTE);
    if (pending.is_next && !includer.empty()) {
        for (cpp_dir* directory = first; directory != nullptr; directory = directory->next) {
            if (real_path(directory->name) == includer_directory) {
                first = directory->next;
                break;
            }
        }
    }
    for (cpp_dir* directory = first; directory != nullptr; directory = directory->next) {
        std::string candidate = real_path((std::string(directory->name) + "/" + pending.spelled).c_str());
        if (!candidate.empty()) {
            return &entry_of_real_path(candidate);
        }
    }
    return nullptr;
}

// The include rules for one directive and its target.
void check_include(const PendingInclude& pending, const FileEntry* target) {
    const FileEntry& includer = *pending.place.file;
    if (target != nullptr && target->file_class != FileClass::outside) {
        if (includer.file_class != FileClass::base
            || (target->file_class == FileClass::base && layer_rank(target->layer) <= layer_rank(includer.layer))) {
            return;
        }
        std::string what =
            target->file_class == FileClass::base ? "the layer " + layer_name(target->layer) : std::string("no layer");
        add_finding(Kind::upward_include, nullptr, pending.place,
                    target->relative + " (" + what + ") from the layer " + layer_name(includer.layer));
        return;
    }
    for (const DoorRow& door : state.table.doors) {
        if (door.header == pending.spelled) {
            if (includer.relative != door.owner) {
                add_finding(Kind::door_header, nullptr, pending.place,
                            "<" + pending.spelled + "> has the door " + door.owner);
            }
            return;
        }
    }
    if (includer.file_class != FileClass::base) {
        return;
    }
    // A file of the language gets the allowance of its own layer and of the
    // layer of its language row, whichever is lower.
    int allowed_rank = layer_rank(includer.layer);
    std::string allowance = "the layer " + layer_name(includer.layer);
    if (includer.language_layer >= 0 && layer_rank(includer.language_layer) < allowed_rank) {
        allowed_rank = layer_rank(includer.language_layer);
        allowance += ", a file of the language with the allowance of the layer " + layer_name(includer.language_layer);
    }
    for (const auto& [layer, header] : state.table.allows) {
        if (header == pending.spelled && layer_rank(layer) <= allowed_rank) {
            return;
        }
    }
    add_finding(Kind::layer_header, nullptr, pending.place, "<" + pending.spelled + "> in " + allowance);
}

// The include callback of libcpp: one #include directive, before the
// preprocessor looks for the file.
void on_include(cpp_reader* reader, location_t location, const unsigned char* directive, const char* name, int is_angle,
                const cpp_token** comments) {
    if (state.previous_include != nullptr) {
        state.previous_include(reader, location, directive, name, is_angle, comments);
    }
    state.include_directive = location;
    finish_pending_include(nullptr);
    auto header = is_angle != 0 && name != nullptr ? state.admitted_headers.find(name) : state.admitted_headers.end();
    state.admitted_header_pending = header != state.admitted_headers.end() ? header->second : nullptr;
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr || name == nullptr) {
        return;
    }
    state.pending.is_set = true;
    state.pending.place = place;
    state.pending.spelled = name;
    state.pending.is_angle = is_angle != 0;
    state.pending.is_next =
        directive != nullptr && std::strcmp(reinterpret_cast<const char*>(directive), "include_next") == 0;
}

// The macro callback of libcpp: one expansion of a macro, after libcpp read
// its arguments.  libcpp also calls it for a test of a macro in a directive,
// with the location of the directive line, where the source spells no
// identifier at the start.
void on_macro_used(cpp_reader* reader, location_t location, cpp_hashnode* node) {
    if (state.previous_used != nullptr) {
        state.previous_used(reader, location, node);
    }
    if (is_system_assert(node) && identifier_spelled_at(location) == "assert") {
        record(Kind::assert_expansion, location, "assert");
    }
}

// THE PROVENANCE.  libcpp calls the file change callback for each file that
// the preprocessor enters or leaves, and for each rename of a line map that a
// directive makes: #line, a line marker and #pragma GCC system_header.
// libcpp makes the other renames of a map itself, with no callback.  Before
// the main file starts, the front end renames the map to <built-in> and
// <command-line>, enters the files of -include, and renames the map back to
// the main file, the file of the first map.  The front end sets its own
// callback after plugin_init, so the plugin chains it at PLUGIN_START_UNIT.

// An error for a rename that a directive of a file under the root makes.  The
// map before the rename holds the directive.
void check_rename(const line_map_ordinary* map) {
    if (!state.is_main_started) {
        const line_map_ordinary* main_map = LINEMAPS_ORDINARY_MAP_AT(line_table, 0);
        state.is_main_started =
            main_map != map && std::strcmp(ORDINARY_MAP_FILE_NAME(main_map), ORDINARY_MAP_FILE_NAME(map)) == 0;
        return;
    }
    unsigned used = LINEMAPS_ORDINARY_USED(line_table);
    if (used < 2) {
        return;
    }
    const line_map_ordinary* holder = LINEMAPS_ORDINARY_MAP_AT(line_table, used - 2);
    const FileEntry& file = classify_file(ORDINARY_MAP_FILE_NAME(holder));
    if (file.file_class == FileClass::outside) {
        return;
    }
    location_t start = MAP_START_LOCATION(map);
    error_at(start > MAP_START_LOCATION(holder) ? start - 1 : UNKNOWN_LOCATION,
             "quarantine: %qs holds a %<#line%> directive, a line marker or %<#pragma GCC system_header%>.  Each one "
             "changes the file, the line or the system flag of the code after it, so a file under the source root "
             "does not hold one",
             file.relative.c_str());
}

// An error for a line marker of a file under the root that enters a file.  An
// #include directive of the same line comes before each entry that is no
// line marker.
void check_entry(const line_map_ordinary* map) {
    location_t directive = state.include_directive;
    state.include_directive = UNKNOWN_LOCATION;
    location_t from = linemap_included_from(map);
    if (!state.is_main_started || from == UNKNOWN_LOCATION) {
        return;
    }
    expanded_location includer = expand_location(from);
    if (includer.file == nullptr) {
        return;
    }
    const FileEntry& file = classify_file(includer.file);
    if (file.file_class == FileClass::outside) {
        return;
    }
    expanded_location announced = expand_location(directive);
    bool is_directive = directive != UNKNOWN_LOCATION && announced.file != nullptr && announced.line == includer.line
                     && &classify_file(announced.file) == &file;
    if (!is_directive) {
        error_at(from,
                 "quarantine: a line marker of %qs enters %qs.  A file under the source root enters a file only with "
                 "%<#include%>, so each finding keeps its file and its line",
                 file.relative.c_str(), ORDINARY_MAP_FILE_NAME(map));
    }
}

// The file change callback of libcpp: the provenance checks of the new map,
// after the callback of the front end.
void on_file_change(cpp_reader* reader, const line_map_ordinary* map) {
    if (state.previous_file_change != nullptr) {
        state.previous_file_change(reader, map);
    }
    if (map == nullptr) {
        return;
    }
    if (map->reason == LC_RENAME || map->reason == LC_RENAME_VERBATIM) {
        check_rename(map);
    } else if (map->reason == LC_ENTER) {
        check_entry(map);
    }
}

void on_start_unit(void*, void*) {
    cpp_callbacks* callbacks = cpp_get_callbacks(parse_in);
    state.previous_file_change = callbacks->file_change;
    callbacks->file_change = on_file_change;
}

// Each file that the preprocessor enters.  The file that the last directive
// names enters right after the include callback, with that directive as the
// place it comes from.
void on_include_file(void* gcc_data, void*) {
    const char* name = static_cast<const char*>(gcc_data);
    if (name == nullptr) {
        return;
    }
    const FileEntry& entry = classify_file(name);
    const line_map_ordinary* map = LINEMAPS_LAST_ORDINARY_MAP(line_table);
    if (map == nullptr || map->reason != LC_ENTER) {
        return;
    }
    if (state.admitted_header_pending != nullptr) {
        state.admitted_header_paths.emplace(entry.real, state.admitted_header_pending);
        state.admitted_header_pending = nullptr;
    }
    if (!state.pending.is_set) {
        return;
    }
    expanded_location from = expand_location(linemap_included_from(map));
    if (from.file != nullptr && &classify_file(from.file) == state.pending.place.file) {
        finish_pending_include(&entry);
    }
}

}  // namespace

void finish_pending_include(const FileEntry* entered) {
    if (!state.pending.is_set) {
        return;
    }
    PendingInclude pending = std::move(state.pending);
    state.pending = PendingInclude{};
    check_include(pending, entered != nullptr ? entered : resolve_skipped_include(pending));
}

// Registers the callbacks of libcpp and the hooks of the include rules and of
// the provenance.
void register_includes(const char* plugin_name) {
    cpp_callbacks* callbacks = cpp_get_callbacks(parse_in);
    state.previous_include = callbacks->include;
    callbacks->include = on_include;
    state.previous_used = callbacks->used;
    callbacks->used = on_macro_used;
    register_callback(plugin_name, PLUGIN_INCLUDE_FILE, on_include_file, nullptr);
    register_callback(plugin_name, PLUGIN_START_UNIT, on_start_unit, nullptr);
}

}  // namespace crucible_plugin
