// Tests of the packed words of foundation/core/PackedWord.h: each operation
// at run time, each refusal, and a property test against the shift and the
// mask of a hand-written word.

#include <foundation/core/PackedWord.h>

#include <foundation/Platform.h>

#include "philox_stream.h"

#include <cstdint>

namespace {

using ::foundation::core::BitField;
using ::foundation::core::PackedWord;

struct Tags final {};
struct Data final {};
struct Flag final {};

// The layout of the kinds of the scalar slots of an op: ten bits of tags,
// five bits of data slots and one bit above them.
using Kinds = PackedWord<std::uint16_t, BitField<Tags, 10>, BitField<Data, 5>>;

// Three fields that fill a word of 64 bits.
using Full = PackedWord<std::uint64_t, BitField<Tags, 33>, BitField<Data, 30>, BitField<Flag, 1>>;

// The operations read the word that from_bits took, and with changes only its
// own field.
void test_operations() {
    Kinds word = Kinds::from_bits(0x7ABCu).expect("the word holds only the bits of the fields");
    CRUCIBLE_FATAL_INVARIANT(word.get<Tags>() == 0x2BCu && word.get<Data>() == 0x1Eu && word.bits() == 0x7ABCu);
    word = word.with<Tags>(std::uint16_t{0x3FF}).expect("ten bits hold 0x3FF");
    CRUCIBLE_FATAL_INVARIANT(word.get<Tags>() == 0x3FFu && word.get<Data>() == 0x1Eu && word.bits() == 0x7BFFu);
    word = word.with<Data>(std::uint8_t{0}).expect("five bits hold 0");
    CRUCIBLE_FATAL_INVARIANT(word.bits() == 0x03FFu && !(word == Kinds{}) && Kinds{}.bits() == 0);
    Full const full = Full{}.with<Flag>(std::uint8_t{1}).expect("one bit holds 1");
    CRUCIBLE_FATAL_INVARIANT(full.bits() == 0x8000000000000000ull && full.get<Data>() == 0 && full.get<Flag>() == 1);
}

// with_fitting writes a value whose type fits its field, keeps each other
// field, and gives the word that with gives for the same value.
void test_fitting_writes() {
    Kinds const word = Kinds::from_bits(0x7ABCu).expect("the word holds only the bits of the fields");
    Kinds const tagged = word.with_fitting<Tags>(std::uint8_t{0xFF});
    CRUCIBLE_FATAL_INVARIANT(tagged.bits() == 0x78FFu && tagged.get<Tags>() == 0xFFu && tagged.get<Data>() == 0x1Eu);
    CRUCIBLE_FATAL_INVARIANT(tagged == word.with<Tags>(std::uint16_t{0xFF}).expect("ten bits hold 0xFF"));
    Full const full = Full{}.with_fitting<Data>(std::uint16_t{0xFFFF}).with_fitting<Tags>(std::uint32_t{0xFFFFFFFFu});
    CRUCIBLE_FATAL_INVARIANT(full.bits() == 0x0001FFFEFFFFFFFFull && full.get<Flag>() == 0);
    CRUCIBLE_FATAL_INVARIANT(full.get<Data>() == 0xFFFFu && full.get<Tags>() == 0xFFFFFFFFu);
}

// from_bits refuses a word with a bit above the last field, and with refuses
// a value with a bit past the width of its field.  A refusal changes nothing.
void test_refusals() {
    CRUCIBLE_FATAL_INVARIANT(Kinds::from_bits(0x8000u).is_none() && Kinds::from_bits(0xFFFFu).is_none());
    CRUCIBLE_FATAL_INVARIANT(Kinds::from_bits(0x7FFFu).is_some() && Full::from_bits(~std::uint64_t{0}).is_some());
    Kinds const word = Kinds::from_bits(0x1234u).expect("the word holds only the bits of the fields");
    CRUCIBLE_FATAL_INVARIANT(word.with<Data>(std::uint8_t{0x20}).is_none());
    CRUCIBLE_FATAL_INVARIANT(word.with<Tags>(std::uint16_t{0x400}).is_none());
    CRUCIBLE_FATAL_INVARIANT(Full{}.with<Flag>(std::uint8_t{2}).is_none());
    CRUCIBLE_FATAL_INVARIANT(Full{}.with<Data>(std::uint32_t{0x40000000u}).is_none());
    CRUCIBLE_FATAL_INVARIANT(word.bits() == 0x1234u);
}

// The property: a random word, refused or not as the hand-written mask says,
// gives the fields that the hand-written shifts give, and a random value into
// a random field gives the word of the hand-written mask and or.  The stream
// has a fixed seed.
void test_against_the_hand_written_word() {
    ::crucible::test::PhiloxStream stream{0x9ACCu};
    for (int step = 0; step < 4096; ++step) {
        auto const raw = static_cast<std::uint16_t>(stream.below(0x10000u));
        bool is_parsed = false;
        for (Kinds const word : Kinds::from_bits(raw)) {
            is_parsed = true;
            CRUCIBLE_FATAL_INVARIANT(word.get<Tags>() == (raw & 0x3FFu) && word.get<Data>() == ((raw >> 10) & 0x1Fu));
            auto const byte = static_cast<std::uint8_t>(stream.below(0x100u));
            CRUCIBLE_FATAL_INVARIANT(word.with_fitting<Tags>(byte).bits()
                                     == static_cast<std::uint16_t>((raw & ~0x3FFu) | byte));
            auto const tags = static_cast<std::uint16_t>(stream.below(0x800u));
            bool is_tagged = false;
            for (Kinds const tagged : word.with<Tags>(tags)) {
                is_tagged = true;
                CRUCIBLE_FATAL_INVARIANT(tagged.bits() == static_cast<std::uint16_t>((raw & ~0x3FFu) | tags));
            }
            CRUCIBLE_FATAL_INVARIANT(is_tagged == (tags < 0x400u));
            auto const data = static_cast<std::uint8_t>(stream.below(0x40u));
            bool is_marked = false;
            for (Kinds const marked : word.with<Data>(data)) {
                is_marked = true;
                auto const expected = static_cast<std::uint16_t>((raw & ~0x7C00u) | (std::uint32_t{data} << 10));
                CRUCIBLE_FATAL_INVARIANT(marked.bits() == expected);
            }
            CRUCIBLE_FATAL_INVARIANT(is_marked == (data < 0x20u));
        }
        CRUCIBLE_FATAL_INVARIANT(is_parsed == ((raw & 0x8000u) == 0));
    }
}

}  // namespace

int main() {
    test_operations();
    test_fitting_writes();
    test_refusals();
    test_against_the_hand_written_word();
    return 0;
}
