// The family library code (the rule library-code): the code of a library in
// each function that the unit compiles, and each symbol that the code of the
// tree names.
//
// THE PASS
//     An RTL pass after the pass shorten reads each instruction of each
//     function that the unit compiles.  After shorten, no pass moves code
//     between functions, and each instruction keeps the location block of the
//     body that it comes from.  The report writes the records at
//     PLUGIN_FINISH_UNIT, after the last function (THE SECTION of plugin.cpp).
//     A parse compiles no function, so a parse build has no record of the
//     family.  The plugin registers the pass only for the argument
//     library-code=on (THE ARGUMENTS of plugin.cpp).
//
// THE FRAMES
//     The location block of an instruction and its outer blocks give the
//     frames of the instruction: one frame for each inlined body, from the
//     innermost, and the function itself last.  The origin of a frame is the
//     declaration of its function (block_ultimate_origin).  A frame is library
//     code when its origin is declared in a library file (in_library_file_at of
//     plugin.h), such as a header of libstdc++ or of the C library.  Each other
//     frame is code of the tree.  The innermost frame owns the instruction.
//
// THE RECORDS
//     Each record is a line of the rule library-code and no finding
//     (is_record_kind of plugin.h).  Its place is in a base or a quarantined
//     file:
//       inlined    a frame of library code whose caller frame is code of the
//                  tree.  The place is the call site in the caller, and the
//                  entity is the name of the library function (code_name)
//       call       an instruction that a frame of the tree owns and that calls
//                  a library function.  The place is the instruction, and the
//                  entity is the symbol and the name of the function
//       reference  an instruction that a frame of the tree owns and that names
//                  a library variable or the address of a library function.
//                  The entity is the symbol and the name
//       symbol     each other symbol that the code of the tree can import
//                  (THE SYMBOLS).  The entity is the symbol.
//     The rule library-code counts the records inlined, call and reference of
//     the base files.  The rule artifact-symbols reads the symbols of the
//     records in the section of each member of a static library, and it
//     explains each import of the member with them (THE HIDDEN IMPORTS of
//     utils/assay/guard/engines/elf/artifact_symbols.py).
//
// THE SYMBOLS
//     A symbol is a SYMBOL_REF of an instruction, with its assembler name.  The
//     declaration of the symbol decides its record:
//       * No declaration, or an artificial external declaration, such as a
//         builtin of GCC (memcpy, __cxa_atexit), __cxa_guard_acquire, a vtable
//         or a typeinfo: the compiler adds the symbol, and the record is
//         symbol.  The front end gives such a declaration the location of its
//         first use, also in a file of the tree, so its location says nothing.
//       * A declaration of a library file: a record call or reference when a
//         frame of the tree owns the instruction.  Else a record symbol at
//         the call site of the library code in the nearest frame of the tree.
//       * An external declaration of the tree with C language linkage: a
//         foreign symbol, and the record is symbol.
//       * Each other declaration of the tree names code of the tree, and it
//         gets no record.  A constant of the unit, such as the string of
//         .LC0, has an artificial declaration that is not external, and it
//         is code of the tree too.
//     An instruction with no frame of the tree, such as an instruction of a
//     library function that the unit compiles out of line, gives no record.
//     The rule artifact-symbols follows the relocations of the section of such
//     a function.  The symbols of the runtimes of the sanitizers
//     (kSanitizerPrefixes) get no record.  No rule reads them, and an ASan
//     build has one call of its runtime for each access that it checks.
//
// THE PLACE OF A RECORD
//     The place of an instruction is the place of its location.  An
//     instruction with no location takes the place of the declaration of the
//     frame that owns it, and else the main file of the unit at line 0.  A
//     record with no place is dropped.  Each function reads each block,
//     declaration and place one time.

#include "plugin.h"

// The headers of the RTL of GCC need this sequence: emit-rtl.h names
// enum memmodel and the macros of rtl.h, and rtl-iter.h names rtunion.
// clang-format off
#include "tree-pass.h"
#include "context.h"
#include "function.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "rtl-iter.h"
// clang-format on

