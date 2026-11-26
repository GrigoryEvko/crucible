// The host tool crucible_layout_digest: it writes the layout table of the
// build to its standard output, and the build puts the table beside the two
// vessel libraries (lib/crucible_layout_digest.txt).
//
// Table
//   One fact on each line, as words that one space divides:
//     digest DIGEST              the layout digest of the build in decimal,
//                                which the layout note of each library of
//                                the build holds (layout_note.h)
//     struct NAME SIZE           the size of one struct of the C ABI that
//                                ctypes mirrors
//     field NAME MEMBER OFFSET TYPE
//                                the offset and the type of one member of
//                                that struct
//     constant NAME VALUE        one enumerator of the C ABI that an enum of
//                                Python mirrors, such as CRUCIBLE_ACTION_RECORD
//     entry LIBRARY NAME RETURN PARAMETER...
//                                one C entry of vessel_api.h (the library
//                                vessel) or of dispatch_api.h (the library
//                                dispatch): its name, its return type and
//                                the type of each parameter
//   The loader of crucible_native.py reads the table before it loads a
//   library.  It refuses a library whose note differs from the digest, a
//   ctypes Structure whose size, field offsets or field types differ from the
//   table, and an enum whose values differ from the constants.  It declares
//   each C entry that Python calls from its entry line, so no signature is
//   written by hand.
//
// Type words
//   A type is one word: void, b8 for bool, c8 for char, u8 to u64 and i8 to
//   i64 by the size and the sign of an integer, f32 and f64, or the name of a
//   struct.  An array writes [N] after the word of its element.  A pointer
//   writes * before the word of its pointee, and a const type writes const:
//   before the word of the type.  So const char ** is **const:c8.  A typedef
//   writes the word of the type that it names.
//
// The tool compiles layout_note.cpp, so the digest of the table and the
// digest of each note come from one text.  The walk of the members of a C
// struct and of the C entries reads a foreign ABI by reflection, so it is in
// an ABI region.

#include "layout_note.h"

#include "dispatch_api.h"
#include "vessel_api.h"

#include <fixy/Core.h>
#include <foundation/Quarantine.h>
#include <foundation/reflect/Members.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <string_view>
#include <vector>

namespace {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the tool reads the layout of the C structs that ctypes mirrors")

// The text of an identifier that reflection gives.
[[nodiscard]] consteval std::string_view identifier_text(std::meta::info entity) {
    return std::meta::identifier_of(entity);
}

// Writes the type word of Type (the type words above).  The recursion goes
// one step for each pointer, const and array level of the type.
template <std::meta::info Type>
void write_type_word() noexcept {
    constexpr std::meta::info type = std::meta::dealias(Type);
    if constexpr (std::meta::is_const_type(type)) {
        ::fixy::report(::fixy::Sink::Out, "const:");
        write_type_word<std::meta::remove_const(type)>();
    } else if constexpr (std::meta::is_pointer_type(type)) {
        ::fixy::report(::fixy::Sink::Out, "*");
        write_type_word<std::meta::remove_pointer(type)>();
    } else if constexpr (std::meta::is_array_type(type)) {
        write_type_word<std::meta::remove_extent(type)>();
        ::fixy::report(::fixy::Sink::Out, "[{}]", std::meta::extent(type));
    } else if constexpr (type == ^^void) {
        ::fixy::report(::fixy::Sink::Out, "void");
    } else if constexpr (type == ^^bool) {
        ::fixy::report(::fixy::Sink::Out, "b8");
    } else if constexpr (type == ^^char) {
        ::fixy::report(::fixy::Sink::Out, "c8");
    } else if constexpr (std::meta::is_integral_type(type) && std::meta::is_signed_type(type)) {
        ::fixy::report(::fixy::Sink::Out, "i{}", 8 * std::meta::size_of(type));
    } else if constexpr (std::meta::is_integral_type(type)) {
        ::fixy::report(::fixy::Sink::Out, "u{}", 8 * std::meta::size_of(type));
    } else if constexpr (std::meta::is_floating_point_type(type)) {
        ::fixy::report(::fixy::Sink::Out, "f{}", 8 * std::meta::size_of(type));
    } else {
        static_assert(std::meta::is_class_type(type),
                      "a type of the C ABI is a scalar, a pointer, an array or a struct");
        constexpr std::string_view name = identifier_text(type);
        ::fixy::report(::fixy::Sink::Out, "{}", ::fixy::mint_foreign_text(name.data(), name.size()).characters());
    }
}

// The lines of one struct of the C ABI: its size, and the offset and the type
// of each member in the order of the declarations.  A C struct has only
// public members, so the current context sees each of them.
template <class Struct>
void write_struct(::fixy::TextView name) noexcept {
    ::fixy::report(::fixy::Sink::Out, "struct {} {}\n", name, sizeof(Struct));
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Struct, std::meta::access_context::current()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info member : members) {
        constexpr auto identifier = std::meta::identifier_of(member);
        ::fixy::report(::fixy::Sink::Out, "field {} {} {} ", name,
                       ::fixy::mint_foreign_text(identifier.data(), identifier.size()).characters(),
                       static_cast<std::size_t>(std::meta::offset_of(member).bytes));
        write_type_word<std::meta::type_of(member)>();
        ::fixy::report(::fixy::Sink::Out, "\n");
    }
#pragma GCC diagnostic pop
}

