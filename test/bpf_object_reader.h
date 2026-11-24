#pragma once

// The reader of the bytes of an embedded BPF object, for a test that loads no
// program.
//
// A BPF object is an ELF64 object of little-endian data.  The reader gives a
// word at a byte offset, the sections of the object by index, and a section
// by its name.  The C structs of the programs name their padding _pad, so the
// compare of a name drops one leading underscore of the object.

#include <fixy/Core.h>

#include <cstddef>
#include <cstdint>

namespace crucible::test {

using ObjectBytes = ::fixy::View<unsigned char const>;

// The bytes of a little-endian object, read at a byte offset.
class LittleEndian {
public:
    explicit constexpr LittleEndian(ObjectBytes bytes) noexcept : bytes_{bytes} {}

    // The word of Width bytes at offset, or no value past the end.
    template <std::size_t Width>
    [[nodiscard]] ::fixy::Option<std::uint64_t> word_at(std::size_t offset) const noexcept {
        return bytes_.window<Width>(offset).map([](::fixy::View<unsigned char const, Width> word) noexcept {
            std::uint64_t value = 0;
            std::size_t shift = 0;
            for (unsigned char const byte : word) {
                value |= std::uint64_t{byte} << shift;
                shift += 8;
            }
            return value;
        });
    }

    // The word of Width bytes at offset.  The walk reads only inside the
    // parts that the headers bound, so a read past the end ends the test.
    template <std::size_t Width>
    [[nodiscard]] std::uint64_t word(std::size_t offset) const noexcept {
        return word_at<Width>(offset).expect("the walk reads inside the bytes of the object");
    }

    [[nodiscard]] constexpr ObjectBytes bytes() const noexcept { return bytes_; }

    // True when the zero-terminated name at offset is name.  A name that
    // starts with an underscore compares without it.
    [[nodiscard]] bool is_named(std::size_t offset, ::fixy::TextView name) const noexcept {
        if (word<1>(offset) == '_') {
            ++offset;
        }
        std::size_t index = 0;
        for (char const character : name) {
            if (word<1>(offset + index) != static_cast<unsigned char>(character)) {
                return false;
            }
            ++index;
        }
        return word<1>(offset + index) == 0;
    }

private:
    ObjectBytes bytes_;
};

// One section header of an ELF64 object of little-endian data.  sh_name is at
// its byte 0, sh_flags at byte 8, sh_offset at byte 24 and sh_size at byte 32.
// The section header table is at e_shoff, at byte 40 of the object, and
// e_shentsize, e_shnum and e_shstrndx are at bytes 58, 60 and 62.
class ObjectSection {
public:
    // The section of index, which is below section_count of the object.
    ObjectSection(LittleEndian object, std::size_t index) noexcept
        : object_{object}, header_{object.word<8>(40) + object.word<2>(58) * index} {}

    [[nodiscard]] static std::size_t section_count(LittleEndian object) noexcept { return object.word<2>(60); }

    [[nodiscard]] bool is_named(::fixy::TextView name) const noexcept {
        std::uint64_t const names =
            object_.word<8>(object_.word<8>(40) + object_.word<2>(58) * object_.word<2>(62) + 24);
        return object_.is_named(names + object_.word<4>(header_), name);
    }

    // True when the section holds instructions: SHF_EXECINSTR is 4.
    [[nodiscard]] bool has_instructions() const noexcept { return (object_.word<8>(header_ + 8) & 4u) != 0; }

    [[nodiscard]] ObjectBytes bytes() const noexcept {
        return object_.bytes()
            .window(object_.word<8>(header_ + 24), object_.word<8>(header_ + 32))
            .expect("the section is inside the bytes of the object");
    }

private:
    LittleEndian object_;
    std::size_t header_ = 0;
};

// The bytes of the section of name, or no value when the object has no such
// section.  Complexity: linear in the sections of the object.
[[nodiscard]] inline ::fixy::Option<ObjectBytes> section_named(LittleEndian object, ::fixy::TextView name) noexcept {
    for (std::size_t index = 0; index < ObjectSection::section_count(object); ++index) {
        ObjectSection const section{object, index};
        if (section.is_named(name)) {
            return ::fixy::some(section.bytes());
        }
    }
    return ::fixy::none;
}

}  // namespace crucible::test
