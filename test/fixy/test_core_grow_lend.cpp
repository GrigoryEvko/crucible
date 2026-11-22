// The deaths of the lend count of foundation/core/Grow.h.  A frame door
// lends the storage of a growable owner to its body.  The rule borrow-match
// reads the body, so each change below comes from a function that the body
// calls, which the rule does not read.  Each such change ends the process in
// each build: an append, also when the storage has room, a clear, a move, a
// freeze and the end of the owner.  The move of the owner into the frame of
// with_mut (foundation/core/Frame.h) is such a move.  More than 255 lends of
// one owner end the process too, and append_self refuses a window outside
// its text.
//
// Each death runs in a child process through expect_dies of the Check
// family, which requires SIGABRT and the text of the refusal.  The controls
// then show that the same changes run after the frame, and that 255 nested
// lends of a const owner run.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/core/Grow.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <utility>

namespace {

namespace core = ::foundation::core;
using ::fixy::check::expect_dies;
using ::fixy::check::pass;

constexpr ::fixy::TextView kLentChange{
    "an append, a clear, a move, a freeze or the end of a growable owner while a frame door lends its storage"};

[[nodiscard]] ::foundation::effects::Alloc allocation() noexcept {
    return ::foundation::effects::testing::test().alloc;
}

// A list of three integers.  Its first storage holds one cache line of
// integers, so an append of a fourth one does not grow the storage.
[[nodiscard]] core::GrowList<int> small_list() noexcept {
    core::GrowList<int> list = core::mint_grow_list<int>(64);
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
[[gnu::noipa]] void append_to(core::GrowList<int>& list) noexcept {
    list.append(allocation(), 4).expect("the list is below its maximum");
}

[[gnu::noipa]] void clear(core::GrowList<int>& list) noexcept { list.clear(); }

[[gnu::noipa]] std::size_t move_out(core::GrowList<int>& list) noexcept {
    core::GrowList<int> const taken = std::move(list);
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
[[gnu::noipa]] void drop(core::FixedList<core::GrowList<int>, 1>& held) noexcept { held.clear(); }

// The frame door of an owner moves the list into its frame and gives it back.
[[gnu::noipa]] std::size_t frame_of(core::GrowList<int>& list) noexcept {
    auto [count, back] = core::with_mut(std::move(list), [](auto out) noexcept { return out.size(); });
    list = std::move(back);
    return count;
}

// Nests depth lends of one const list, one in the body of the next.
[[gnu::noipa]] std::size_t lend_nested(core::GrowList<int> const& list, int depth) noexcept {
    return list.with_view([&list, depth](core::View<int const> view) noexcept {
        return depth == 1 ? view.size() : lend_nested(list, depth - 1);
    });
}

void append_from_callee() noexcept {
    core::GrowList<int> list = small_list();
    static_cast<void>(list.with_view([&list](core::View<int const> view) noexcept {
        append_to(list);
        return view.size();
    }));
}

void clear_from_callee() noexcept {
    core::GrowList<int> list = small_list();
    static_cast<void>(list.with_element(
        0,
        [&list](int& first) noexcept {
            clear(list);
            return first;
        },
        [] noexcept { return 0; }));
}

void move_from_callee() noexcept {
    core::GrowList<int> list = small_list();
    static_cast<void>(
        list.with_view([&list](core::View<int const> view) noexcept { return move_out(list) + view.size(); }));
}

void text_append_from_callee() noexcept {
    core::GrowText text = small_text();
    static_cast<void>(text.with_view([&text](::fixy::TextView characters) noexcept {
        append_to(text);
        return characters.size();
    }));
}

void freeze_from_callee() noexcept {
    core::GrowText text = small_text();
    static_cast<void>(text.with_view(
        [&text](::fixy::TextView characters) noexcept { return freeze_text(text) + characters.size(); }));
}

void end_from_callee() noexcept {
    core::FixedList<core::GrowList<int>, 1> held{};
    static_cast<void>(held.append(small_list()));
    for (core::GrowList<int> const& list : std::as_const(held).view()) {
        static_cast<void>(list.with_view([&held](core::View<int const> view) noexcept {
            drop(held);
            return view.size();
        }));
    }
}

// A callee of a frame door takes the lent list into the frame of with_mut.
// The move would give the lent storage to a second frame.
void unique_frame_from_callee() noexcept {
    core::GrowList<int> list = small_list();
    static_cast<void>(
        list.with_view([&list](core::View<int const> view) noexcept { return frame_of(list) + view.size(); }));
}

void overflow_the_count() noexcept {
    core::GrowList<int> const list = small_list();
    static_cast<void>(lend_nested(list, 256));
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
    expect_dies(test, overflow_the_count,
                ::fixy::TextView{"more than 255 frame doors lend the storage of one growable owner at one time"});
    expect_dies(test, append_self_outside,
                ::fixy::TextView{"append_self got the 4 characters from 2 of a text of 3 characters"});

    // The controls: the same changes after the frame, and 255 nested lends.
    core::GrowList<int> list = small_list();
    std::size_t const before = list.with_view([](core::View<int const> view) noexcept { return view.size(); });
    append_to(list);
    CRUCIBLE_TEST_ASSERT(test, frame_of(list) == 4 && list.size() == 4);
    clear(list);
    CRUCIBLE_TEST_ASSERT(test, before == 3 && list.size() == 0);
    core::GrowList<int> const nested = small_list();
    CRUCIBLE_TEST_ASSERT(test, lend_nested(nested, 255) == 3);
    core::GrowText text = small_text();
    std::size_t const characters = text.with_view([](::fixy::TextView view) noexcept { return view.size(); });
    append_to(text);
    CRUCIBLE_TEST_ASSERT(test, characters == 3 && freeze_text(text) == 4);

    pass(test, "test_core_grow_lend: PASSED\n");
    return 0;
}
