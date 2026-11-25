#pragma once

// The count of the open lends of an owner.
//
//   LendCount<Word>     the count of the frame doors that lend, at one time,
//                       what an owner holds.  Word is the unsigned word of the
//                       storage of the count, which the owner picks
//   count.lend()        a Lend, which counts as one open lend until its end
//   count.is_lent()     true while one lend of the count is open
//
// A frame door lends what its owner holds to a body for the time of the call.
// The rule borrow-match refuses a change of the owner that the body names.  A
// function that the body calls can still change the owner, and so can a second
// name of it.  So a frame door opens a Lend around its body.  A door of the owner
// that moves or ends what a frame door lends is a change door: it tests
// is_lent() first, and it ends the process while a lend is open.
//
// What the count proves
//   A change door that finds no open lend runs while no body of a frame door of
//   the owner runs on the thread of the call.  A frame door that opens in the
//   body of another one counts too.  The lends end in the reverse order of their
//   opens, so the count is zero only after the last body returned.
//
// What the count does not prove
//   - Nothing about a second thread.  The steps of the count are plain, so a
//     frame door and a change door of one owner on two threads race on it.  An
//     owner with a LendCount has one owner thread
//   - Nothing about a borrow that outlives its body.  The gates of the frame door
//     keep it in: HoldsNoBorrow of foundation/core/Choice.h and
//     StoresNoBorrowOutside of foundation/core/CaptureStore.h
//   - Nothing about a door that does not test it.  The end of an owner is a
//     change door too
//   - Nothing about a write of a value in its place, which moves and ends nothing
//   - Nothing at compile time.  A type or a key that keeps each change out of a
//     body makes the count unnecessary
//
// Width of the word
//   A count of one or two bytes ends the process at the lend past its largest
//   value.  A count of four bytes or more has no such test.  Each open lend stands
//   in a frame of one thread, and a stack of 2^32 frames takes at least 64 GiB.
//   So a lend of such a count costs two plain steps of one word.
//
// The count is trivially copyable, because the owner core of
// foundation/core/Owner.h copies the state of an owner by its bytes.  A state
// refuses a move while it is lent, so a copied count is zero.

#include <foundation/Platform.h>
#include <foundation/core/Report.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

// The word of a LendCount: an unsigned integer that is no bool.
template <class Word>
concept LendWord = std::is_integral_v<Word> && std::is_unsigned_v<Word> && !std::is_same_v<Word, bool>;

namespace detail {

// The fatal exit of a lend past the largest value of the word of a count.  The
// call takes no argument, so the test of a lend adds one compare and one branch.
[[noreturn]] CRUCIBLE_COLD inline void report_lend_count_overflow_() noexcept {
    fatal("more frame doors lend what one owner holds at one time than the word of its LendCount can count");
}

}  // namespace detail

template <class Word>
    requires LendWord<Word>
class LendCount final {
    Word open_ = 0;

public:
    // One open lend of a count, until its end.  It lives in one frame door.
    class [[nodiscard]] Lend final {
        Word& open_;

        friend class LendCount;
        constexpr explicit Lend(Word& open) noexcept : open_{open} {
            if constexpr (sizeof(Word) < sizeof(std::uint32_t)) {
                if (open_ == static_cast<Word>(~Word{0})) [[unlikely]] {
                    detail::report_lend_count_overflow_();
                }
            }
            ++open_;
        }

    public:
        constexpr ~Lend() { --open_; }

        Lend(Lend const&) = delete("a lend lives in one frame door, so no copy of it counts a second one");
        Lend& operator=(Lend const&) = delete("a lend lives in one frame door, so no copy of it counts a second one");
    };

    // No open lend.
    constexpr LendCount() noexcept = default;

    // Opens one lend, which the caller keeps for the time of one body.  The lend
    // refers to the count, so a lend of a temporary count is refused.
    [[nodiscard]] constexpr Lend lend() & noexcept { return Lend{open_}; }
    Lend lend() && = delete("a lend of a temporary LendCount counts in an object that the full expression ends");

    // True while one lend is open.
    [[nodiscard]] constexpr bool is_lent() const noexcept { return open_ != 0; }
};

}  // namespace foundation::core
