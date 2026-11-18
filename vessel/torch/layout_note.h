#pragma once

// The layout note of a vessel library: an ELF note with the layout digest of
// the types and the C entries that the two vessel libraries share.  The two
// libraries compile the header-only Vigil, and each one reads values that the
// other one made.  So a library that a different source compiled reads such a
// value with the wrong offsets.  The loader of the vessel (crucible_native.py)
// reads the note from the file before it loads the library, and refuses a
// library whose digest differs from the digest of the build.
//
// THE NOTE
//   The section .note.crucible holds it beside the level note (THE NOTE of
//   level_note.h): eight words of 32 bits, the size of the name (9), the
//   size of the descriptor (8), the type (2), the name "Crucible" and its
//   zero in three words, and the digest, its low word first.  A note has the
//   alignment 4, and a descriptor of 8 bytes keeps it.
//
// THE DIGEST
//   layout_note.cpp folds the layout digest (layout_digest_of of
//   foundation/reflect/Hash.h) of each shared type, and the digest of the C
//   entries of vessel_api.h and dispatch_api.h (c_entry_digest).  Each vessel
//   library compiles that unit, and the host tool crucible_layout_digest
//   compiles it too and writes the digest of the build into the layout table
//   beside the libraries (layout_table.cpp).

#include <cstddef>
#include <cstdint>

namespace crucible::vessel {

// The type of the layout note, after the level note of type 1.
inline constexpr std::uint32_t kLayoutNoteType = 2;

// The alignment of a note in the ELF format.
inline constexpr std::size_t kLayoutNoteAlignment = 4;

// The eight words of the note, in the order of the file.
struct LayoutNote {
    std::uint32_t name_size = 9;
    std::uint32_t descriptor_size = 8;
    std::uint32_t type = kLayoutNoteType;
    // "Cruc", "ible", and the zero with three bytes of padding, as the words
    // of a little-endian file hold them.
    std::uint32_t name_first = 0x63757243U;
    std::uint32_t name_second = 0x656C6269U;
    std::uint32_t name_end = 0;
    std::uint32_t digest_low = 0;
    std::uint32_t digest_high = 0;
};

// The note of a digest.
[[nodiscard]] consteval LayoutNote layout_note_of(std::uint64_t digest) noexcept {
    LayoutNote note{};
    note.digest_low = static_cast<std::uint32_t>(digest);
    note.digest_high = static_cast<std::uint32_t>(digest >> 32);
    return note;
}

// The layout digest of the shared types and the C entries, as the unit of
// the note of this library computed it.  The host tool reads it.
[[nodiscard]] std::uint64_t library_layout_digest() noexcept;

}  // namespace crucible::vessel
