// ── test_fixy_is — sentinel TU for fixy/Is.h ───────────────────────
//
// Pulls fixy/Is.h into one TU compiled under project warning flags
// so the concept aliases + trait re-exports are instantiated and
// re-checked.  Witnesses:
//
//   1. Every fixy::is::IsX concept alias accepts the canonical
//      positive example (the wrapper type recognized by the
//      substrate's IsX) and rejects an unrelated type.
//   2. Every fixy::is::is_x_v<T> trait alias matches the substrate
//      trait on the same canonical positive / negative carriers.
//   3. fixy::is::IsWitness accepts a canonical witness type;
//      WitnessAtLeast obeys the witness lattice.
//
// HS14: ≥2 fixy_neg fixtures live in test/fixy_neg/neg_fixy_is_*.cpp.

#include <crucible/fixy/Is.h>

#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace fis  = ::crucible::fixy::is;
namespace saf  = ::crucible::safety;
namespace sext = ::crucible::safety::extract;
namespace swit = ::crucible::safety::witness;

// ─── 1. Concept aliases — Linear / Refined / Tagged / Secret ───────

namespace {
// Unrelated baseline.
struct PlainInt { int x = 0; };

// Substrate carriers — recognized by the substrate IsX concepts.
using SubLinear   = saf::Linear<int>;
using SubSecret   = saf::Secret<int>;

// Refined takes an `auto Pred` non-type template parameter; the
// canonical predicates live in safety/Refined.h.  Use `positive` to
// build a recognized Refined<positive, int>.
using SubRefined  = saf::Refined<saf::positive, int>;

// Tagged wants a value type + source tag (any phantom type works).
struct TagSource {};
using SubTagged   = saf::Tagged<int, TagSource>;
}  // namespace

static_assert( fis::IsLinear<SubLinear>,
    "fixy::is::IsLinear accepts safety::Linear<T>.");
static_assert(!fis::IsLinear<PlainInt>,
    "fixy::is::IsLinear rejects unrelated types.");

static_assert( fis::IsSecret<SubSecret>,
    "fixy::is::IsSecret accepts safety::Secret<T>.");
static_assert(!fis::IsSecret<PlainInt>,
    "fixy::is::IsSecret rejects unrelated types.");

static_assert( fis::IsRefined<SubRefined>,
    "fixy::is::IsRefined accepts safety::Refined<Pred, T>.");
static_assert(!fis::IsRefined<PlainInt>,
    "fixy::is::IsRefined rejects unrelated types.");

static_assert( fis::IsTagged<SubTagged>,
    "fixy::is::IsTagged accepts safety::Tagged<T, S>.");
static_assert(!fis::IsTagged<PlainInt>,
    "fixy::is::IsTagged rejects unrelated types.");

// ─── 2. Trait re-export agreement with substrate ───────────────────

static_assert(fis::is_linear_v<SubLinear>   == sext::is_linear_v<SubLinear>,
    "fixy::is::is_linear_v must match the substrate trait.");
static_assert(fis::is_secret_v<SubSecret>   == sext::is_secret_v<SubSecret>,
    "fixy::is::is_secret_v must match the substrate trait.");
static_assert(fis::is_refined_v<SubRefined> == sext::is_refined_v<SubRefined>,
    "fixy::is::is_refined_v must match the substrate trait.");
static_assert(fis::is_tagged_v<SubTagged>   == sext::is_tagged_v<SubTagged>,
    "fixy::is::is_tagged_v must match the substrate trait.");
static_assert(fis::is_linear_v<PlainInt>    == sext::is_linear_v<PlainInt>,
    "Negative trait result must match the substrate.");

// ─── 3. Witness concept alias ──────────────────────────────────────

// Asserted<R> is one of the four canonical witness types; the
// substrate IsWitness recognizes it, and so must the alias.
namespace {
struct WitnessReason {};
using AssertedW = swit::Asserted<WitnessReason>;
}  // namespace

static_assert( fis::IsWitness<AssertedW>,
    "fixy::is::IsWitness must accept Asserted<R>.");
static_assert(!fis::IsWitness<PlainInt>,
    "fixy::is::IsWitness must reject non-witness types.");

// WitnessAtLeast<W, Min> mirrors the lattice — Asserted<R> is at least
// as strong as itself in the witness lattice.
static_assert(fis::WitnessAtLeast<AssertedW, AssertedW>,
    "fixy::is::WitnessAtLeast must be reflexive on the witness lattice.");

int main() {
    return 0;
}
