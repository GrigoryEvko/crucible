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
//     field NAME MEMBER OFFSET   the offset of one member of that struct
//     constant NAME VALUE        one enumerator of the C ABI that an enum of
//                                Python mirrors, such as CRUCIBLE_ACTION_RECORD
//   The loader of crucible_native.py reads the table before it loads a
//   library.  It refuses a library whose note differs from the digest, a
//   ctypes Structure whose size or field offsets differ from the table, and
//   an enum whose values differ from the constants.
//
// The tool compiles layout_note.cpp, so the digest of the table and the
// digest of each note come from one text.  The walk of the members of a C
// struct reads the layout of a foreign ABI by reflection, so it is in an ABI
// region.

#include "layout_note.h"

#include "vessel_api.h"

#include <fixy/Core.h>
#include <foundation/Quarantine.h>

#include <cstddef>
#include <cstdint>
#include <meta>

namespace {

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the tool reads the layout of the C structs that ctypes mirrors")

// The lines of one struct of the C ABI: its size, and the offset of each
// member in the order of the declarations.  A C struct has only public
// members, so the current context sees each of them.
template <class Struct>
void write_struct(::fixy::TextView name) noexcept {
    ::fixy::report(::fixy::Sink::Out, "struct {} {}\n", name, sizeof(Struct));
    static constexpr auto members =
        std::define_static_array(std::meta::nonstatic_data_members_of(^^Struct, std::meta::access_context::current()));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr std::meta::info member : members) {
        constexpr auto identifier = std::meta::identifier_of(member);
        ::fixy::report(::fixy::Sink::Out, "field {} {} {}\n", name,
                       ::fixy::mint_foreign_text(identifier.data(), identifier.size()).characters(),
                       static_cast<std::size_t>(std::meta::offset_of(member).bytes));
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
    return 0;
}
