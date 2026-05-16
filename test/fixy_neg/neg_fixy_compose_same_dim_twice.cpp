// NEGATIVE-COMPILE TEST.  MUST FAIL TO COMPILE.
//
// FIXY-C-STANCE-COMPOSE — `stance::compose<Base, NewGrants...>`
// rejects packs where two NewGrants engage the same dim within a
// single compose call.  The author has to resolve the ambiguity
// either by ordering (compose_no_dedup with explicit last-write-wins)
// or by splitting into two compose calls.
//
// Probe: grant::copy and grant::affine both engage dim::Usage.

#include <crucible/fixy/Stance.h>

namespace cf = crucible::fixy;
namespace cg = crucible::fixy::grant;
namespace cs = crucible::fixy::stance;

// Two grants engaging dim::Usage in one compose pack — REJECTED.
using ConflictedStance = cs::compose_t<cs::PureLinear,
    cg::copy,
    cg::affine>;

static_assert(sizeof(ConflictedStance) > 0,
    "neg fixture must force template instantiation of compose_t");

int main() { return 0; }
