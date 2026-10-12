// The family include closure: the include rules, the door units, the seals,
// the heavy headers, the names of a higher root, the expansions of assert and
// the provenance of each place.
//
// The include rules apply to each #include directive that a base or a
// quarantined file holds.  A base file can include a header of its own layer
// and of each lower layer, and each header outside the root that the allow
// rows give its layer.  An allow row with `until FAMILY` gives such an include
// the pending kind of the header (kPendingKindPrefix and the header), and the
// ratchet counts it.  A door header can have only its door as includer.  A
// file of the language gets the allowance of its own layer and of the layer of
// its language row, whichever is lower.
//
// THE DOOR UNITS (the rule door-unit)
//     A door is one source file, and its unit holds the system declarations of
//     one family.  A source file has the extension .c, .cc, .cpp or .cxx.  An
//     include of a door header by its OWNER gets header-door when the OWNER is
//     no source file, because each unit that includes such an OWNER also gets
//     its system declarations.  A door file is the OWNER of a door row.  Its
//     include closure holds Linux UAPI headers or headers of the C library, and
//     not the two families, because some of their declarations conflict.  The
//     closure is the door file and each file of the root that a file of the
//     closure includes, through each #include directive whose target the
//     plugin knows.  The report reads the directives of the door file first,
//     and then each file of the closure in the sequence that it finds the file.
//     The directive that adds the second family gets mixed-headers, one time
//     for each door file.  Its place is the directive of the door file that
//     starts the path to it.  A UAPI header has a first directory of
//     kUapiDirectories.  A header of the C library is no UAPI header, and the
//     directory of the C library holds it: the first directory of the search
//     chain that holds <features.h>, which each header of glibc includes.  A
//     header of the C++ library or of the compiler is in neither family, and
//     the closure does not go through a header outside the root.  The plugin
//     reads only the arms that a build compiles, as for the seals.
//
// THE SEALS (the rule detsafe-ledger)
//     The include closure of a seal ROOT is ROOT and each file that a file of
//     the closure includes, through each #include directive whose target the
//     plugin knows.  The closure holds no file under a PREFIX of the row
//     (sealed-include).  No file of the closure declares the namespace of a
//     PREFIX, and no file of the closure spells a reference to a declaration
//     of that namespace (sealed-name).  The report judges each seal one time,
//     when the unit knows each directive and each reference.  The sentinel
//     unit of each header of include/ compiles the header alone, so the
//     namespace of a PREFIX exists there only when the closure declares it.
//     The plugin reads only the arms that a build compiles.  An #include or a
//     name in another arm, and a name in a macro body that no unit expands, is
//     no finding (decision 5 of misc/01_10_2026_quarantine.md).
//
// THE HEAVY HEADERS (the rule heavy-include)
//     A heavy row gives a header outside the root whose compile alone costs
//     more than the limit of the rule heavy-include of utils/registry.txt, and
//     a light row gives such a header that costs no more.  `loop derive
//     header-costs` writes the two kinds of row from a measure.  A file under
//     include/ that includes a header with a heavy row gets heavy-include, and
//     one that includes a header outside the root with no row of either kind
//     gets unmeasured-header: measure it.  A base file gets these findings
//     beside the findings of the include rules, so an allow row does not make
//     a heavy header light.  A table with no heavy row and no light row, such
//     as the table of a plant of a different rule, gives no such finding, and
//     the rule heavy-include refuses such a table of the tree.
//
// THE NAMES OF A HIGHER ROOT (the rule layer-boundary)
//     The project roots are the directories under include/ of the layer rows
//     and of the quarantine rows.  The rank of a root is the lowest rank of a
//     layer row of the root, and a root of a quarantine row is above each
//     layer.  A base file names only a root of its own rank or of a lower
//     rank.  So a base file does not open the namespace of a higher root first
//     in a unit (upward-name), and the code does not spell a reference to a
//     declaration of a higher root in a base file.  The sentinel unit of each
//     base header and the unit of each base source file open no namespace of a
//     higher root before a base file does, when the base file opens one.
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

// The prefix of the path of a file that the rule heavy-include reads.
constexpr const char* kHeavyScope = "include/";

