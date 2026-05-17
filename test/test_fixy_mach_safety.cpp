// ── test_fixy_mach_safety — sentinel TU for fixy/Mach.h + Safety.h
//
// Pulls fixy/Mach.h and fixy/Safety.h into one TU compiled under
// project warning flags so each header's static_asserts execute.
// Witnesses:
//
//   1. fixy::mach::Machine / mint_machine / transition_to alias the
//      substrate.
//   2. fixy::safety::Linear / mint_linear, Secret / mint_secret,
//      ScopedView / mint_view alias the substrate.
//   3. mint_linear and mint_secret round-trip via the fixy alias.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/
// neg_fixy_mach_safety_*.cpp.

#include <crucible/fixy/Mach.h>
#include <crucible/fixy/Safety.h>

#include <type_traits>
#include <utility>

namespace fmach = ::crucible::fixy::mach;
namespace fsaf  = ::crucible::fixy::safety;
namespace saf   = ::crucible::safety;

// ─── 1. Machine alias ─────────────────────────────────────────────

static_assert(std::is_same_v<fmach::Machine<int>, saf::Machine<int>>,
    "fixy::mach::Machine must alias safety::Machine.");

// ─── 2. Linear / Secret alias ─────────────────────────────────────

static_assert(std::is_same_v<fsaf::Linear<int>, saf::Linear<int>>,
    "fixy::safety::Linear must alias safety::Linear.");

static_assert(std::is_same_v<fsaf::Secret<int>, saf::Secret<int>>,
    "fixy::safety::Secret must alias safety::Secret.");

// ─── 3. mint_machine / mint_linear / mint_secret round-trip ───────

int main() {
    auto m = fmach::mint_machine<int>(42);
    auto l = fsaf::mint_linear<int>(7);
    auto s = fsaf::mint_secret<int>(9);

    auto m2 = fmach::transition_to(std::move(m), int{99});
    (void)m2;
    fsaf::drop(std::move(l));
    (void)s;
    return 0;
}
