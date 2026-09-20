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

#include <fixy/Qtt.h>

#include "../foundation/abort_probe.h"

#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace {

using ::fixy::Affine;
using ::fixy::Linear;
using ::foundation::test::aborts;

// Without the key this TU would prove nothing, and it would prove it
// quietly: every `aborts` expectation below would simply report false.
// So the key is asserted rather than assumed.
static_assert(::fixy::qtt_consume_tracked,
              "test_qtt_tracked must be compiled with CRUCIBLE_QTT_TRACK_CONSUME=1; see test/fixy/CMakeLists.txt");

// The state is one bool, so the wrapper grows by the alignment it has
// to round up to and no more.
static_assert(sizeof(Linear<std::uint32_t>) <= sizeof(std::uint32_t) + alignof(std::uint32_t));
static_assert(sizeof(Affine<std::uint64_t>) <= sizeof(std::uint64_t) + alignof(std::uint64_t));

// A destructor that can say something is a destructor that runs.
static_assert(!std::is_trivially_destructible_v<Linear<int>>);
static_assert(!std::is_trivially_destructible_v<Affine<int>>);

// The tracker holds the obligation, not a copy of it: the wrapper is
// still move-only, and the move is still noexcept.
static_assert(!std::is_copy_constructible_v<Linear<int>>);
static_assert(!std::is_copy_assignable_v<Linear<int>>);
static_assert(std::is_nothrow_move_constructible_v<Linear<int>>);
static_assert(std::is_nothrow_move_assignable_v<Linear<int>>);

}  // namespace

int main() {
    // ── The first consume, and the second ────────────────────────────
    //
    // One consume spends the usage the grade counts.  A second one
    // would move out of a value that has already been moved out of,
    // which is the use-after-consume this whole rail exists for.
    if (aborts([] {
            auto once = ::fixy::mint_linear<int>(7);
            (void)std::move(once).consume();
        }))
        return 10;

    if (!aborts([] {
            auto twice = ::fixy::mint_linear<int>(7);
            (void)std::move(twice).consume();
            (void)std::move(twice).consume();
        }))
        return 11;

    // The same fact at the other grade.  An Affine value may be
    // consumed once or never, and "once" still means once.
    if (!aborts([] {
            auto twice = ::fixy::mint_affine<int>(7);
            (void)std::move(twice).consume();
            (void)std::move(twice).consume();
        }))
        return 12;

    // ── Reading, before and after ────────────────────────────────────
    if (aborts([] {
            auto live = ::fixy::mint_linear<int>(3);
            if (live.peek() != 3) std::abort();
            live.peek_mut() = 4;
            if (live.peek() != 4) std::abort();
            (void)std::move(live).consume();
        }))
        return 20;

    if (!aborts([] {
            auto spent = ::fixy::mint_linear<int>(3);
            (void)std::move(spent).consume();
            (void)spent.peek();
        }))
        return 21;

    if (!aborts([] {
            auto spent = ::fixy::mint_linear<int>(3);
            (void)std::move(spent).consume();
            spent.peek_mut() = 9;
        }))
        return 22;

    // ── The move carries the obligation ──────────────────────────────
    //
    // After a move the destination owes the discharge and the source
    // owes nothing, so the source's own scope end is silent and a
    // consume through the source is the second consume.
    if (aborts([] {
            auto source = ::fixy::mint_linear<int>(5);
            auto destination = std::move(source);
            (void)std::move(destination).consume();
        }))
        return 30;

    if (!aborts([] {
            auto source = ::fixy::mint_linear<int>(5);
            auto destination = std::move(source);
            (void)std::move(source).consume();
            (void)std::move(destination).consume();
        }))
        return 31;

    // ── The scope end ────────────────────────────────────────────────
    //
    // This is the half that makes the grade Linear rather than Affine.
    // A Linear that reaches the end of its scope with the obligation
    // open is reported; an Affine that does is a first-class outcome.
    if (!aborts([] {
            auto forgotten = ::fixy::mint_linear<int>(1);
            (void)forgotten.peek();
        }))
        return 40;

    if (aborts([] {
            auto abandoned = ::fixy::mint_affine<int>(1);
            (void)abandoned.peek();
        }))
        return 41;

    // drop is the spelling for "discharged deliberately, without
    // wanting the value", so it satisfies the obligation.
    if (aborts([] {
            auto discarded = ::fixy::mint_linear<int>(1);
            drop(std::move(discarded));
        }))
        return 42;

    // And a drop is a discharge at the other grade too, so a use after
    // one reads as a use after consume rather than as a fresh value.
    if (!aborts([] {
            auto discarded = ::fixy::mint_affine<int>(1);
            drop(std::move(discarded));
            (void)discarded.peek();
        }))
        return 43;

    // ── Assignment over a live wrapper ───────────────────────────────
    //
    // `held = std::move(other)` releases whatever `held` carried
    // through T's own move assignment, which is a discharge by neither
    // consume nor drop.  It is the scope-end case one line earlier.
    if (!aborts([] {
            auto held = ::fixy::mint_linear<int>(1);
            auto other = ::fixy::mint_linear<int>(2);
            held = std::move(other);
            drop(std::move(held));
        }))
        return 50;

    // Assigning over a wrapper that has already discharged is the
    // ordinary reuse of a variable, and it is not reported.
    if (aborts([] {
            auto held = ::fixy::mint_linear<int>(1);
            (void)std::move(held).consume();
            auto other = ::fixy::mint_linear<int>(2);
            held = std::move(other);
            drop(std::move(held));
        }))
        return 51;

    // ── swap moves the obligation with the value ─────────────────────
    //
    // After the swap the spent side is the live one.  Discharging the
    // side that was live before the swap is therefore a second
    // consume, which is what shows the state travelled.
    if (!aborts([] {
            auto live = ::fixy::mint_linear<int>(1);
            auto spent = ::fixy::mint_linear<int>(2);
            (void)std::move(spent).consume();
            swap(live, spent);
            drop(std::move(live));
        }))
        return 60;

    // And the other side of the same swap discharges cleanly.
    if (aborts([] {
            auto live = ::fixy::mint_linear<int>(1);
            auto spent = ::fixy::mint_linear<int>(2);
            (void)std::move(spent).consume();
            swap(live, spent);
            drop(std::move(spent));
        }))
        return 61;

    return 0;
}