// The library of a C entry: vessel for an entry of vessel_api.h, dispatch for
// an entry of dispatch_api.h, and an empty text for an entry of another file.
[[nodiscard]] consteval std::string_view library_of(std::meta::info entry) {
    const std::string_view file = std::meta::source_location_of(entry).file_name();
    if (file.ends_with("/vessel_api.h")) return "vessel";
    if (file.ends_with("/dispatch_api.h")) return "dispatch";
    return {};
}

// The C entries of vessel_api.h and dispatch_api.h, in the order of the
// declarations of the unit: each function of the global namespace with C
// linkage whose name starts with crucible_.  Linear in the members of the
// global namespace.
[[nodiscard]] consteval std::vector<std::meta::info> vessel_c_entries() {
    std::vector<std::meta::info> entries{};
    static_cast<void>(::foundation::reflect::find_member(
        ^^::, std::meta::access_context::current(), [&entries](std::meta::info member) {
          const bool is_entry = std::meta::is_function(member) && std::meta::has_c_language_linkage(member)
                             && identifier_text(member).starts_with("crucible_") && !library_of(member).empty();
          if (is_entry) entries.push_back(member);
          return false;
        }));
    return entries;
}

// The line of each C entry: its library, its name, its return type and the
// type of each parameter.
void write_entries() noexcept {
    static constexpr auto entries = std::define_static_array(vessel_c_entries());
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info entry : entries) {
        constexpr std::string_view library = library_of(entry);
        constexpr std::string_view name = identifier_text(entry);
        ::fixy::report(::fixy::Sink::Out, "entry {} {} ",
                       ::fixy::mint_foreign_text(library.data(), library.size()).characters(),
                       ::fixy::mint_foreign_text(name.data(), name.size()).characters());
        write_type_word<std::meta::return_type_of(std::meta::type_of(entry))>();
        static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(entry));
        template for (constexpr std::meta::info parameter : parameters) {
            ::fixy::report(::fixy::Sink::Out, " ");
            write_type_word<std::meta::type_of(parameter)>();
        }
        ::fixy::report(::fixy::Sink::Out, "\n");
    }
#pragma GCC diagnostic pop
}

// The lines of each enumerator of the unnamed enum of the C ABI that holds
// Member, such as CRUCIBLE_ACTION_REFUSED: its name and its value.
template <auto Member>
void write_constants() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^decltype(Member)));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info enumerator : enumerators) {
        constexpr auto identifier = std::meta::identifier_of(enumerator);
        ::fixy::report(::fixy::Sink::Out, "constant {} {}\n",
                       ::fixy::mint_foreign_text(identifier.data(), identifier.size()).characters(),
                       static_cast<long long>([:enumerator:]));
    }
#pragma GCC diagnostic pop
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace

int main() {
    ::fixy::report(::fixy::Sink::Out, "digest {}\n", crucible::vessel::library_layout_digest());
    write_struct<CrucibleMeta>("CrucibleMeta");
    write_struct<CrucibleDispatchResult>("CrucibleDispatchResult");
    write_struct<CrucibleSlot>("CrucibleSlot");
    write_struct<CrucibleBoundScalar>("CrucibleBoundScalar");
    write_constants<CRUCIBLE_ACTION_REFUSED>();
    write_constants<CRUCIBLE_STATUS_MATCH>();
    write_constants<CRUCIBLE_SCHEMA_NAME_REGISTERED>();
    write_constants<CRUCIBLE_HANDLE_LIVE>();
    write_constants<CRUCIBLE_VIGIL_SLOTS>();
    write_constants<CRUCIBLE_MAX_FFI_SCALARS>();
    write_entries();
    return 0;
}