// The heavy headers: a file under include/ that includes a header outside the
// root (THE HEAVY HEADERS).  A target that the plugin cannot find is a header
// outside the root too.
void check_header_cost(const PendingInclude& pending, const FileEntry* target) {
    const FileEntry& includer = *pending.place.file;
    if (state.table.header_costs.empty() || !has_prefix(includer.relative, kHeavyScope)
        || (target != nullptr && target->file_class != FileClass::outside)) {
        return;
    }
    std::string header = "<" + pending.spelled + ">";
    auto found = state.table.header_costs.find(pending.spelled);
    if (found == state.table.header_costs.end()) {
        add_finding(Kind::unmeasured_header, nullptr, pending.place, header);
    } else if (found->second == HeaderCost::heavy) {
        add_finding(Kind::heavy_include, nullptr, pending.place, header);
    }
}

// ── The door units (THE DOOR UNITS) ─────────────────────────────────────

// The top directories of the Linux UAPI headers that `make headers_install`
// writes.  glibc writes scsi/ too, so scsi/ is no UAPI directory here.
constexpr std::string_view kUapiDirectories[] = {"asm", "asm-generic", "drm", "linux", "misc",
                                                 "mtd", "rdma",        "sound", "video", "xen"};

// The family of a system header (THE DOOR UNITS).
enum class HeaderFamily : std::uint8_t {
    other,
    uapi,
    c_library,
};

// A directive of a file of the root that includes a system header of a family.
struct FamilyInclude {
    const FileEntry* from = nullptr;
    Place directive;
    HeaderFamily family = HeaderFamily::other;
    std::string header;  // <NAME>, as the directive spells it
};

// Each directive of the unit that includes a system header of a family, in
// the sequence of the preprocessor.
std::vector<FamilyInclude> family_includes;

// True when PATH has the extension of a source file.
bool is_source_path(const std::string& path) {
    for (std::string_view extension : {".c", ".cc", ".cpp", ".cxx"}) {
        if (path.size() > extension.size()
            && path.compare(path.size() - extension.size(), extension.size(), extension) == 0) {
            return true;
        }
    }
    return false;
}

// True when a door row names FILE as its OWNER.  Complexity: O(door rows).
bool is_door_file(const FileEntry& file) {
    for (const DoorRow& door : state.table.doors) {
        if (door.owner == file.relative) {
            return true;
        }
    }
    return false;
}

// The real path of the directory of the C library: the first directory of the
// search chain of an angle include that holds <features.h>, or empty.  The
// chain of a unit does not change, so the plugin finds the directory one time.
// Complexity: O(directories of the chain), one time.
const std::string& c_library_directory() {
    static const std::string directory = [] {
        for (cpp_dir* entry = get_added_cpp_dirs(INC_BRACKET); entry != nullptr; entry = entry->next) {
            if (!real_path((std::string(entry->name) + "/features.h").c_str()).empty()) {
                return real_path(entry->name);
            }
        }
        return std::string{};
    }();
    return directory;
}

// The family of the header of a directive, whose TARGET is the file that it
// enters, or null.
HeaderFamily header_family(const PendingInclude& pending, const FileEntry* target) {
    std::string_view spelled = pending.spelled;
    std::string_view first = spelled.substr(0, spelled.find('/'));
    bool has_directory = first.size() < spelled.size();
    for (std::string_view directory : kUapiDirectories) {
        if (has_directory && first == directory) {
            return HeaderFamily::uapi;
        }
    }
    const std::string& library = c_library_directory();
    bool is_c_library = target != nullptr && !library.empty() && has_prefix(target->real, library + "/");
    return is_c_library ? HeaderFamily::c_library : HeaderFamily::other;
}

// Keeps a directive of a file of the root that includes a system header of a
// family, for the report (THE DOOR UNITS).  The report reads the include
// closure of each door file when the unit knows each directive.
void note_family_include(const PendingInclude& pending, const FileEntry* target) {
    if (state.table.doors.empty()) {
        return;
    }
    HeaderFamily family = header_family(pending, target);
    if (family != HeaderFamily::other) {
        family_includes.push_back(FamilyInclude{pending.place.file, pending.place, family, "<" + pending.spelled + ">"});
    }
}

