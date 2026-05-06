// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-066 fixture #5: a runtime RecipeSpec<T> with default/unknown
// recipe grade is not a compile-time BITEXACT NumericalTier<T>.  A
// strict receiver must see a type-pinned producer certificate before
// cross-vendor numerics CI ever runs.

#include <crucible/safety/RecipeSpec.h>
#include <crucible/sessions/SessionPayloadSubsort.h>

namespace proto = crucible::safety::proto;
namespace safe  = crucible::safety;

namespace {
struct Tile { float value[4]; };

using DefaultRecipeTile = safe::RecipeSpec<Tile>;
using StrictTile        = safe::NumericalTier<safe::Tolerance::BITEXACT, Tile>;

using Producer = proto::Send<DefaultRecipeTile, proto::End>;
using Consumer = proto::Recv<StrictTile, proto::End>;
}  // namespace

static_assert(proto::CompatibleClient<Producer, Consumer>,
    "NumericalTier_DefaultRecipe_CannotSatisfyStrictRecv");

int main() { return 0; }
