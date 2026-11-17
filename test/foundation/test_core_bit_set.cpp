// Tests of the bit sets of foundation/core/BitSet.h: each verb of BitSet and
// HeapBitSet, the walk in the order of the indexes, the refusal of an id past
// the count, and a property test against a model of plain flags.

#include <foundation/core/BitSet.h>

#include <foundation/Platform.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include "philox_stream.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

using ::foundation::core::BitId;
using ::foundation::core::BitSet;
using ::foundation::core::FixedArray;
using ::foundation::core::HeapBitSet;
using ::foundation::core::Index;
using ::foundation::core::mint_heap_bit_set;
using ::foundation::core::Quantity;
namespace unit = ::foundation::core::unit;

struct SlotTag final {};

using SlotId = BitId<SlotTag>;
using Count = Quantity<unit::Element, std::uint32_t>;

// The largest count of a set in the property test.
constexpr std::size_t kModelCapacity = 1024;

[[nodiscard]] HeapBitSet<SlotTag> heap_set_of(std::uint32_t count) noexcept {
    return mint_heap_bit_set<SlotTag>(::foundation::effects::testing::test().alloc, Count{count});
}

// The index raw of a fixed set of N indexes.
template <std::size_t N>
[[nodiscard]] Index<N> index_of(std::size_t raw) noexcept {
    return Index<N>::check(raw).expect("the test gives an index below N");
}

void test_fixed_set_verbs() {
    BitSet<130> set{};
    CRUCIBLE_FATAL_INVARIANT(set.is_empty() && set.size() == 0);
    FixedArray<std::size_t, 7> const positions{0, 63, 64, 127, 128, 129, 64};
    for (std::size_t const raw : positions) {
        set.set(index_of<130>(raw));
    }
    CRUCIBLE_FATAL_INVARIANT(set.size() == 6 && !set.is_empty());
    CRUCIBLE_FATAL_INVARIANT(set.test(index_of<130>(129)) && !set.test(index_of<130>(1)));
    set.reset(index_of<130>(64));
    set.reset(index_of<130>(65));
    CRUCIBLE_FATAL_INVARIANT(set.size() == 5 && !set.test(index_of<130>(64)));
    std::size_t expected_count = 0;
    std::size_t last = 0;
    for (Index<130> const index : set) {
        CRUCIBLE_FATAL_INVARIANT(expected_count == 0 || index.raw() > last);
        last = index.raw();
        ++expected_count;
    }
    CRUCIBLE_FATAL_INVARIANT(expected_count == 5 && last == 129);
    BitSet<130> const copied = set;
    set.clear();
    CRUCIBLE_FATAL_INVARIANT(set.is_empty() && copied.size() == 5 && !(set == copied));
}

void test_heap_set_verbs() {
    HeapBitSet<SlotTag> set = heap_set_of(130);
    CRUCIBLE_FATAL_INVARIANT(set.extent().raw() == 130 && set.is_empty());
    set.set(SlotId{0});
    set.set(SlotId{64});
    set.set(SlotId{129});
    CRUCIBLE_FATAL_INVARIANT(set.size() == 3 && set.test(SlotId{64}) && !set.test(SlotId{65}));
    set.reset(SlotId{64});
    std::uint32_t sum = 0;
    for (SlotId const id : set) {
        sum += id.raw() + 1;
    }
    CRUCIBLE_FATAL_INVARIANT(sum == 1 + 130);
    set.clear();
    CRUCIBLE_FATAL_INVARIANT(set.is_empty() && set.size() == 0);

    HeapBitSet<SlotTag> const none = heap_set_of(0);
    HeapBitSet<SlotTag> const fresh{};
    CRUCIBLE_FATAL_INVARIANT(none.extent().raw() == 0 && none.is_empty() && none == fresh);
    for (SlotId const id : none) {
        static_cast<void>(id);
        ::foundation::core::fatal("an empty set gives no id to a walk");
    }
}

// Two heap sets of different counts give LengthMismatch for each change, and
// the change writes nothing.  The queries read the two sets as sets of ids.
void test_heap_sets_of_two_counts() {
    HeapBitSet<SlotTag> small = heap_set_of(10);
    HeapBitSet<SlotTag> large = heap_set_of(200);
    small.set(SlotId{3});
    large.set(SlotId{3});
    CRUCIBLE_FATAL_INVARIANT(small == large && large == small);
    CRUCIBLE_FATAL_INVARIANT(small.is_subset_of(large) && large.is_subset_of(small));
    large.set(SlotId{150});
    CRUCIBLE_FATAL_INVARIANT(!(small == large) && small.is_subset_of(large) && !large.is_subset_of(small));
    CRUCIBLE_FATAL_INVARIANT(!small.is_disjoint_from(large) && !large.is_disjoint_from(small));
    CRUCIBLE_FATAL_INVARIANT(small.unite(large).is_err() && small.intersect(large).is_err());
    CRUCIBLE_FATAL_INVARIANT(small.subtract(large).is_err() && small.flip(large).is_err());
    CRUCIBLE_FATAL_INVARIANT(small.size() == 1 && small.test(SlotId{3}));
    small.reset(SlotId{3});
    CRUCIBLE_FATAL_INVARIANT(small.is_disjoint_from(large) && large.is_disjoint_from(small));
}

