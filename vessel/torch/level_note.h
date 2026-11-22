#pragma once

// The level note of a vessel library: an ELF note that names the x86-64
// level of the flags of the library.  The loader of the vessel
// (crucible_native.py) reads the note from the file before it loads the
// library.  The global constructors of a library of a level above the host
// can fault, so no check after the load can come first.
//
// The note
//   The section .note.crucible holds one note in the ELF format, seven words
//   of 32 bits: the size of the name (9), the size of the descriptor (4), the
//   type (1), the name "Crucible" and its zero in three words, and the
//   descriptor.  The first byte of the descriptor holds the level (1 to 4),
//   the second byte holds the kind, and the two last bytes are zero.  A later
//   type of the same owner can hold the digest of the layout of a library.
//
//   The kind is exact when the flags give each feature of the level and no
//   feature above it: a build with CRUCIBLE_X86_LEVEL, and the level probe.
//   The kind is host for each other build, such as -march=native, whose flags
//   can give features above the level.  A level root of the loader takes only
//   exact libraries.
//
//   The name of the section starts with .note, so the assembler gives it the
//   type SHT_NOTE.  The attribute retain keeps it at --gc-sections, because
//   no code reads it.  A note has the alignment 4.  GCC gives a global object
//   of 16 bytes or more the alignment 16, unless the declaration states its
//   own alignment, so each note states kLevelNoteAlignment.  On aarch64 the
//   header declares nothing.

#include <foundation/os/X86Level.h>

#include <cstddef>
#include <cstdint>

namespace crucible::vessel {

#if defined(__x86_64__)

// The kind of the level of a library (the note).
enum class LevelKind : std::uint8_t {
    exact = 1,
    host = 2
};

// The alignment of a note in the ELF format.
inline constexpr std::size_t kLevelNoteAlignment = 4;

// The seven words of the note, in the order of the file.
struct LevelNote {
    std::uint32_t name_size = 9;
    std::uint32_t descriptor_size = 4;
    std::uint32_t type = 1;
    // "Cruc", "ible", and the zero with three bytes of padding, as the words
    // of a little-endian file hold them.
    std::uint32_t name_first = 0x63757243U;
    std::uint32_t name_second = 0x656C6269U;
    std::uint32_t name_end = 0;
    std::uint32_t descriptor = 0;
};

// The note of the flags of the unit, with the given kind.
[[nodiscard]] consteval LevelNote level_note_of(LevelKind kind) noexcept {
    LevelNote note{};
    auto const level = static_cast<std::uint32_t>(::foundation::os::x86_level_of_target());
    note.descriptor = level | (std::uint32_t{static_cast<std::uint8_t>(kind)} << 8);
    return note;
}

#endif

}  // namespace crucible::vessel
