// Tests of foundation/core/ArenaFrame.h: the stores of a frame into the slots
// of objects and of elements of its arena, a link to a base, a frame on a Box,
// and the parcel of an arena across two frames.

#include <foundation/core/ArenaFrame.h>

#include <foundation/Platform.h>
#include <foundation/core/BumpArena.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Effect.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace fc = ::foundation::core;
using Bytes = fc::Quantity<fc::unit::Byte, std::size_t>;

// The family of the arenas of the tests.
struct[[= ::foundation::brand::arena_family_brand{}]] TestFamily final {};

// An object with a link slot and a run slot of the family.
struct Node {
    std::uint32_t value = 0;
    fc::FrameLink<Node, TestFamily> next{};
    fc::FrameRunSlot<std::uint32_t const, TestFamily> items{};
};

// An element of a run with a run slot and a slot of a link of const, as a
// region holds its plan.
struct Entry {
    std::uint32_t count = 0;
    fc::FrameRunSlot<std::uint32_t const, TestFamily> items{};
    fc::FrameLink<Node const, TestFamily> owner{};
};

// An arm of a branch: a value and a link to the node that it runs.  A branch
// holds a run of arms that are not const, as a branch of the DAG does.
struct Arm {
    std::uint32_t value = 0;
    fc::FrameLink<Node, TestFamily> target{};
};

struct Branch {
    fc::FrameRunSlot<Arm, TestFamily> arms{};
};

// A base and a derived class that state the family, as the nodes of a graph
// do: each constructor takes the build key, so only an arena builds them, and
// to() of the base gives a Ref of the family.
struct Base {
    using brand_type = TestFamily;

    explicit constexpr Base(fc::ArenaBuildKey<TestFamily, Base> /*key*/) noexcept {}
    Base(Base const&) = delete("a node lives in its arena");
    Base& operator=(Base const&) = delete("a node lives in its arena");
    ~Base() = default;

    std::uint32_t depth = 0;
    fc::FrameLink<Base, TestFamily> next{};
};

struct Derived : Base {
    explicit constexpr Derived(fc::ArenaBuildKey<TestFamily, Derived> key) noexcept
        : Base{std::move(key).for_base<Base>()} {}

    std::uint32_t extra = 0;
};

inline constexpr fc::ArenaBlockSize block{Bytes{4096}};

// A store of a link and a store of a run fill the slots of an object with
// objects of the same frame.  A clear empties the link slot.
void test_store_fills_the_slots() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena = fc::mint_bump_arena<TestFamily>(block);
    std::uint32_t const total = fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const first = frame.template make<Node>(alloc);
        auto const second = frame.template make<Node>(alloc);
        second->value = 5;
        frame.store(first, &Node::next, second);
        auto items = frame.array(alloc, std::size_t{3}, std::uint32_t{7});
        frame.store(first, &Node::items, items);
        CRUCIBLE_FATAL_INVARIANT(first->next.is_some() && second->next.is_none());
        CRUCIBLE_FATAL_INVARIANT(first->items.size() == 3 && second->items.size() == 0);
        std::uint32_t sum = first->next.expect("the store linked the second node")->value;
        for (std::uint32_t const item : first->items.view()) {
            sum += item;
        }
        frame.clear(first, &Node::next);
        CRUCIBLE_FATAL_INVARIANT(first->next.is_none());
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(total == 26);
}

// A const object gives its link and its run only as const.
void test_const_holder_reads_const() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena = fc::mint_bump_arena<TestFamily>(block);
    fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const first = frame.template make<Node>(alloc);
        auto const second = frame.template make<Node>(alloc);
        frame.store(first, &Node::next, second);
        Node const& reader = *first;
        fc::Option<fc::Ref<Node const, TestFamily>> const linked = reader.next.option();
        CRUCIBLE_FATAL_INVARIANT(linked.is_some());
        std::size_t passes = 0;
        for (fc::Ref<Node const, TestFamily> const& node : reader.next) {
            if (node->value == 0) ++passes;
        }
        CRUCIBLE_FATAL_INVARIANT(passes == 1);
    });
}

// A store fills the slots of one element of a run.  A link slot of const takes
// an object that is not const, and a window of a run slot gives a part of it.
void test_store_fills_the_slots_of_an_element() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena = fc::mint_bump_arena<TestFamily>(block);
    fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto entries = frame.template build_array<Entry>(alloc, fc::ElementCount{2},
                                                         [](fc::RunStart) noexcept { return Entry{}; });
        auto const parent = frame.template make<Node>(alloc);
        parent->value = 3;
        auto parts = frame.array(alloc, std::size_t{4}, std::uint32_t{2});
        auto const second = entries.element(fc::RunStart{fc::ElementCount{1}}).expect("the run holds two entries");
        second->count = 4;
        frame.store(second, &Entry::items, parts);
        frame.store(second, &Entry::owner, parent);
        fc::View<Entry> const all = entries.view();
        Entry const& first_entry = *all.get(0).expect("the run holds two entries");
        Entry const& second_entry = *all.get(1).expect("the run holds two entries");
        CRUCIBLE_FATAL_INVARIANT(first_entry.count == 0 && first_entry.items.size() == 0);
        CRUCIBLE_FATAL_INVARIANT(second_entry.count == 4 && second_entry.items.size() == 4);
        std::uint32_t const owner_value =
            second_entry.owner.match([](fc::Ref<Node const, TestFamily> const& owner) noexcept { return owner->value; },
                                     []() noexcept { return std::uint32_t{0}; });
        CRUCIBLE_FATAL_INVARIANT(owner_value == 3);
        CRUCIBLE_FATAL_INVARIANT(second_entry.owner.is_some_and(
            [](fc::Ref<Node const, TestFamily> const& owner) noexcept { return owner->value == 3; }));
        auto const part = second_entry.items.window(fc::RunStart{fc::ElementCount{1}}, fc::ElementCount{2});
        CRUCIBLE_FATAL_INVARIANT(part.is_some());
        CRUCIBLE_FATAL_INVARIANT(entries.element(fc::RunStart{fc::ElementCount{2}}).is_none());
    });
}

