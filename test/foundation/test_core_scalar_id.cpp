// Tests of foundation/core/Id.h at run time: an Option of an Id is empty only
// for the highest word, decode of a word gives the id or no value, and the
// order of two ids is the order of their words.  A Digest takes each word.

#include <foundation/core/Id.h>
#include <foundation/core/Scalar.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <compare>
#include <cstdint>

namespace {

namespace core = ::foundation::core;
using SlotId = core::Id<struct SlotTag, std::uint32_t>;
using MetaId = core::Id<struct MetaTag, std::uint64_t>;
using SchemaDigest = core::Digest<struct SchemaTag>;

template <class T>
[[gnu::noipa, nodiscard]] T opaque(T value) noexcept {
    return value;
}

void test_decode_of_each_word() {
    ::foundation::test::PhiloxStream stream{100};
    for (int index = 0; index < 100000; ++index) {
        std::uint64_t const wide = stream.next_wide();
        auto const narrow = static_cast<std::uint32_t>(wide);
        core::Option<SlotId> const slot = core::decode<SlotId>(opaque(narrow));
        CRUCIBLE_FATAL_INVARIANT(slot.is_some() == (narrow != 0xFFFFFFFFu));
        CRUCIBLE_FATAL_INVARIANT(core::decode<MetaId>(opaque(wide)).is_some() == (wide != ~std::uint64_t{0}));
        CRUCIBLE_FATAL_INVARIANT(core::decode<SchemaDigest>(opaque(wide)).is_some());
    }
    CRUCIBLE_FATAL_INVARIANT(core::decode<SlotId>(opaque(std::uint32_t{0xFFFFFFFFu})).is_none());
    CRUCIBLE_FATAL_INVARIANT(core::decode<MetaId>(opaque(~std::uint64_t{0})).is_none());
    SlotId const decoded = core::decode<SlotId>(opaque(std::uint32_t{41})).expect("41 is no empty value");
    CRUCIBLE_FATAL_INVARIANT(decoded == SlotId{41} && decoded.raw() == 41);
}

void test_order_is_the_order_of_the_words() {
    ::foundation::test::PhiloxStream stream{101};
    for (int index = 0; index < 100000; ++index) {
        std::uint32_t const left = stream.next_word();
        std::uint32_t const right = stream.next_word();
        SlotId const first{opaque(left)};
        SlotId const second{opaque(right)};
        CRUCIBLE_FATAL_INVARIANT(std::is_lt(first <=> second) == (left < right));
        CRUCIBLE_FATAL_INVARIANT((first == second) == (left == right));
        SchemaDigest const digest{opaque(std::uint64_t{left})};
        CRUCIBLE_FATAL_INVARIANT(digest.raw() == left);
    }
}

// An Option of an Id holds the id, and its empty state is the highest word.
void test_option_of_an_id() {
    core::Option<SlotId> const full = core::Option<SlotId>::some(SlotId{opaque(std::uint32_t{0xFFFFFFFEu})});
    CRUCIBLE_FATAL_INVARIANT(full.is_some());
    core::Option<SlotId> const empty{core::none};
    CRUCIBLE_FATAL_INVARIANT(empty.is_none());
    static_assert(sizeof(core::Option<SlotId>) == sizeof(std::uint32_t));
}

}  // namespace

int main() {
    test_decode_of_each_word();
    test_order_is_the_order_of_the_words();
    test_option_of_an_id();
    return 0;
}