// A move gives the words to the target and leaves the source empty.
void test_heap_set_moves() {
    HeapBitSet<SlotTag> source = heap_set_of(70);
    source.set(SlotId{69});
    HeapBitSet<SlotTag> target{std::move(source)};
    CRUCIBLE_FATAL_INVARIANT(target.extent().raw() == 70 && target.test(SlotId{69}));
    HeapBitSet<SlotTag> other = heap_set_of(5);
    other = std::move(target);
    CRUCIBLE_FATAL_INVARIANT(other.extent().raw() == 70 && other.size() == 1);
}

// An id at or past the count, an id of the default set, and a read through
// the end cursor of a walk end the process.
void test_refusals(::fixy::TestRunnerCtx const& test) {
    ::foundation::core::TextView const outside{
        "foundation: fatal: a HeapBitSet got an id at or past the count of its mint"};
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapBitSet<SlotTag> const set = heap_set_of(64);
            static_cast<void>(set.test(SlotId{64}));
        },
        outside);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapBitSet<SlotTag> set = heap_set_of(65);
            set.set(SlotId{0xFFFFFFFFu});
        },
        outside);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            HeapBitSet<SlotTag> set{};
            set.reset(SlotId{0});
        },
        outside);
    ::fixy::check::expect_dies(
        test,
        [] noexcept {
            BitSet<8> set{};
            set.set(index_of<8>(7));
            static_cast<void>(*set.end());
        },
        ::foundation::core::TextView{
            "foundation: fatal: a read or a step through the end cursor of a walk of a bit set"});
    HeapBitSet<SlotTag> const control = heap_set_of(64);
    CRUCIBLE_FATAL_INVARIANT(!control.test(SlotId{63}));
}

// ── The property test ──────────────────────────────────────────────────────

// The model of a set: one flag for each index below the count.
struct Model {
    FixedArray<bool, kModelCapacity> held{};
    std::size_t count = 0;

    [[nodiscard]] bool at(std::size_t raw) const noexcept {
        return held.at(Index<kModelCapacity>::check(raw).expect("the model holds the index"));
    }
    void put(std::size_t raw, bool value) noexcept {
        held.at(Index<kModelCapacity>::check(raw).expect("the model holds the index")) = value;
    }

    // The relations of this model and other: subset and disjoint.
    [[nodiscard]] bool is_subset_of(Model const& other) const noexcept {
        for (std::size_t raw = 0; raw != count; ++raw) {
            if (at(raw) && !other.at(raw)) return false;
        }
        return true;
    }

    [[nodiscard]] bool is_disjoint_from(Model const& other) const noexcept {
        for (std::size_t raw = 0; raw != count; ++raw) {
            if (at(raw) && other.at(raw)) return false;
        }
        return true;
    }
};

// The access of the property test to a fixed set of N indexes.
template <std::size_t N>
struct FixedSide {
    using Set = BitSet<N>;
    static constexpr std::uint32_t count = N;

    [[nodiscard]] static Set make() noexcept { return Set{}; }
    static void set(Set& target, std::size_t raw) noexcept { target.set(index_of<N>(raw)); }
    static void reset(Set& target, std::size_t raw) noexcept { target.reset(index_of<N>(raw)); }
    [[nodiscard]] static bool test(Set const& target, std::size_t raw) noexcept {
        return target.test(index_of<N>(raw));
    }
    static void unite(Set& target, Set const& other) noexcept { target.unite(other); }
    static void intersect(Set& target, Set const& other) noexcept { target.intersect(other); }
    static void subtract(Set& target, Set const& other) noexcept { target.subtract(other); }
    static void flip(Set& target, Set const& other) noexcept { target.flip(other); }
    [[nodiscard]] static std::size_t raw_of(Index<N> index) noexcept { return index.raw(); }
};

// The access of the property test to a heap set of Count ids.
template <std::uint32_t Count>
struct HeapSide {
    using Set = HeapBitSet<SlotTag>;
    static constexpr std::uint32_t count = Count;

    [[nodiscard]] static Set make() noexcept { return heap_set_of(Count); }
    static void set(Set& target, std::size_t raw) noexcept { target.set(SlotId{static_cast<std::uint32_t>(raw)}); }
    static void reset(Set& target, std::size_t raw) noexcept { target.reset(SlotId{static_cast<std::uint32_t>(raw)}); }
    [[nodiscard]] static bool test(Set const& target, std::size_t raw) noexcept {
        return target.test(SlotId{static_cast<std::uint32_t>(raw)});
    }
    static void unite(Set& target, Set const& other) noexcept { target.unite(other).expect("one count"); }
    static void intersect(Set& target, Set const& other) noexcept { target.intersect(other).expect("one count"); }
    static void subtract(Set& target, Set const& other) noexcept { target.subtract(other).expect("one count"); }
    static void flip(Set& target, Set const& other) noexcept { target.flip(other).expect("one count"); }
    [[nodiscard]] static std::size_t raw_of(SlotId id) noexcept { return id.raw(); }
};

