// Tests of foundation/core/Ref.h: Box and mint_box.

#include <foundation/core/Ref.h>

#include <foundation/Platform.h>
#include <foundation/effects/Effect.h>

#include "abort_probe.h"
#include "philox_stream.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

using ::foundation::core::Box;
using ::foundation::core::mint_box;

// The allocation tag of foundation/effects passes the gate, and the other
// capability tags do not.
static_assert(::foundation::core::AllocationCapability<::foundation::effects::Alloc>);
static_assert(!::foundation::core::AllocationCapability<::foundation::effects::IO>);
static_assert(!::foundation::core::AllocationCapability<::foundation::effects::Block>);

// The count of live CountedObject objects.  A Box must destroy each object
// one time and leak none.
int live_objects = 0;

class CountedObject {
public:
    explicit CountedObject(int label) noexcept : label_{label} { ++live_objects; }
    CountedObject(CountedObject const&) = delete;
    CountedObject& operator=(CountedObject const&) = delete;
    ~CountedObject() { --live_objects; }

    [[nodiscard]] int label() const noexcept { return label_; }
    void relabel(int label) noexcept { label_ = label; }

private:
    int label_ = 0;
};

struct Pair {
    int first = 0;
    int second = 0;
};

void test_mint_and_access() {
    ::foundation::effects::Alloc const alloc{};
    Box<int> number = mint_box<int>(alloc, 41);
    CRUCIBLE_FATAL_INVARIANT(*number == 41);
    *number += 1;
    CRUCIBLE_FATAL_INVARIANT(*number == 42);

    Box<Pair> pair = mint_box<Pair>(alloc, 3, 4);
    CRUCIBLE_FATAL_INVARIANT(pair->first == 3 && pair->second == 4);
    pair->second = 5;
    Box<Pair> const& fixed_pair = pair;
    CRUCIBLE_FATAL_INVARIANT(fixed_pair->second == 5 && (*fixed_pair).first == 3);
    CRUCIBLE_FATAL_INVARIANT(fixed_pair.view()->second == 5);

    Box<int> zero = mint_box<int>(alloc);
    CRUCIBLE_FATAL_INVARIANT(*zero == 0);

    // A Ref from view() reaches the object of the Box, and a write through
    // it shows through the Box.  Each call of view() gives a brand of its own.
    auto const pair_ref = pair.view();
    static_assert(!std::is_same_v<decltype(pair.view()), decltype(pair.view())>);
    pair_ref->first = 8;
    CRUCIBLE_FATAL_INVARIANT(pair->first == 8);
    CRUCIBLE_FATAL_INVARIANT(::foundation::core::same_object(pair_ref, fixed_pair.view()));
}

