// The deaths of the lend count (foundation/core/LendCount.h) of a SmallList and
// of a GrowText.  A frame door lends the storage of a SmallList or of a
// GrowText to its body.  Each change below comes from a
// function that the body calls.  Each such change ends the process in each
// build: an append, also when the storage has room, a clear, a move, a freeze
// and the end of the owner.  The move of the owner into the frame of with_mut
// (foundation/core/Frame.h) is such a move.  More than 255 lends of one owner
// end the process too, and append_self refuses a window outside its text.
//
// The rule borrow-match refuses a callee that ends an owner of the lent
// storage.  The stop at run time is the subject of the test, so a PROBE
// region keeps each such call: the append, the move and the freeze.
//
// Each death runs in a child process through expect_dies of the Check
// family, which requires SIGABRT and the text of the refusal.  The controls
// then show that the same changes run after the frame, and that 255 nested
// lends of a const owner run.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Grow.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/SmallList.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

namespace {

namespace core = ::foundation::core;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

constexpr ::fixy::TextView kLentChange{
    "an append, a clear, a move, a freeze or the end of a growable owner while a frame door lends its storage"};
constexpr ::fixy::TextView kLendOverflow{
    "more frame doors lend what one owner holds at one time than the word of its LendCount can count"};

[[nodiscard]] ::foundation::effects::Alloc allocation() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

using IntList = core::SmallList<int, 2>;

// A list of three integers.  Its heap storage holds one cache line of
// integers, so an append of a fourth one does not grow the storage.
[[nodiscard]] IntList small_list() noexcept {
    IntList list = core::mint_small_list<int, 2>(core::Quantity<core::unit::Element, std::size_t>{64});
    for (int value = 1; value <= 3; ++value) {
        list.append(allocation(), value).expect("the list is below its maximum");
    }
    return list;
}

[[nodiscard]] core::GrowText small_text() noexcept {
    core::GrowText text = core::mint_grow_text(64);
    text.append(allocation(), "abc").expect("the text is below its maximum");
    return text;
}

// The changes that a body calls.  Each one is opaque to the optimizer, so
// the death comes from the count and not from a fold of the frame.
[[gnu::noipa]] void append_to(IntList& list) noexcept {
    list.append(allocation(), 4).expect("the list is below its maximum");
}

[[gnu::noipa]] void clear(IntList& list) noexcept { list.clear(); }

[[gnu::noipa]] std::size_t move_out(IntList& list) noexcept {
    IntList const taken = std::move(list);
    return taken.size();
}

[[gnu::noipa]] void append_to(core::GrowText& text) noexcept {
    text.append(allocation(), 'd').expect("the text is below its maximum");
}

[[gnu::noipa]] std::size_t freeze_text(core::GrowText& text) noexcept {
    core::HeapText const frozen = core::freeze(std::move(text));
    return frozen.size();
}

// A FixedList counts no lend of its elements, so its clear ends the lent list
// with no earlier check, and the list itself sees its end.
[[gnu::noipa]] void drop(core::FixedList<IntList, 1>& held) noexcept { held.clear(); }

// The frame door of an owner moves the list into its frame and gives it back.
[[gnu::noipa]] std::size_t frame_of(IntList& list) noexcept {
    auto [count, back] = core::with_mut(std::move(list), [](auto out) noexcept { return out.size(); });
    list = std::move(back);
    return count;
}

// Nests depth lends of one const owner, one in the body of the next.
template <class Owner>
[[gnu::noipa]] std::size_t lend_nested(Owner const& owner, int depth) noexcept {
    return owner.with_view(
        [&owner, depth](auto view) noexcept { return depth == 1 ? view.size() : lend_nested(owner, depth - 1); });
}

void append_from_callee() noexcept {
    IntList list = small_list();
    static_cast<void>(list.with_view([&list](core::View<int const> view) noexcept {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the lend count stops a change of a growable owner in a callee of a frame")
        append_to(list);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        return view.size();
    }));
}

void clear_from_callee() noexcept {
    IntList list = small_list();
    static_cast<void>(list.with_view([&list](core::View<int const> view) noexcept {
        clear(list);
        return view.size();
    }));
}

void move_from_callee() noexcept {
    IntList list = small_list();
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the lend count stops a change of a growable owner in a callee of a frame")
    static_cast<void>(
        list.with_view([&list](core::View<int const> view) noexcept { return move_out(list) + view.size(); }));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

void text_append_from_callee() noexcept {
    core::GrowText text = small_text();
    static_cast<void>(text.with_view([&text](::fixy::TextView characters) noexcept {
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the lend count stops a change of a growable owner in a callee of a frame")
        append_to(text);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        return characters.size();
    }));
}

void freeze_from_callee() noexcept {
    core::GrowText text = small_text();
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the lend count stops a change of a growable owner in a callee of a frame")
    static_cast<void>(text.with_view(
        [&text](::fixy::TextView characters) noexcept { return freeze_text(text) + characters.size(); }));
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

void end_from_callee() noexcept {
    core::FixedList<IntList, 1> held{};
    static_cast<void>(held.append(small_list()));
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the end of a growable owner in the body of its frame ends the process")
    for (IntList const& list : std::as_const(held).view()) {
        static_cast<void>(list.with_view([&held](core::View<int const> view) noexcept {
            drop(held);
            return view.size();
        }));
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
}

// A callee of a frame door takes the lent list into the frame of with_mut.
// The move would give the lent storage to a second frame.
void unique_frame_from_callee() noexcept {
    IntList list = small_list();
    static_cast<void>(
        list.with_view([&list](core::View<int const> view) noexcept { return frame_of(list) + view.size(); }));
}

// The 256th nested lend of one owner, past the count of one byte.
void overflow_the_list_count() noexcept {
    IntList const list = small_list();
    static_cast<void>(lend_nested(list, 256));
}

void overflow_the_text_count() noexcept {
    core::GrowText const text = small_text();
    static_cast<void>(lend_nested(text, 256));
}

void append_self_outside() noexcept {
    core::GrowText text = small_text();
    static_cast<void>(text.append_self(allocation(), 2, 4));
}

}  // namespace

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    expect_dies(test, append_from_callee, kLentChange);
    expect_dies(test, clear_from_callee, kLentChange);
    expect_dies(test, move_from_callee, kLentChange);
    expect_dies(test, text_append_from_callee, kLentChange);
    expect_dies(test, freeze_from_callee, kLentChange);
    expect_dies(test, end_from_callee, kLentChange);
    expect_dies(test, unique_frame_from_callee, kLentChange);
    expect_dies(test, overflow_the_list_count, kLendOverflow);
    expect_dies(test, overflow_the_text_count, kLendOverflow);
    expect_dies(test, append_self_outside,
                ::fixy::TextView{"append_self got the 4 characters from 2 of a text of 3 characters"});

    // The controls: the same changes after the frame, and 255 nested lends.
    IntList list = small_list();
    std::size_t const before = list.with_view([](core::View<int const> view) noexcept { return view.size(); });
    append_to(list);
    CRUCIBLE_TEST_ASSERT(test, frame_of(list) == 4 && list.size() == 4);
    clear(list);
    CRUCIBLE_TEST_ASSERT(test, before == 3 && list.size() == 0);
    IntList const nested = small_list();
    CRUCIBLE_TEST_ASSERT(test, lend_nested(nested, 255) == 3);
    core::GrowText const nested_text = small_text();
    CRUCIBLE_TEST_ASSERT(test, lend_nested(nested_text, 255) == 3);
    core::GrowText text = small_text();
    std::size_t const characters = text.with_view([](::fixy::TextView view) noexcept { return view.size(); });
    append_to(text);
    CRUCIBLE_TEST_ASSERT(test, characters == 3 && freeze_text(text) == 4);

    pass(test, "test_core_grow_lend: PASSED\n");
    return 0;
}