// The include rules for one directive and its target.
void check_include(const PendingInclude& pending, const FileEntry* target) {
    const FileEntry& includer = *pending.place.file;
    check_header_cost(pending, target);
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
    note_family_include(pending, target);
    for (const DoorRow& door : state.table.doors) {
        if (door.header == pending.spelled) {
            if (includer.relative != door.owner) {
                add_finding(Kind::door_header, nullptr, pending.place,
                            "<" + pending.spelled + "> has the door " + door.owner);
            } else if (!is_source_path(door.owner)) {
                add_finding(Kind::header_door, nullptr, pending.place, "<" + pending.spelled + ">");
            } else if (target != nullptr) {
                // A source file that owns the door admits the declarations
                // that the include brings in (THE DOOR ADMISSION of plugin.h).
                state.door_entries[&includer].push_back(target);
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
    // An allow row with no `until` admits the include.  A row with `until`
    // gives it the pending kind of the header, and no enforce mode of the
    // scope include makes that kind an error.
    const AllowRow* pending_row = nullptr;
    for (const AllowRow& allow : state.table.allows) {
        if (allow.header != pending.spelled || layer_rank(allow.layer) > allowed_rank) {
            continue;
        }
        if (allow.pending.until.empty()) {
            return;
        }
        pending_row = &allow;
    }
    if (pending_row != nullptr) {
        add_finding(Kind::replace_pending, &pending_row->pending, pending.place, "<" + pending.spelled + ">");
        return;
    }
    add_finding(Kind::layer_header, nullptr, pending.place, "<" + pending.spelled + "> in " + allowance);
}

// True when a door row names the main file of the unit as its owner (THE DOOR
// ADMISSION of plugin.h).  Complexity: O(door rows), one time.
bool is_door_unit() {
    if (!state.is_door_unit_known && main_input_filename != nullptr) {
        state.is_door_unit_known = true;
        const FileEntry& main_file = classify_file(main_input_filename);
        for (const DoorRow& door : state.table.doors) {
            state.is_door_unit = state.is_door_unit || (door.owner == main_file.relative && is_source_path(door.owner));
        }
    }
    return state.is_door_unit;
}

// A directive of a header of the C library in a door unit.  The door admission
// resolves it only when it builds the closure of a door header, so a door
// unit pays one copy of the name here, and a different unit pays nothing.
void note_library_include(location_t location, const char* name, bool is_angle, const unsigned char* directive) {
    if (name == nullptr || !is_door_unit()) {
        return;
    }
    expanded_location at = expand_location(location);
    const std::string& library = c_library_directory();
    if (at.file == nullptr || library.empty()) {
        return;
    }
    const FileEntry& includer = classify_file(at.file);
    if (!has_prefix(includer.real, library + "/")) {
        return;
    }
    LibraryInclude include;
    include.directive.is_set = true;
    include.directive.place.file = &includer;
    include.directive.spelled = name;
    include.directive.is_angle = is_angle;
    include.directive.is_next =
        directive != nullptr && std::strcmp(reinterpret_cast<const char*>(directive), "include_next") == 0;
    state.library_includes[&includer].push_back(std::move(include));
    ++state.library_include_count;
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
        note_library_include(location, name, is_angle != 0, directive);
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
        if (state.is_main_started) {
            note_main_started();
        }
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
    const FileEntry* target = entered != nullptr ? entered : resolve_skipped_include(pending);
    if (target != nullptr && (!state.table.seals.empty() || !state.table.doors.empty())) {
        state.include_edges.push_back(IncludeEdge{pending.place.file, target, pending.place});
    }
    check_include(pending, target);
}

namespace {

// True when FILE is the header of a door row whose owner is not DOOR_FILE: a
// header of the C library that a different door owns, such as <unistd.h>,
// which <signal.h> includes for SIGSTKSZ (THE DOOR ADMISSION of plugin.h).
// Complexity: O(door rows).
bool is_header_of_other_door(const FileEntry& file, const FileEntry& door_file) {
    const std::string& library = c_library_directory();
    if (library.empty() || !has_prefix(file.real, library + "/")) {
        return false;
    }
    std::string spelled = file.real.substr(library.size() + 1);
    for (const DoorRow& door : state.table.doors) {
        if (door.header == spelled && door.owner != door_file.relative) {
            return true;
        }
    }
    return false;
}

// The include closure of the door headers ENTRIES of DOOR_FILE (THE DOOR
// ADMISSION of plugin.h): each header, and each file that a directive of a
// file of the closure names.  The walk resolves a directive as libcpp does,
// also a directive that an include guard stopped, and it does not go into a
// header of a different door.  libstdc++ wraps some headers of the C
// library, such as <stdlib.h>: the directive of the door file enters the
// wrapper, and the wrapper enters the header of the C library through a
// header of libstdc++.  So the closure also starts at the header of the C
// library that each door row of DOOR_FILE names, when the unit entered it.
// The closure keeps the count of the directives that it read, and a new
// directive builds it again.  Complexity: O(the directives of the closure
// times the search chain), one time for each count.
const std::unordered_set<const FileEntry*>& door_closure(const FileEntry& door_file,
                                                         const std::vector<const FileEntry*>& entries) {
    DoorClosure& closure = state.door_closures[&door_file];
    if (closure.is_built && closure.include_count == state.library_include_count) {
        return closure.files;
    }
    closure.files.clear();
    std::vector<const FileEntry*> frontier;
    for (const FileEntry* entry : entries) {
        if (closure.files.insert(entry).second) {
            frontier.push_back(entry);
        }
    }
    const std::string& library = c_library_directory();
    for (const DoorRow& door : state.table.doors) {
        if (library.empty() || door.owner != door_file.relative) {
            continue;
        }
        auto wrapped = state.entries.find(library + "/" + door.header);
        if (wrapped != state.entries.end() && closure.files.insert(&wrapped->second).second) {
            frontier.push_back(&wrapped->second);
        }
    }
    while (!frontier.empty()) {
        const FileEntry* file = frontier.back();
        frontier.pop_back();
        auto directives = state.library_includes.find(file);
        if (directives == state.library_includes.end()) {
            continue;
        }
        for (LibraryInclude& include : directives->second) {
            if (!include.is_resolved) {
                include.target = resolve_skipped_include(include.directive);
                include.is_resolved = true;
            }
            const FileEntry* target = include.target;
            if (target != nullptr && !is_header_of_other_door(*target, door_file)
                && closure.files.insert(target).second) {
                frontier.push_back(target);
            }
        }
    }
    closure.include_count = state.library_include_count;
    closure.is_built = true;
    return closure.files;
}

}  // namespace

bool is_door_admitted(location_t location, tree decl) {
    // The C++ front end reads each token of the unit before it parses one, so
    // the last directive of the unit is still pending when its target entered
    // no file.  Its target goes into the door entries here, and not first in
    // the report of the unit.
    finish_pending_include(nullptr);
    if (decl == NULL_TREE || !DECL_P(decl) || state.door_entries.empty()) {
        return false;
    }
    Place place = place_of(location, Scope::quarantine);
    auto entries = place.file != nullptr ? state.door_entries.find(place.file) : state.door_entries.end();
    if (entries == state.door_entries.end()) {
        return false;
    }
    const line_map_ordinary* map = nullptr;
    linemap_resolve_location(line_table, DECL_SOURCE_LOCATION(decl), LRK_SPELLING_LOCATION, &map);
    if (map == nullptr) {
        return false;
    }
    const FileEntry& declared = classify_file(ORDINARY_MAP_FILE_NAME(map));
    return door_closure(*place.file, entries->second).count(&declared) != 0;
}

namespace {

// ── The project roots (THE NAMES OF A HIGHER ROOT) ──────────────────────

// The rank of a root of a quarantine row, above each layer.
constexpr int kTopRank = INT_MAX;

// The project root of a path of the table or of a file: the directory after
// the first directory, such as fixy for include/fixy/Fn.h or src/fixy/os/, or
// "" for a path with no such directory.
std::string root_of_path(const std::string& relative) {
    std::size_t first = relative.find('/');
    if (first == std::string::npos) {
        return {};
    }
    std::size_t second = relative.find('/', first + 1);
    return second == std::string::npos ? std::string{} : relative.substr(first + 1, second - first - 1);
}

// The rank of each project root, from the rule table.  The table of a unit
// does not change, so the map is made one time.  Complexity: O(paths of the
// layer rows and the quarantine rows).
const std::unordered_map<std::string, int>& root_ranks() {
    static const std::unordered_map<std::string, int> ranks = [] {
        std::unordered_map<std::string, int> made;
        for (const LayerRow& layer : state.table.layers) {
            for (const std::string& path : layer.paths) {
                std::string root = has_prefix(path, "include/") || has_prefix(path, "src/") ? root_of_path(path)
                                                                                           : std::string{};
                if (root.empty()) {
                    continue;
                }
                auto [entry, is_new] = made.emplace(root, layer.rank);
                if (!is_new && layer.rank < entry->second) {
                    entry->second = layer.rank;
                }
            }
        }
        for (const std::string& path : state.table.quarantines) {
            std::string root = has_prefix(path, "include/") ? root_of_path(path) : std::string{};
            if (!root.empty()) {
                made.emplace(root, kTopRank);
            }
        }
        return made;
    }();
    return ranks;
}

// The rank of a project root, or -1 for a name that is no project root.
int rank_of_root(const std::string& root) {
    auto found = root_ranks().find(root);
    return found != root_ranks().end() ? found->second : -1;
}

// The lowest rank of a project root, or kTopRank for a table with no root.
// A declaration of a root of this rank is never the name of a higher root.
int lowest_root_rank() {
    static const int lowest = [] {
        int made = kTopRank;
        for (const auto& [root, rank] : root_ranks()) {
            made = std::min(made, rank);
        }
        return made;
    }();
    return lowest;
}

// The namespaces of the seal rows, each one time, such as {crucible, ledger}.
// The table of a unit does not change, so the list is made one time.
const std::vector<std::vector<std::string>>& sealed_namespaces() {
    static const std::vector<std::vector<std::string>> namespaces = [] {
        std::vector<std::vector<std::string>> made;
        for (const SealRow& seal : state.table.seals) {
            for (const std::vector<std::string>& names : seal.namespaces) {
                if (std::find(made.begin(), made.end(), names) == made.end()) {
                    made.push_back(names);
                }
            }
        }
        return made;
    }();
    return namespaces;
}

// The facts of a namespace at the top of the global namespace, for the
// references to its declarations: the rank of its root, and whether a seal row
// names a namespace in it.  The cache holds the namespace node, and the
// collection hook of plugin.cpp clears it.  Complexity: O(1) for a namespace
// that the cache holds.
const TopFacts& facts_of_top(tree top) {
    auto found = state.top_facts.find(top);
    if (found != state.top_facts.end()) {
        return found->second;
    }
    std::string root = simple_name(top);
    TopFacts facts;
    facts.rank = rank_of_root(root);
    for (const std::vector<std::string>& names : sealed_namespaces()) {
        facts.is_sealed_root = facts.is_sealed_root || names.front() == root;
    }
    return state.top_facts.emplace(top, facts).first->second;
}

// True when the namespaces of DECL, outermost first and with no inline
// namespace, start with the names of a namespace of a seal row.  This is the
// test is_in_namespace(namespace_of(DECL), the names of the row) with no text.
// Complexity: O(depth of the scopes).
bool is_in_sealed_namespace(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && TREE_CODE(current) != NAMESPACE_DECL && depth < kTypeDepth; ++depth) {
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                  : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                    : NULL_TREE;
    }
    // The names of the namespaces, innermost first.  A namespace with no name
    // gives null, which no name of a seal row equals.
    std::array<const char*, kTypeDepth> names{};
    std::size_t count = 0;
    for (tree scope = current; scope != NULL_TREE && scope != global_namespace && count < names.size();
         scope = CP_DECL_CONTEXT(scope)) {
        if (TREE_CODE(scope) != NAMESPACE_DECL) {
            return false;
        }
        if (!DECL_NAMESPACE_INLINE_P(scope)) {
            names[count++] = DECL_NAME(scope) != NULL_TREE ? IDENTIFIER_POINTER(DECL_NAME(scope)) : nullptr;
        }
    }
    for (const std::vector<std::string>& sealed : sealed_namespaces()) {
        bool is_prefix = sealed.size() <= count;
        for (std::size_t index = 0; is_prefix && index < sealed.size(); ++index) {
            const char* name = names[count - 1 - index];
            is_prefix = name != nullptr && sealed[index] == name;
        }
        if (is_prefix) {
            return true;
        }
    }
    return false;
}

// The namespaces of DECL, outermost first, with no inline namespace: the
// namespace that holds DECL and each namespace around it.  A namespace gives
// itself too.  Complexity: O(depth of the scopes).
std::string namespace_of(tree decl) {
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && TREE_CODE(current) != NAMESPACE_DECL && depth < kTypeDepth; ++depth) {
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                  : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                    : NULL_TREE;
    }
    return current == NULL_TREE || current == global_namespace ? std::string{} : qualified_name(current);
}

// The text of the names of a namespace of a seal row, such as crucible::ledger.
std::string joined_names(const std::vector<std::string>& names) {
    std::string text;
    for (const std::string& name : names) {
        text += text.empty() ? name : "::" + name;
    }
    return text;
}

// True when the namespace SCOPE is the namespace NAMES or a namespace in it.
bool is_in_namespace(const std::string& scope, const std::string& names) {
    return scope == names || has_prefix(scope, names + "::");
}

// True when the source spells the name of DECL, or the name of a class or a
// namespace around it, at the start of LOCATION.  The walk gives a reference
// the location of the expression that holds it, so an implicit call, such as
// a destructor, and a name of a macro argument that an expression of the
// macro body holds have a location where the source spells a different name.
// Complexity: O(depth of the scopes).
bool is_named_at(tree decl, location_t location) {
    std::string spelled = identifier_spelled_at(location);
    if (spelled.empty()) {
        return false;
    }
    tree current = decl;
    for (int depth = 0; current != NULL_TREE && current != global_namespace && depth < kTypeDepth; ++depth) {
        tree named = TYPE_P(current) ? TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(current)) : current;
        if (named != NULL_TREE && DECL_P(named) && DECL_NAME(named) != NULL_TREE
            && spelled == IDENTIFIER_POINTER(DECL_NAME(named))) {
            return true;
        }
        current = TYPE_P(current) ? CP_TYPE_CONTEXT(TYPE_MAIN_VARIANT(current))
                  : DECL_P(current) ? CP_DECL_CONTEXT(current)
                                    : NULL_TREE;
    }
    return false;
}

// The place of a declaration: the file of the macro expansion point of its
// location, so a declaration that a macro of a base file makes in a different
// file is a declaration of that different file.
Place declaration_place(tree decl) {
    location_t point = linemap_resolve_location(line_table, DECL_SOURCE_LOCATION(decl),
                                                LRK_MACRO_EXPANSION_POINT, nullptr);
    return place_of(point, Scope::quarantine);
}

// The namespace NAMES of the unit, or null when the unit holds no such
// namespace.  A qualified lookup finds a namespace through an inline
// namespace, and an alias gives its namespace.
tree find_namespace(const std::vector<std::string>& names) {
    tree current = global_namespace;
    for (const std::string& name : names) {
        tree found = lookup_qualified_name(current, get_identifier(name.c_str()), LOOK_want::NAMESPACE, false);
        if (found == NULL_TREE || found == error_mark_node || TREE_CODE(found) != NAMESPACE_DECL) {
            return NULL_TREE;
        }
        current = ORIGINAL_NAMESPACE(found);
    }
    return current;
}

// The directives of each file of the unit, for the include closures.
using EdgesOf = std::unordered_map<const FileEntry*, std::vector<const IncludeEdge*>>;

// The include closure of the file ROOT in the unit (THE SEALS), and a
// sealed-include finding for each directive of the closure whose target is
// under a PREFIX of SEAL.  The walk does not enter a sealed file.
// Complexity: O(edges of the unit).
std::unordered_set<const FileEntry*> sealed_closure(const SealRow& seal, const FileEntry* root,
                                                   const EdgesOf& edges_of) {
    std::unordered_set<const FileEntry*> closure{root};
    std::vector<const FileEntry*> queue{root};
    while (!queue.empty()) {
        const FileEntry* current = queue.back();
        queue.pop_back();
        auto found = edges_of.find(current);
        if (found == edges_of.end()) {
            continue;
        }
        for (const IncludeEdge* edge : found->second) {
            const std::string& target = edge->to->relative;
            if (target.empty()) {
                continue;
            }
            bool is_sealed = false;
            for (const std::string& prefix : seal.prefixes) {
                is_sealed = is_sealed || has_prefix(target, prefix);
            }
            if (is_sealed) {
                add_finding(Kind::sealed_include, nullptr, edge->directive,
                            target + " in the include closure of " + seal.root);
            } else if (closure.insert(edge->to).second) {
                queue.push_back(edge->to);
            }
        }
    }
    return closure;
}

// The sealed-name findings of SEAL: the first opening of a sealed namespace in
// a file of the closure, and each reference that a file of the closure spells.
void check_sealed_names(const SealRow& seal, const std::unordered_set<const FileEntry*>& closure) {
    for (const std::vector<std::string>& names : seal.namespaces) {
        std::string text = joined_names(names);
        if (tree sealed = find_namespace(names)) {
            Place place = declaration_place(sealed);
            if (place.file != nullptr && closure.count(place.file) != 0) {
                add_finding(Kind::sealed_name, nullptr, place,
                            "the namespace " + text + " in the include closure of " + seal.root);
            }
        }
        for (const SealedReference& reference : state.sealed_references) {
            if (closure.count(reference.place.file) != 0 && is_in_namespace(reference.scope, text)) {
                add_finding(Kind::sealed_name, nullptr, reference.place,
                            reference.entity + " in the include closure of " + seal.root);
            }
        }
    }
}

// The first opening of the namespace of each root above the lowest root, when
// a base file of a lower root opens it (THE NAMES OF A HIGHER ROOT).  The roots
// are read in the order of their names, so the report has one order.
void check_upward_namespaces() {
    std::vector<std::pair<std::string, int>> roots(root_ranks().begin(), root_ranks().end());
    std::sort(roots.begin(), roots.end());
    for (const auto& [root, rank] : roots) {
        tree opened = find_namespace({root});
        if (opened == NULL_TREE) {
            continue;
        }
        Place place = declaration_place(opened);
        if (place.file_class != FileClass::base) {
            continue;
        }
        int file_rank = rank_of_root(root_of_path(place.file->relative));
        if (file_rank >= 0 && file_rank < rank) {
            add_finding(Kind::upward_name, nullptr, place,
                        "the namespace " + root + " from the root " + root_of_path(place.file->relative));
        }
    }
}

// The directives of each file of the unit that include a system header of a
// family, for the door units.
using FamiliesOf = std::unordered_map<const FileEntry*, std::vector<const FamilyInclude*>>;

// The families of the include closure of the door file DOOR (THE DOOR UNITS).
// The walk reads the directives of DOOR first, and then each file of the
// closure in the sequence that it finds the file.  The first directive that
// adds the second family gets mixed-headers.  Its place is that directive when
// DOOR holds it, else the directive of DOOR that starts the path to it.
// Complexity: O(edges and family includes of the unit).
void check_door_closure(const FileEntry* door, const EdgesOf& edges_of, const FamiliesOf& families_of) {
    std::string first_uapi;
    std::string first_c_library;
    std::unordered_set<const FileEntry*> closure{door};
    // Each file of the closure, with the directive of DOOR that starts the
    // path to it, or null for DOOR.
    std::vector<std::pair<const FileEntry*, const Place*>> queue{{door, nullptr}};
    for (std::size_t index = 0; index < queue.size(); ++index) {
        auto [current, start] = queue[index];
        auto families = families_of.find(current);
        if (families != families_of.end()) {
            for (const FamilyInclude* include : families->second) {
                std::string& first = include->family == HeaderFamily::uapi ? first_uapi : first_c_library;
                if (first.empty()) {
                    first = include->header;
                }
                if (!first_uapi.empty() && !first_c_library.empty()) {
                    add_finding(Kind::mixed_headers, nullptr, start != nullptr ? *start : include->directive,
                                first_uapi + " and " + first_c_library);
                    return;
                }
            }
        }
        auto edges = edges_of.find(current);
        if (edges == edges_of.end()) {
            continue;
        }
        for (const IncludeEdge* edge : edges->second) {
            if (!edge->to->relative.empty() && closure.insert(edge->to).second) {
                queue.emplace_back(edge->to, start != nullptr ? start : &edge->directive);
            }
        }
    }
}

}  // namespace

