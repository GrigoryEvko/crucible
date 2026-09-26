// HS14 fixture (1/2) — the canonical-order gate at the federation boundary.
//
// MUST fail to compile.  An argument that inverts the canonical wrapper
// order fails the `ArgsCanonicallyOrdered` requires-clause on
// `federation_key` (and on `federation_content_hash` and
// `serialize_computation_cache_federation_entry`).
//
// Stale has canonical_layer_index 8 and Tagged has index 9.  Stale inside
// Tagged (Tagged ⊃ Stale) walks 9 then 8, which is a strict decrease, so
// `CanonicallyOrdered<Tagged<Stale<int>, ...>>` is false.  Without the
// gate this stack projects to a federation cache slot that differs from
// the slot of a peer's canonical `Stale<Tagged<int>, ...>`, and the
// shared cache fragments.  The gate refuses it at the publish site.
//
// Expected diagnostic substrings (the call is ill-formed because no
// federation_key overload satisfies its constraints):
//   * "federation_key"          — the constrained function name
//   * "constraint"              — the requires-clause failed
//   * "ArgsCanonicallyOrdered"  — the specific gate that rejected it

#include <crucible/cipher/ComputationCacheFederation.h>
#include <fixy/Stale.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/effects/Row.h>

namespace fed = crucible::cipher::federation;
namespace eff = ::foundation::effects;

inline void target(int) noexcept {}

// The inverted wrapper stack: Tagged (9) wrapping Stale (8).
using InvertedStack = ::fixy::Tagged<::fixy::Stale<int>, ::fixy::tags::source::FromUser>;

// Force instantiation of the constrained projection at TU scope.  The
// ArgsCanonicallyOrdered<InvertedStack> requires-clause is unsatisfied,
// so this call is ill-formed — the fixture passes by failing to compile.
constexpr auto bad_key = fed::federation_key<&target, eff::Row<>, InvertedStack>();

static_assert(!bad_key.is_zero());
