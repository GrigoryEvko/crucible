// Sentinel TU for the consume tracker in fixy/Qtt.h, compiled with
// CRUCIBLE_QTT_TRACK_CONSUME=1.
//
// test_qtt.cpp is this same header with the tracker off, and it states
// what the wrapper costs there: sizeof(T), a trivial move, a trivial
// destructor.  This file states what the tracker buys.  The deleted
// copy and the rvalue-qualified consume already make a copy
// unspellable and make a second consume need an explicit std::move,
// which is at-most-once.  Exactly-once is the two facts below, and
// neither is in any signature: a second consume must be refused, and a
// value that ends its scope still owing a discharge must be reported.
//
// Every cell is a pair.  A use that must abort is shown to abort, and
// the neighbouring legitimate use is shown not to, because a tracker
// that fired on everything would pass the first half of every cell
// while making the wrapper useless.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Qtt.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::Affine;
using ::fixy::Linear;
using ::fixy::check::expect_dies;

// The reports of the tracker, each with the grade that it names.
constexpr ::fixy::TextView kLinearConsumedTwice{
    "foundation: consumed twice failed: constexpr T fixy::Qtt<Grade, T>::consume() && [with auto Grade = "
    "foundation::algebra::lattices::QttGrade::One"};
constexpr ::fixy::TextView kAffineConsumedTwice{
    "foundation: consumed twice failed: constexpr T fixy::Qtt<Grade, T>::consume() && [with auto Grade = "
    "foundation::algebra::lattices::QttGrade::AtMostOne"};
constexpr ::fixy::TextView kLinearReadAfterConsume{
    "foundation: read after consume failed: constexpr const T& fixy::Qtt<Grade, T>::peek() const & [with auto Grade = "
    "foundation::algebra::lattices::QttGrade::One"};
constexpr ::fixy::TextView kAffineReadAfterConsume{
    "foundation: read after consume failed: constexpr const T& fixy::Qtt<Grade, T>::peek() const & [with auto Grade = "
    "foundation::algebra::lattices::QttGrade::AtMostOne"};
constexpr ::fixy::TextView kLinearWrittenAfterConsume{
    "foundation: written after consume failed: constexpr T& fixy::Qtt<Grade, T>::peek_mut() & [with auto Grade = "
    "foundation::algebra::lattices::QttGrade::One"};
constexpr ::fixy::TextView kNeverDischargedAtScopeEnd{
    "foundation: linear obligation never discharged failed: constexpr fixy::detail::ConsumeTrackerImpl<Grade, T, "
    "true>::~ConsumeTrackerImpl()"};
constexpr ::fixy::TextView kNeverDischargedAtAssignment{
    "foundation: linear obligation never discharged failed: constexpr fixy::detail::ConsumeTrackerImpl<Grade, T, "
    "true>& fixy::detail::ConsumeTrackerImpl<Grade, T, true>::operator="};
// The reports of a wrapper that keeps the state in the niche of its payload.
constexpr ::fixy::TextView kNicheNeverDischargedAtScopeEnd{
    "foundation: linear obligation never discharged failed: constexpr fixy::Qtt<Grade, T>::~Qtt()"};
constexpr ::fixy::TextView kNicheNeverDischargedAtAssignment{
    "foundation: linear obligation never discharged failed: constexpr fixy::Qtt<Grade, T>& fixy::Qtt<Grade, "
    "T>::operator="};

// A payload with a niche: the empty value of an Index<8> is 8, which no
// Index<8> holds.
using Slot = ::fixy::Index<8>;

// Without the key this TU would prove nothing, and it would prove it
// quietly: every death test below would end its child with no abort.
// So the key is asserted rather than assumed.
static_assert(::fixy::qtt_consume_tracked,
              "test_qtt_tracked must be compiled with CRUCIBLE_QTT_TRACK_CONSUME=1; see test/fixy/CMakeLists.txt");

// The state is one bool, so the wrapper grows by the alignment it has
// to round up to and no more.
static_assert(sizeof(Linear<std::uint32_t>) <= sizeof(std::uint32_t) + alignof(std::uint32_t));
static_assert(sizeof(Affine<std::uint64_t>) <= sizeof(std::uint64_t) + alignof(std::uint64_t));