void test_destruction_counts() {
    ::foundation::effects::Alloc const alloc{};
    {
        Box<CountedObject> first = mint_box<CountedObject>(alloc, 1);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        Box<CountedObject> second{std::move(first)};
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(second->label() == 1);

        Box<CountedObject> third = mint_box<CountedObject>(alloc, 3);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 2);
        // The assignment destroys the object that the target held.
        third = std::move(second);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(third->label() == 1);
        third->relabel(9);
        CRUCIBLE_FATAL_INVARIANT((*third).label() == 9);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

void test_self_move_assignment_keeps_the_object() {
    ::foundation::effects::Alloc const alloc{};
    {
        Box<CountedObject> held = mint_box<CountedObject>(alloc, 7);
        Box<CountedObject>& alias = held;
        held = std::move(alias);
        CRUCIBLE_FATAL_INVARIANT(live_objects == 1);
        CRUCIBLE_FATAL_INVARIANT(held->label() == 7);
    }
    CRUCIBLE_FATAL_INVARIANT(live_objects == 0);
}

// A payload that counts its live objects and can move.
int live_movables = 0;

class CountedMovable {
public:
    explicit CountedMovable(int label) noexcept : label_{label} { ++live_movables; }
    CountedMovable(CountedMovable&& other) noexcept : label_{other.label_} {
        other.label_ = 0;
        ++live_movables;
    }
    CountedMovable(CountedMovable const&) = delete;
    CountedMovable& operator=(CountedMovable const&) = delete;
    CountedMovable& operator=(CountedMovable&&) = delete;
    ~CountedMovable() { --live_movables; }

    [[nodiscard]] int label() const noexcept { return label_; }

private:
    int label_ = 0;
};

void test_consume() {
    ::foundation::effects::Alloc const alloc{};
    {
        Box<CountedMovable> held = mint_box<CountedMovable>(alloc, 13);
        CRUCIBLE_FATAL_INVARIANT(live_movables == 1);
        CountedMovable const taken = std::move(held).consume();
        // The moved-from object in the storage is destroyed, so one object
        // lives: the one that consume() gave back.
        CRUCIBLE_FATAL_INVARIANT(live_movables == 1);
        CRUCIBLE_FATAL_INVARIANT(taken.label() == 13);
    }
    CRUCIBLE_FATAL_INVARIANT(live_movables == 0);
    CRUCIBLE_FATAL_INVARIANT(mint_box<int>(alloc, 17).consume() == 17);
}

// The probe consumes a moved-from Box on purpose: it holds no object, and
// the precondition ends the process.  The probe jumps out of the body with
// no destructor call, so the body owns no allocation.
void test_consume_of_moved_from_aborts() {
    Box<int> source = mint_box<int>(::foundation::effects::Alloc{}, 1);
    Box<int> target{std::move(source)};
    bool const moved_from_consume_aborts =
        ::crucible::test::aborts([&source] { static_cast<void>(std::move(source).consume()); });
    CRUCIBLE_FATAL_INVARIANT(moved_from_consume_aborts);
    CRUCIBLE_FATAL_INVARIANT(*target == 1);
}

// The probe borrows a moved-from Box on purpose: each build tests for the
// object, so the borrow ends the process and reads no null pointer.
void test_borrow_of_moved_from_aborts() {
    Box<int> source = mint_box<int>(::foundation::effects::Alloc{}, 2);
    Box<int> target{std::move(source)};
    bool const view_aborts = ::crucible::test::aborts([&source] { static_cast<void>(source.view()); });
    bool const star_aborts = ::crucible::test::aborts([&source] { static_cast<void>(*source); });
    Box<int> const& fixed_source = source;
    bool const fixed_view_aborts =
        ::crucible::test::aborts([&fixed_source] { static_cast<void>(fixed_source.view()); });
    bool const fixed_star_aborts = ::crucible::test::aborts([&fixed_source] { static_cast<void>(*fixed_source); });
    CRUCIBLE_FATAL_INVARIANT(view_aborts && star_aborts && fixed_view_aborts && fixed_star_aborts);
    CRUCIBLE_FATAL_INVARIANT(*target == 2);
}

// The allocation door gives malloc storage up to an alignment of 16 and
// aligned_alloc storage above it.  Each alignment from 8 to 4096 gives a
// Box at that alignment, and its object holds its value.
template <std::size_t Alignment>
struct alignas(Alignment) AlignedValue {
    std::uint64_t value = 0;
};

template <std::size_t Alignment>
void test_box_at_alignment() {
    ::foundation::effects::Alloc const alloc{};
    for (std::uint64_t trial = 0; trial < 8; ++trial) {
        Box<AlignedValue<Alignment>> held = mint_box<AlignedValue<Alignment>>(alloc, AlignedValue<Alignment>{trial});
        CRUCIBLE_FATAL_INVARIANT(std::bit_cast<std::uintptr_t>(&*held) % Alignment == 0);
        CRUCIBLE_FATAL_INVARIANT(held->value == trial);
    }
}

void test_each_alignment() {
    test_box_at_alignment<8>();
    test_box_at_alignment<16>();
    test_box_at_alignment<32>();
    test_box_at_alignment<64>();
    test_box_at_alignment<128>();
    test_box_at_alignment<4096>();
}

// The property: boxes built from random values, moved along a random
// chain, give back the values they were built from.  A plain model holds
// the values.
void test_agrees_with_plain_model() {
    ::foundation::effects::Alloc const alloc{};
    ::crucible::test::PhiloxStream stream{0x5EEDB0C5000000A1u};
    for (int trial = 0; trial < 512; ++trial) {
        std::uint64_t const model_value = stream.next_wide();
        Box<std::uint64_t> source = mint_box<std::uint64_t>(alloc, model_value);
        int const hops = static_cast<int>(stream.below(5));
        Box<std::uint64_t> current{std::move(source)};
        for (int hop = 0; hop < hops; ++hop) {
            Box<std::uint64_t> next{std::move(current)};
            current = std::move(next);
        }
        CRUCIBLE_FATAL_INVARIANT(*current == model_value);
    }
}

}  // namespace

int main() {
    test_mint_and_access();
    test_destruction_counts();
    test_self_move_assignment_keeps_the_object();
    test_consume();
    test_consume_of_moved_from_aborts();
    test_borrow_of_moved_from_aborts();
    test_each_alignment();
    test_agrees_with_plain_model();
    return 0;
}