// Compares a set with its model: each flag, the count, the walk in the order
// of the indexes, and the bits past the count, which no index reaches and the
// walk must never give.  Complexity: O(count).
template <class Side>
void expect_agrees(typename Side::Set const& set, Model const& model) {
    std::size_t members = 0;
    for (std::size_t raw = 0; raw != model.count; ++raw) {
        CRUCIBLE_FATAL_INVARIANT(Side::test(set, raw) == model.at(raw));
        members += model.at(raw) ? 1uz : 0uz;
    }
    CRUCIBLE_FATAL_INVARIANT(set.size() == members && set.is_empty() == (members == 0));
    std::size_t next = 0;
    std::size_t walked = 0;
    for (auto const member : set) {
        std::size_t const raw = Side::raw_of(member);
        CRUCIBLE_FATAL_INVARIANT(raw >= next && raw < model.count && model.at(raw));
        for (std::size_t skipped = next; skipped != raw; ++skipped) {
            CRUCIBLE_FATAL_INVARIANT(!model.at(skipped));
        }
        next = raw + 1;
        ++walked;
    }
    CRUCIBLE_FATAL_INVARIANT(walked == members);
}

// The property: random sets, resets, clears and changes of two sets keep each
// set equal to its model, and the queries agree with the queries of the
// models.  The stream has a fixed seed for each count.
template <class Side>
void expect_property(std::uint64_t seed) {
    ::crucible::test::PhiloxStream stream{seed};
    typename Side::Set first = Side::make();
    typename Side::Set second = Side::make();
    Model first_model{};
    Model second_model{};
    first_model.count = Side::count;
    second_model.count = Side::count;
    for (int step = 0; step < 2048; ++step) {
        bool const is_first = stream.below(2) == 0;
        typename Side::Set& target = is_first ? first : second;
        Model& target_model = is_first ? first_model : second_model;
        typename Side::Set const& other = is_first ? second : first;
        Model const& other_model = is_first ? second_model : first_model;
        std::size_t const raw = static_cast<std::size_t>(stream.below(Side::count));
        switch (stream.below(16)) {
            case 0:
                Side::unite(target, other);
                for (std::size_t at = 0; at != Side::count; ++at)
                    target_model.put(at, target_model.at(at) || other_model.at(at));
                break;
            case 1:
                Side::intersect(target, other);
                for (std::size_t at = 0; at != Side::count; ++at)
                    target_model.put(at, target_model.at(at) && other_model.at(at));
                break;
            case 2:
                Side::subtract(target, other);
                for (std::size_t at = 0; at != Side::count; ++at)
                    target_model.put(at, target_model.at(at) && !other_model.at(at));
                break;
            case 3:
                Side::flip(target, other);
                for (std::size_t at = 0; at != Side::count; ++at)
                    target_model.put(at, target_model.at(at) != other_model.at(at));
                break;
            case 4:
                if (stream.below(8) == 0) {
                    target.clear();
                    for (std::size_t at = 0; at != Side::count; ++at)
                        target_model.put(at, false);
                }
                break;
            case 5:
            case 6:
            case 7:
            case 8:
            case 9:
                Side::reset(target, raw);
                target_model.put(raw, false);
                break;
            default:
                Side::set(target, raw);
                target_model.put(raw, true);
                break;
        }
        expect_agrees<Side>(first, first_model);
        expect_agrees<Side>(second, second_model);
        bool const is_first_inside = first_model.is_subset_of(second_model);
        bool const is_second_inside = second_model.is_subset_of(first_model);
        CRUCIBLE_FATAL_INVARIANT(first.is_subset_of(second) == is_first_inside);
        CRUCIBLE_FATAL_INVARIANT(second.is_subset_of(first) == is_second_inside);
        CRUCIBLE_FATAL_INVARIANT(first.is_disjoint_from(second) == first_model.is_disjoint_from(second_model));
        CRUCIBLE_FATAL_INVARIANT((first == second) == (is_first_inside && is_second_inside));
    }
}

void test_sets_agree_with_the_model() {
    expect_property<FixedSide<1>>(0x1001u);
    expect_property<FixedSide<63>>(0x1063u);
    expect_property<FixedSide<64>>(0x1064u);
    expect_property<FixedSide<65>>(0x1065u);
    expect_property<FixedSide<130>>(0x1130u);
    expect_property<HeapSide<1>>(0x2001u);
    expect_property<HeapSide<63>>(0x2063u);
    expect_property<HeapSide<64>>(0x2064u);
    expect_property<HeapSide<65>>(0x2065u);
    expect_property<HeapSide<130>>(0x2130u);
    expect_property<HeapSide<1000>>(0x3000u);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_fixed_set_verbs();
    test_heap_set_verbs();
    test_heap_sets_of_two_counts();
    test_heap_set_moves();
    test_refusals(test);
    test_sets_agree_with_the_model();
    return 0;
}