// A payload with a niche keeps the state in its own bytes, so the wrapper
// keeps the size of the payload.
static_assert(sizeof(Linear<Slot>) == sizeof(Slot));
static_assert(sizeof(Affine<Slot>) == sizeof(Slot));

// A destructor that can say something is a destructor that runs.
static_assert(!std::is_trivially_destructible_v<Linear<int>>);
static_assert(!std::is_trivially_destructible_v<Affine<int>>);
static_assert(!std::is_trivially_destructible_v<Linear<Slot>>);
static_assert(std::is_nothrow_move_constructible_v<Linear<Slot>>);

// The tracker holds the obligation, not a copy of it: the wrapper is
// still move-only, and the move is still noexcept.
static_assert(!std::is_copy_constructible_v<Linear<int>>);
static_assert(!std::is_copy_assignable_v<Linear<int>>);
static_assert(std::is_nothrow_move_constructible_v<Linear<int>>);
static_assert(std::is_nothrow_move_assignable_v<Linear<int>>);

}  // namespace

// Each use that must abort runs in a child through expect_dies of the Check
// family.  Each legitimate use runs in the test process, so a report that
// it gave would end the test.
int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};

    // ── The first consume, and the second ────────────────────────────
    //
    // One consume spends the usage the grade counts.  A second one
    // would move out of a value that has already been moved out of,
    // which is the use-after-consume this whole rail exists for.
    [] noexcept {
        auto once = ::fixy::mint_linear<int>(7);
        (void)std::move(once).consume();
    }();

    expect_dies(
        test,
        [] noexcept {
            auto twice = ::fixy::mint_linear<int>(7);
            (void)std::move(twice).consume();
            (void)std::move(twice).consume();
        },
        kLinearConsumedTwice);

    // The same fact at the other grade.  An Affine value may be
    // consumed once or never, and "once" still means once.
    expect_dies(
        test,
        [] noexcept {
            auto twice = ::fixy::mint_affine<int>(7);
            (void)std::move(twice).consume();
            (void)std::move(twice).consume();
        },
        kAffineConsumedTwice);

    // ── Reading, before and after ────────────────────────────────────
    [] noexcept {
        auto live = ::fixy::mint_linear<int>(3);
        if (live.peek() != 3) std::abort();
        live.peek_mut() = 4;
        if (live.peek() != 4) std::abort();
        (void)std::move(live).consume();
    }();

    expect_dies(
        test,
        [] noexcept {
            auto spent = ::fixy::mint_linear<int>(3);
            (void)std::move(spent).consume();
            (void)spent.peek();
        },
        kLinearReadAfterConsume);

    expect_dies(
        test,
        [] noexcept {
            auto spent = ::fixy::mint_linear<int>(3);
            (void)std::move(spent).consume();
            spent.peek_mut() = 9;
        },
        kLinearWrittenAfterConsume);

    // ── The move carries the obligation ──────────────────────────────
    //
    // After a move the destination owes the discharge and the source
    // owes nothing, so the source's own scope end is silent and a
    // consume through the source is the second consume.
    [] noexcept {
        auto source = ::fixy::mint_linear<int>(5);
        auto destination = std::move(source);
        (void)std::move(destination).consume();
    }();

    expect_dies(
        test,
        [] noexcept {
            auto source = ::fixy::mint_linear<int>(5);
            auto destination = std::move(source);
            (void)std::move(source).consume();
            (void)std::move(destination).consume();
        },
        kLinearConsumedTwice);

    // ── The scope end ────────────────────────────────────────────────
    //
    // This is the half that makes the grade Linear rather than Affine.
    // A Linear that reaches the end of its scope with the obligation
    // open is reported; an Affine that does is a first-class outcome.
    expect_dies(
        test,
        [] noexcept {
            auto forgotten = ::fixy::mint_linear<int>(1);
            (void)forgotten.peek();
        },
        kNeverDischargedAtScopeEnd);

    [] noexcept {
        auto abandoned = ::fixy::mint_affine<int>(1);
        (void)abandoned.peek();
    }();

    // drop is the spelling for "discharged deliberately, without
    // wanting the value", so it satisfies the obligation.
    [] noexcept {
        auto discarded = ::fixy::mint_linear<int>(1);
        drop(std::move(discarded));
    }();

    // And a drop is a discharge at the other grade too, so a use after
    // one reads as a use after consume rather than as a fresh value.
    expect_dies(
        test,
        [] noexcept {
            auto discarded = ::fixy::mint_affine<int>(1);
            drop(std::move(discarded));
            (void)discarded.peek();
        },
        kAffineReadAfterConsume);

    // ── Assignment over a live wrapper ───────────────────────────────
    //
    // `held = std::move(other)` releases whatever `held` carried
    // through T's own move assignment, which is a discharge by neither
    // consume nor drop.  It is the scope-end case one line earlier.
    expect_dies(
        test,
        [] noexcept {
            auto held = ::fixy::mint_linear<int>(1);
            auto other = ::fixy::mint_linear<int>(2);
            held = std::move(other);
            drop(std::move(held));
        },
        kNeverDischargedAtAssignment);

    // Assigning over a wrapper that has already discharged is the
    // ordinary reuse of a variable, and it is not reported.
    [] noexcept {
        auto held = ::fixy::mint_linear<int>(1);
        (void)std::move(held).consume();
        auto other = ::fixy::mint_linear<int>(2);
        held = std::move(other);
        drop(std::move(held));
    }();

    // ── swap moves the obligation with the value ─────────────────────
    //
    // After the swap the spent side is the live one.  Discharging the
    // side that was live before the swap is therefore a second
    // consume, which is what shows the state travelled.
    expect_dies(
        test,
        [] noexcept {
            auto live = ::fixy::mint_linear<int>(1);
            auto spent = ::fixy::mint_linear<int>(2);
            (void)std::move(spent).consume();
            swap(live, spent);
            drop(std::move(live));
        },
        kLinearConsumedTwice);

    // And the other side of the same swap discharges cleanly.
    [] noexcept {
        auto live = ::fixy::mint_linear<int>(1);
        auto spent = ::fixy::mint_linear<int>(2);
        (void)std::move(spent).consume();
        swap(live, spent);
        drop(std::move(spent));
    }();

    // ── The state in the niche of the payload ────────────────────────
    //
    // A consumed wrapper of an Index holds the empty value of the niche, so
    // each cell above holds for it too, with no byte beside the payload.
    [] noexcept {
        auto once = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<3>{});
        auto moved = std::move(once);
        if (moved.peek().raw() != 3) ::fixy::fatal("a move of a Linear of an Index keeps its index");
        if (std::move(moved).consume().raw() != 3) ::fixy::fatal("a consume of a Linear of an Index gives its index");
        auto discarded = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<4>{});
        drop(std::move(discarded));
        auto abandoned = ::fixy::mint_affine<Slot>(::fixy::StaticIndex<5>{});
        (void)abandoned.peek();
    }();

    expect_dies(
        test,
        [] noexcept {
            auto twice = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<3>{});
            (void)std::move(twice).consume();
            (void)std::move(twice).consume();
        },
        kLinearConsumedTwice);

    expect_dies(
        test,
        [] noexcept {
            auto source = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<3>{});
            auto destination = std::move(source);
            (void)source.peek();
            (void)std::move(destination).consume();
        },
        kLinearReadAfterConsume);

    // The planted drop: a Linear of an Index that ends its scope live.
    expect_dies(
        test,
        [] noexcept {
            auto forgotten = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<1>{});
            (void)forgotten.peek();
        },
        kNicheNeverDischargedAtScopeEnd);

    expect_dies(
        test,
        [] noexcept {
            auto held = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<1>{});
            auto other = ::fixy::mint_linear<Slot>(::fixy::StaticIndex<2>{});
            held = std::move(other);
            drop(std::move(held));
        },
        kNicheNeverDischargedAtAssignment);

    return 0;
}
