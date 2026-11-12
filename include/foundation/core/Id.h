#pragma once

// The Scalar family: a strong id and a digest, two numbers whose meaning is
// in their type.
//
//   Id<Tag, Word>    an id of the kind Tag in an unsigned Word.  An explicit
//                    construction from a Word, raw(), == and <=>, and no
//                    arithmetic
//   Digest<Tag>      a hash of 64 bits of the kind Tag.  An explicit
//                    construction, raw(), == and <=>, and no arithmetic
//
// An Id has no default constructor and no none().  A field that can hold no
// id is an Option<Id>.  The value max_of<Word> is the empty value of the
// niche of Id, so an Option<Id> has the size of the Word, and decode gives no
// value for the bytes of that value.  An Id with that value can exist, and
// Option::some of it ends the process, as for each niche payload of Choice.
//
// A Digest has no niche: each value of 64 bits is a hash.  So an
// Option<Digest> holds a tag beside the word.
//
// Two ids of different tags do not compare, an id is no integer, and the
// number of an id comes out only through raw().  Tag is a class type, which a
// header can name with no definition: Id<struct SlotTag, std::uint32_t>.

#include <foundation/core/Choice.h>
#include <foundation/core/Number.h>

#include <compare>
#include <cstdint>
#include <type_traits>

namespace foundation::core {

template <class Tag, UnsignedInteger Word>
    requires std::is_class_v<Tag>
class Id final {
    Word value_;

public:
    constexpr explicit Id(Word value) noexcept : value_{value} {}

    [[nodiscard]] constexpr Word raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(Id left, Id right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Id left, Id right) noexcept = default;
};

// The empty value of an Option of an Id: the highest value of the Word.
template <class Tag, UnsignedInteger Word>
struct niche<Id<Tag, Word>> {
    [[nodiscard]] static constexpr Id<Tag, Word> empty(niche_key<Id<Tag, Word>> const&) noexcept {
        return Id<Tag, Word>{max_of<Word>};
    }
    [[nodiscard]] static constexpr bool is_empty(niche_key<Id<Tag, Word>> const&, Id<Tag, Word> const& id) noexcept {
        return id.raw() == max_of<Word>;
    }
};

template <class Tag>
    requires std::is_class_v<Tag>
class Digest final {
    std::uint64_t value_;

public:
    constexpr explicit Digest(std::uint64_t value) noexcept : value_{value} {}

    [[nodiscard]] constexpr std::uint64_t raw() const noexcept { return value_; }

    [[nodiscard]] friend constexpr bool operator==(Digest left, Digest right) noexcept = default;
    [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Digest left, Digest right) noexcept = default;
};

}  // namespace foundation::core