namespace crucible_plugin {

namespace {

// The name prefixes of the symbols of the runtimes of the sanitizers (THE
// SYMBOLS).
constexpr const char* kSanitizerPrefixes[] = {"__asan_", "__hwasan_", "__lsan_",     "__msan_",
                                              "__tsan_", "__ubsan_",  "__sanitizer_"};

// The class of the declaration of a symbol (THE SYMBOLS).
enum class SymbolClass : std::uint8_t {
    tree_code,  // code of the tree: no record
    compiler,  // added by the compiler: a record symbol
    library,  // a declaration of a library file
    foreign,  // an external declaration of the tree with C language linkage
};

// A frame of library code whose caller frame is code of the tree: its origin,
// the call site in the caller, and the origin of the caller.
struct Boundary {
    tree origin = NULL_TREE;
    location_t site = UNKNOWN_LOCATION;
    tree caller = NULL_TREE;
};

// The frames of one location block (THE FRAMES).
struct Frames {
    std::vector<Boundary> boundaries;
    bool has_tree_frame = false;  // a frame of the tree holds the instruction
    bool is_owner_library = false;  // the innermost frame is library code
    // The origin of the innermost frame of the tree.
    tree tree_origin = NULL_TREE;
    // The call site of the library code in the innermost frame of the tree,
    // when the innermost frame is library code.
    location_t library_site = UNKNOWN_LOCATION;
};

// True when DECL is declared in a library file (THE FRAMES).  The answer of
// each declaration stays for the unit: a DECL_UID is never used again.
bool is_library_decl(tree decl) {
    static std::unordered_map<unsigned, bool> answers;
    if (decl == NULL_TREE || !DECL_P(decl) || DECL_IS_UNDECLARED_BUILTIN(decl)) {
        return false;
    }
    auto found = answers.find(DECL_UID(decl));
    if (found != answers.end()) {
        return found->second;
    }
    bool is_library = in_library_file_at(DECL_SOURCE_LOCATION(decl));
    answers.emplace(DECL_UID(decl), is_library);
    return is_library;
}

bool is_sanitizer_symbol(const char* name) {
    for (const char* prefix : kSanitizerPrefixes) {
        if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
            return true;
        }
    }
    return false;
}

// The class of the declaration of a symbol (THE SYMBOLS).  A builtin of GCC,
// such as memcpy or __cxa_atexit, is an artificial external declaration.  A
// constant of the unit, such as the string of .LC0, has an artificial
// declaration that is not external, and it is code of the tree.
SymbolClass class_of_symbol(tree decl) {
    if (decl == NULL_TREE || (DECL_ARTIFICIAL(decl) && DECL_EXTERNAL(decl))) {
        return SymbolClass::compiler;
    }
    if (is_library_decl(decl)) {
        return SymbolClass::library;
    }
    if (DECL_EXTERNAL(decl) && DECL_EXTERN_C_P(decl)) {
        return SymbolClass::foreign;
    }
    return SymbolClass::tree_code;
}

// The name of a function or a variable without template arguments
// (qualified_name of library.cpp).  A constructor and a destructor take the
// name of their class.
std::string code_name(tree decl) {
    if (TREE_CODE(decl) == FUNCTION_DECL && (DECL_CONSTRUCTOR_P(decl) || DECL_DESTRUCTOR_P(decl))
        && DECL_CONTEXT(decl) != NULL_TREE && TYPE_P(DECL_CONTEXT(decl))) {
        tree owner = TYPE_MAIN_VARIANT(DECL_CONTEXT(decl));
        tree owner_name = TYPE_NAME(owner);
        std::string simple =
            owner_name != NULL_TREE && TREE_CODE(owner_name) == TYPE_DECL ? simple_name(owner_name) : "(unnamed)";
        return qualified_name(owner) + "::" + (DECL_DESTRUCTOR_P(decl) ? "~" : "") + simple;
    }
    return qualified_name(decl);
}

// The frames of the location block BLOCK in the function FUNCTION (THE
// FRAMES).  Complexity: O(depth of the block).
Frames frames_of(tree block, tree function) {
    std::vector<std::pair<tree, location_t>> frames;
    for (tree current = block; current != NULL_TREE && TREE_CODE(current) == BLOCK;
         current = BLOCK_SUPERCONTEXT(current)) {
        if (!inlined_function_outer_scope_p(current)) {
            continue;
        }
        tree origin = block_ultimate_origin(current);
        if (origin != NULL_TREE && TREE_CODE(origin) == FUNCTION_DECL) {
            frames.emplace_back(origin, BLOCK_SOURCE_LOCATION(current));
        }
    }
    frames.emplace_back(function, UNKNOWN_LOCATION);
    Frames result;
    std::vector<bool> is_library(frames.size());
    for (std::size_t index = 0; index < frames.size(); ++index) {
        is_library[index] = is_library_decl(frames[index].first);
    }
    for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
        if (is_library[index] && !is_library[index + 1]) {
            result.boundaries.push_back(Boundary{frames[index].first, frames[index].second, frames[index + 1].first});
        }
    }
    result.is_owner_library = is_library[0];
    for (std::size_t index = 0; index < frames.size(); ++index) {
        if (!is_library[index]) {
            result.has_tree_frame = true;
            result.tree_origin = frames[index].first;
            result.library_site = index > 0 ? frames[index - 1].second : UNKNOWN_LOCATION;
            break;
        }
    }
    return result;
}

// The place of a record at LOCATION, else at the declaration FALLBACK, else in
// the main file of the unit at line 0 (THE PLACE OF A RECORD).
Place record_place(location_t location, tree fallback) {
    Place place = place_of(location, Scope::quarantine);
    if (place.file == nullptr && fallback != NULL_TREE) {
        place = place_of(DECL_SOURCE_LOCATION(fallback), Scope::quarantine);
    }
    if (place.file == nullptr && main_input_filename != nullptr) {
        const FileEntry& entry = classify_file(main_input_filename);
        if (owns_place(entry.file_class, Scope::quarantine)) {
            place.file_class = entry.file_class;
            place.file = &entry;
            place.spelling = location;
        }
    }
    return place;
}

