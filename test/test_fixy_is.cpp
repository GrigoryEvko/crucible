// Sentinel TU: compiles the alias header under the project warning flags so its
// concept aliases and trait re-exports are instantiated.

#include <crucible/fixy/Is.h>

#include <crucible/safety/_Linear.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/witness/Witness.h>

#include <cstdint>
#include <type_traits>

namespace fis = ::crucible::fixy::is;
namespace saf = ::crucible::safety;
namespace sext = ::crucible::safety::extract;
namespace swit = ::crucible::safety::witness;

namespace {
struct PlainInt {
    int x = 0;
};

using SubLinear = saf::Linear<int>;
using SubSecret = saf::Secret<int>;
using SubRefined = saf::Refined<saf::positive, int>;

struct TagSource {};
using SubTagged = saf::Tagged<int, TagSource>;
}  // namespace

static_assert(fis::IsLinear<SubLinear>, "fixy::is::IsLinear accepts safety::Linear<T>.");
static_assert(!fis::IsLinear<PlainInt>, "fixy::is::IsLinear rejects unrelated types.");

static_assert(fis::IsSecret<SubSecret>, "fixy::is::IsSecret accepts safety::Secret<T>.");
static_assert(!fis::IsSecret<PlainInt>, "fixy::is::IsSecret rejects unrelated types.");

static_assert(fis::IsRefined<SubRefined>, "fixy::is::IsRefined accepts safety::Refined<Pred, T>.");
static_assert(!fis::IsRefined<PlainInt>, "fixy::is::IsRefined rejects unrelated types.");

static_assert(fis::IsTagged<SubTagged>, "fixy::is::IsTagged accepts safety::Tagged<T, S>.");
static_assert(!fis::IsTagged<PlainInt>, "fixy::is::IsTagged rejects unrelated types.");

static_assert(fis::is_linear_v<SubLinear> == sext::is_linear_v<SubLinear>,
              "fixy::is::is_linear_v must match the substrate trait.");
static_assert(fis::is_secret_v<SubSecret> == sext::is_secret_v<SubSecret>,
              "fixy::is::is_secret_v must match the substrate trait.");
static_assert(fis::is_refined_v<SubRefined> == sext::is_refined_v<SubRefined>,
              "fixy::is::is_refined_v must match the substrate trait.");
static_assert(fis::is_tagged_v<SubTagged> == sext::is_tagged_v<SubTagged>,
              "fixy::is::is_tagged_v must match the substrate trait.");
static_assert(fis::is_linear_v<PlainInt> == sext::is_linear_v<PlainInt>,
              "Negative trait result must match the substrate.");

namespace {
struct WitnessReason {};
using AssertedW = swit::Asserted<WitnessReason>;
}  // namespace

static_assert(fis::IsWitness<AssertedW>, "fixy::is::IsWitness must accept Asserted<R>.");
static_assert(!fis::IsWitness<PlainInt>, "fixy::is::IsWitness must reject non-witness types.");

static_assert(fis::WitnessAtLeast<AssertedW, AssertedW>,
              "fixy::is::WitnessAtLeast must be reflexive on the witness lattice.");

int main() { return 0; }