// A run slot of elements that are not const takes a run of the frame, and a
// store fills the link slot of each element of that run.  The slot gives its
// elements for a write of their plain fields.
void test_store_fills_a_run_of_arms() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena = fc::mint_bump_arena<TestFamily>(block);
    std::uint32_t const total = fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const branch = frame.template make<Branch>(alloc);
        auto const target = frame.template make<Node>(alloc);
        target->value = 10;
        auto arms =
            frame.template build_array<Arm>(alloc, fc::ElementCount{2}, [](fc::RunStart) noexcept { return Arm{}; });
        auto const second = arms.element(fc::RunStart{fc::ElementCount{1}}).expect("the run holds two arms");
        frame.store(second, &Arm::target, target);
        frame.store(branch, &Branch::arms, arms);
        std::uint32_t sum = 0;
        for (Arm& arm : branch->arms.view()) {
            arm.value = 1;
            for (fc::Ref<Node, TestFamily> const& node : arm.target) {
                sum += node->value;
            }
        }
        for (Arm const& arm : branch->arms.view()) {
            sum += arm.value;
        }
        return sum;
    });
    CRUCIBLE_FATAL_INVARIANT(total == 12);
}

// A link slot of a base takes an object of a derived class: to() of the base
// gives the Ref of the family.
void test_store_links_a_derived_object_to_a_base_slot() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::BumpArena<TestFamily> arena = fc::mint_bump_arena<TestFamily>(block);
    std::uint32_t const extra = fc::with_arena_frame(arena, [alloc](auto& frame) noexcept {
        auto const head = frame.template make<Base>(alloc);
        auto const tail = frame.template make<Derived>(alloc);
        tail->extra = 9;
        tail->depth = 1;
        frame.store(head, &Base::next, tail);
        frame.store(tail, &Base::next, head);
        Base const& linked = *head->next.expect("the store linked the derived node");
        CRUCIBLE_FATAL_INVARIANT(linked.depth == 1 && tail->next.is_some());
        return tail->extra;
    });
    CRUCIBLE_FATAL_INVARIANT(extra == 9);
}

// A frame on the arena of a Box takes the Box by move and gives it back, with
// the value of the body.
void test_frame_on_a_box_gives_the_box_back() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(alloc, Bytes{4096});
    auto back = fc::with_arena_frame(std::move(boxed), [alloc](auto& frame) noexcept {
        auto const first = frame.template make<Node>(alloc);
        auto const second = frame.template make<Node>(alloc);
        frame.store(first, &Node::next, second);
        return first.weaken();
    });
    fc::Ref<Node, TestFamily> const first = back.at(fc::index<0>());
    CRUCIBLE_FATAL_INVARIANT(first->next.is_some() && first->value == 0);
    fc::Box<fc::BumpArena<TestFamily>> again = std::move(back.at(fc::index<1>()));
    fc::Box<fc::BumpArena<TestFamily>> kept = fc::with_arena_frame(std::move(again), [](auto& /*frame*/) noexcept {});
    static_cast<void>(kept);
}

// A parcel carries the Box of an arena and the runs that a frame on it sealed.
// A frame on the parcel gives each run back as a run of the new frame, and the
// door gives the parcel back.
void test_parcel_carries_its_runs_to_a_second_frame() {
    ::foundation::effects::Alloc const alloc = ::foundation::effects::testing::test().alloc;
    fc::Box<fc::BumpArena<TestFamily>> boxed = fc::mint_box<fc::BumpArena<TestFamily>>(alloc, Bytes{4096});
    auto parcel = fc::mint_arena_parcel(std::move(boxed), [alloc](auto& frame) noexcept {
        auto items = frame.array(alloc, std::size_t{4}, std::uint32_t{5});
        auto marks = frame.array(alloc, std::size_t{2}, std::uint32_t{1});
        return frame.seal(items, marks);
    });
    CRUCIBLE_FATAL_INVARIANT(parcel.template view<0>().size() == 4 && parcel.template view<1>().size() == 2);
    // A holder with an empty state, such as a built region, keeps the parcel in
    // an Option.
    fc::Option<decltype(parcel)> held = fc::Option<decltype(parcel)>::some(std::move(parcel));
    CRUCIBLE_FATAL_INVARIANT(held.is_some());
    auto moved = std::move(held).expect("the Option holds the parcel");
    auto back = fc::with_arena_frame(std::move(moved), [alloc](auto& frame, auto& items, auto& marks) noexcept {
        auto const node = frame.template make<Node>(alloc);
        frame.store(node, &Node::items, items);
        std::uint32_t sum = 0;
        for (std::uint32_t const item : node->items.view()) {
            sum += item;
        }
        return sum + static_cast<std::uint32_t>(marks.size());
    });
    CRUCIBLE_FATAL_INVARIANT(back.at(fc::index<0>()) == 22);
    CRUCIBLE_FATAL_INVARIANT(back.at(fc::index<1>()).template view<0>().size() == 4);
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    static_cast<void>(test);
    test_store_fills_the_slots();
    test_const_holder_reads_const();
    test_store_fills_the_slots_of_an_element();
    test_store_fills_a_run_of_arms();
    test_store_links_a_derived_object_to_a_base_slot();
    test_frame_on_a_box_gives_the_box_back();
    test_parcel_carries_its_runs_to_a_second_frame();
    return 0;
}
