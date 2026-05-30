// fix-08 HS14 fixture (1/2) — federation publish-boundary canonical gate
//
// MUST fail to compile: presenting a §XVI-INVERTED wrapper stack as an
// Arg to the federation key projection violates the
// `ArgsCanonicallyOrdered` requires-clause that fix-08 wired onto
// `federation_key` (and `federation_content_hash` /
// `serialize_computation_cache_federation_entry`).
//
// Tagged has canonical_layer_index 11 and Stale has index 10; nesting
// Stale INSIDE Tagged (Tagged ⊃ Stale) walks 11 then 10 — a strict
// DECREASE — so `CanonicallyOrdered<Tagged<Stale<int>, ...>>` is false.
// Before fix-08 this stack would silently project to a federation cache
// slot DISTINCT from a canonical-order peer's `Stale<Tagged<int>, ...>`,
// fragmenting the L16 computation genome.  The boundary now rejects it
// at the publish site instead.
//
// Expected diagnostic substrings (the call is ill-formed because no
// federation_key overload satisfies its constraints):
//   * "federation_key"          — the constrained function name
//   * "constraint"              — the requires-clause failed
//   * "ArgsCanonicallyOrdered"  — the specific gate that rejected it

#include <crucible/cipher/ComputationCacheFederation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>

namespace fed = crucible::cipher::federation;
namespace sf  = crucible::safety;
namespace eff = crucible::effects;

inline void target(int) noexcept {}

// §XVI-inverted wrapper stack: Tagged(11) wrapping Stale(10).
using InvertedStack = sf::Tagged<sf::Stale<int>, sf::source::FromUser>;

// Force instantiation of the constrained projection at TU scope.  The
// ArgsCanonicallyOrdered<InvertedStack> requires-clause is unsatisfied,
// so this call is ill-formed — the fixture passes by failing to compile.
constexpr auto bad_key =
    fed::federation_key<&target, eff::Row<>, InvertedStack>();

static_assert(!bad_key.is_zero());