// The findings of the door units that the report judges when the unit knows
// each directive: mixed-headers in the include closure of each door file of
// the unit (THE DOOR UNITS).  Complexity: O(door files * (edges + family
// includes)) and O(files * door rows) for the door files.
void report_door_units() {
    if (state.table.doors.empty() || family_includes.size() < 2) {
        return;
    }
    EdgesOf edges_of;
    std::unordered_set<const FileEntry*> checked;
    std::vector<const FileEntry*> doors;
    auto note_door = [&checked, &doors](const FileEntry* file) {
        if (checked.insert(file).second && is_door_file(*file)) {
            doors.push_back(file);
        }
    };
    for (const IncludeEdge& edge : state.include_edges) {
        edges_of[edge.from].push_back(&edge);
        note_door(edge.from);
    }
    FamiliesOf families_of;
    for (const FamilyInclude& include : family_includes) {
        families_of[include.from].push_back(&include);
        note_door(include.from);
    }
    for (const FileEntry* door : doors) {
        check_door_closure(door, edges_of, families_of);
    }
}

// One reference of the code to the declaration or the namespace NAMED at
// LOCATION, from the walk of a body or of a declaration.  The reference
// counts only when the source spells a name of it at LOCATION (is_named_at).
// The walk reads no body of a template instantiation, so a name that only an
// instantiation resolves is no reference of the source.  A reference to a
// declaration of a sealed namespace waits for the report (THE SEALS).  A
// reference that a base file spells to a declaration of a higher root is an
// upward-name (THE NAMES OF A HIGHER ROOT).  The plugin reads the place and
// the source text of a reference only when one of the two rules can judge it.
// Complexity: O(depth of the scopes) for a declaration of a project root, and
// O(1) for each other.
void note_project_reference(tree named, location_t location) {
    if (named == NULL_TREE || location <= BUILTINS_LOCATION) {
        return;
    }
    tree decl = named;
    if (TREE_CODE(decl) == BASELINK) {
        decl = BASELINK_FUNCTIONS(decl);
    }
    if (TREE_CODE(decl) == OVERLOAD) {
        decl = OVL_FIRST(decl);
    }
    if (TYPE_P(decl)) {
        decl = TYPE_MAIN_DECL(TYPE_MAIN_VARIANT(decl));
    }
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return;
    }
    tree top = top_namespace(decl);
    if (top == global_namespace || top == std_node) {
        return;
    }
    const TopFacts& facts = facts_of_top(top);
    // A reference waits for the seals only when its declaration is in a sealed
    // namespace, and it can be an upward-name only when a root has a lower
    // rank.  Each other reference stops here, before the place and the text.
    bool is_sealed = facts.is_sealed_root && is_in_sealed_namespace(decl);
    bool can_be_upward = facts.rank > lowest_root_rank();
    if (!is_sealed && !can_be_upward) {
        return;
    }
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr) {
        return;
    }
    int file_rank = place.file_class == FileClass::base ? rank_of_root(root_of_path(place.file->relative)) : -1;
    bool is_upward = file_rank >= 0 && file_rank < facts.rank;
    if ((!is_sealed && !is_upward) || !is_named_at(decl, location)) {
        return;
    }
    std::string entity = TREE_CODE(decl) == NAMESPACE_DECL ? "the namespace " + qualified_name(decl)
                                                           : qualified_name(decl);
    if (is_sealed) {
        state.sealed_references.push_back(SealedReference{place, namespace_of(decl), entity});
    }
    if (is_upward) {
        add_finding(Kind::upward_name, nullptr, place, entity + " from the root " + root_of_path(place.file->relative));
    }
}

// The findings of the include closure that the report judges when the unit
// knows each directive and each reference: the seals and the namespaces of a
// higher root.  Complexity: O(seals * edges) and O(roots).
void report_include_closure() {
    check_upward_namespaces();
    if (state.table.seals.empty()) {
        return;
    }
    std::unordered_map<std::string, const FileEntry*> by_relative;
    for (const auto& [real, entry] : state.entries) {
        if (!entry.relative.empty()) {
            by_relative.emplace(entry.relative, &entry);
        }
    }
    EdgesOf edges_of;
    for (const IncludeEdge& edge : state.include_edges) {
        edges_of[edge.from].push_back(&edge);
    }
    for (const SealRow& seal : state.table.seals) {
        auto root = by_relative.find(seal.root);
        if (root != by_relative.end()) {
            check_sealed_names(seal, sealed_closure(seal, root->second, edges_of));
        }
    }
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
