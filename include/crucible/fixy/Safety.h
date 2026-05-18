#pragma once

// ── crucible::fixy::safety — Safety wrapper minters under fixy:: ───
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the
// safety-wrapper token mints (Linear / Secret / ScopedView) under
// `fixy::safety::` so callers who include only the fixy umbrella
// never have to descend into the safety/ tree to wrap a value.
//
// ── Cross-reference (fixy-A4-011) ─────────────────────────────────
//
// Linear / Secret / mint_linear / mint_secret / drop are ALSO
// re-exported via `fixy::wrap::` (the "one-stop value-wrapping"
// directory; see Wrap.h header doc, line 17-18 + line 32-33).  The
// dual export is intentional: by-feature carve-outs here, one-stop
// directory there.  Both paths name the SAME substrate symbol via
// `using ::crucible::safety::*` — type identity is drift-checked at
// compile time by `test/test_fixy_umbrella.cpp` (search
// "fixy-A4-011" for the static_assert family).  Callers should pick
// ONE namespace path per TU and stick to it; mixing
// `using namespace fixy::safety; using namespace fixy::wrap;` works
// today only because the using-declarations point at identical
// substrate symbols, and would degenerate to ADL ambiguity the
// moment one path acquires a divergent re-export — the drift-check
// static_asserts catch that the same build.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: every re-export
// preserves the substrate's `std::is_constructible_v<T, Args...>`
// token-mint gate (or the lifetime-bound `Carrier const&` gate for
// ScopedView), the `[[nodiscard]] constexpr noexcept(...)`
// qualifiers, and the wrapper's linearity / classification /
// lifetime-bound discipline.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   safety::Linear<T>              — move-only linear carrier
//   safety::mint_linear<T>(args)   — token mint
//   safety::Secret<T>              — classified-by-default carrier
//   safety::mint_secret<T>(args)   — token mint
//   safety::ScopedView<C, Tag>     — lifetime-bound borrow
//   safety::mint_view<Tag>(c)      — view mint (single chokepoint
//                                    for state-assertion)
//   safety::mint_linear_view<...>  — linear-typed view mint
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — substrate forwards args; alias preserves.
//   TypeSafe — using-declarations preserve concept gates.
//   NullSafe — wrappers are value-typed.
//   MemSafe  — Linear<T>/Secret<T> are move-only; carrier
//              lifetime-bounded views.  Alias preserves.
//   BorrowSafe — ScopedView's CRUCIBLE_LIFETIMEBOUND attribute
//              propagates through the using-declaration.
//   DetSafe  — pure value wrap; bit-exact.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  using-declarations are pure name-lookup directives.

#include <crucible/safety/Linear.h>
#include <crucible/safety/ScopedView.h>
#include <crucible/safety/Secret.h>

namespace crucible::fixy::safety {

// ── Linear (move-only consume-once) ───────────────────────────────

using ::crucible::safety::Linear;
using ::crucible::safety::mint_linear;
using ::crucible::safety::drop;

// ── Secret (classified-by-default) ────────────────────────────────

using ::crucible::safety::Secret;
using ::crucible::safety::mint_secret;

// ── ScopedView (lifetime-bound borrow) ────────────────────────────

using ::crucible::safety::ScopedView;
using ::crucible::safety::mint_view;
using ::crucible::safety::mint_linear_view;

}  // namespace crucible::fixy::safety