// The key of one record of a function: its kind, the locus of its location,
// and its subject, the name of its symbol or the origin of its frame.  The
// compiler keeps one name string for each symbol.
struct RecordKey {
    Kind kind = Kind::library_symbol;
    location_t locus = UNKNOWN_LOCATION;
    const void* subject = nullptr;

    bool operator==(const RecordKey& other) const {
        return kind == other.kind && locus == other.locus && subject == other.subject;
    }
};

struct RecordKeyHash {
    std::size_t operator()(const RecordKey& key) const {
        std::size_t hash = std::hash<const void*>{}(key.subject);
        hash ^= std::hash<location_t>{}(key.locus) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        return hash ^ static_cast<std::size_t>(key.kind);
    }
};

// The records of one function.  Each record counts one time, and its entity
// is built one time (ENTITY).
struct FunctionRecords {
    std::unordered_map<tree, Frames> frames;
    std::unordered_set<RecordKey, RecordKeyHash> keys;

    template <typename Entity>
    void add(Kind kind, location_t location, tree fallback, const void* subject, Entity&& entity) {
        if (!keys.insert(RecordKey{kind, LOCATION_LOCUS(location), subject}).second) {
            return;
        }
        Place place = record_place(location, fallback);
        if (place.file != nullptr) {
            add_finding(kind, nullptr, place, entity());
        }
    }
};

// The records of the symbols of the instruction INSN, which the frames FRAMES
// hold (THE SYMBOLS).  Complexity: linear in the subexpressions of the pattern.
void read_symbols(rtx_insn* insn, const Frames& frames, FunctionRecords& records) {
    location_t location = frames.is_owner_library ? frames.library_site : INSN_LOCATION(insn);
    subrtx_iterator::array_type array;
    FOR_EACH_SUBRTX(iter, array, PATTERN(insn), ALL) {
        const_rtx expression = *iter;
        if (GET_CODE(expression) != SYMBOL_REF || CONSTANT_POOL_ADDRESS_P(expression)) {
            continue;
        }
        const char* name = XSTR(expression, 0);
        name += name[0] == '*';
        if (is_sanitizer_symbol(name)) {
            continue;
        }
        tree decl = SYMBOL_REF_DECL(expression);
        SymbolClass symbol_class = class_of_symbol(decl);
        if (symbol_class == SymbolClass::tree_code) {
            continue;
        }
        if (symbol_class == SymbolClass::library && !frames.is_owner_library) {
            Kind kind = CALL_P(insn) ? Kind::library_call : Kind::library_reference;
            records.add(kind, location, frames.tree_origin, name,
                        [&] { return std::string(name) + ' ' + code_name(decl); });
        } else {
            records.add(Kind::library_symbol, location, frames.tree_origin, name, [&] { return std::string(name); });
        }
    }
}

const pass_data kLibraryCodePass = {
    RTL_PASS, "crucible_library_code", OPTGROUP_NONE, TV_NONE, 0, 0, 0, 0, 0,
};

// The pass of the family (THE PASS).
struct LibraryCodePass final : rtl_opt_pass {
    explicit LibraryCodePass(gcc::context* context) : rtl_opt_pass(kLibraryCodePass, context) {}

    unsigned int execute(function*) final {
        FunctionRecords records;
        tree function = current_function_decl;
        for (rtx_insn* insn = get_insns(); insn != nullptr; insn = NEXT_INSN(insn)) {
            if (!NONDEBUG_INSN_P(insn)) {
                continue;
            }
            location_t location = INSN_LOCATION(insn);
            tree block = location != UNKNOWN_LOCATION ? LOCATION_BLOCK(location) : NULL_TREE;
            auto found = records.frames.find(block);
            if (found == records.frames.end()) {
                found = records.frames.emplace(block, frames_of(block, function)).first;
                // The boundaries of a block are the same for each of its
                // instructions.
                for (const Boundary& boundary : found->second.boundaries) {
                    records.add(Kind::library_inlined, boundary.site, boundary.caller, boundary.origin,
                                [&boundary] { return code_name(boundary.origin); });
                }
            }
            const Frames& frames = found->second;
            if (frames.has_tree_frame) {
                read_symbols(insn, frames, records);
            }
        }
        return 0;
    }
};

}  // namespace

// Registers the pass of the family after the pass shorten (THE PASS).
void register_library_code(const char* plugin_name) {
    register_pass_info pass_info;
    pass_info.pass = new LibraryCodePass(g);
    pass_info.reference_pass_name = "shorten";
    pass_info.ref_pass_instance_number = 1;
    pass_info.pos_op = PASS_POS_INSERT_AFTER;
    register_callback(plugin_name, PLUGIN_PASS_MANAGER_SETUP, nullptr, &pass_info);
}

}  // namespace crucible_plugin
