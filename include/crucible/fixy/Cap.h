#pragma once

// ── crucible::fixy::cap — Capability minters under fixy:: ──────────
//
// Phase C re-export per misc/16_05_2026_fixy.md.  Surfaces the two
// effects/Capability.h minters under `fixy::cap::` so callers who
// include only the fixy umbrella never have to descend into the
// effects/ tree to mint a Capability token.
//
// Per CLAUDE.md §XXI Universal Mint Pattern: re-exports preserve the
// substrate's `requires CanMintCap<E, S>` / `CtxCanMint<Ctx, E>`
// concept gate, the `[[nodiscard]] constexpr noexcept` qualifiers,
// and the two-flavor (token vs ctx-bound) discipline.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   effects::mint_cap<E>(Source const&)          — token mint
//   effects::mint_from_ctx<E>(Ctx const&)        — ctx-bound mint
//   effects::Capability<E, S>                    — capability handle
//   effects::CanMintCap<E, S>                    — token gate
//   effects::CtxCanMint<Ctx, E>                  — ctx-bound gate
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe — re-exports do not introduce any new state path; the
//              substrate's NSDMI discipline carries through.
//   TypeSafe — the using-declarations preserve the substrate's
//              concept-gated overload set; no implicit conversions.
//   NullSafe — re-exports do not own pointers; substrate's
//              value-semantic minting carries through.
//   MemSafe  — Capability is move-only at the substrate; the fixy::
//              alias inherits the same discipline.
//   DetSafe  — empty-class minting is bit-exact across re-export.
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  `using crucible::effects::mint_cap;` is a name-lookup
// directive only; the call resolves to the same substrate function.

#include <crucible/effects/Capability.h>

namespace crucible::fixy::cap {

// ── Token mint — derives authority from a source cap-tag ───────────
//
// `mint_cap<Effect::E>(source)` returns Capability<E, decltype(source)>.
// The single concept gate `CanMintCap<E, Source>` checks that Source
// is a cap_type and that its permitted_row contains E.

using ::crucible::effects::mint_cap;

// ── Ctx-bound mint — derives authority from an ExecCtx ─────────────
//
// `mint_from_ctx<Effect::E>(ctx)` extracts Source from Ctx::cap_type
// and delegates to mint_cap.  The single concept gate
// `CtxCanMint<Ctx, E>` checks the row containment plus IsExecCtx.

using ::crucible::effects::mint_from_ctx;

// ── Re-export the type carrier for grep-discoverable surface ───────

using ::crucible::effects::Capability;

}  // namespace crucible::fixy::cap

// ── Self-test ──────────────────────────────────────────────────────
//
// One witness rides this header: the re-exported `mint_cap` and
// `mint_from_ctx` are the SAME functions as the substrate's, not
// re-declarations.  We assert this via pointer-to-function
// comparison at compile time.

namespace crucible::fixy::cap::self_test {

// Round-trip witness: `fixy::cap::mint_cap` and `effects::mint_cap`
// name the same function template.  We instantiate both at the same
// (E, Source) pair and check the resolved function-pointer types
// match — this proves the using-declaration does not introduce a
// new overload.

template <::crucible::effects::Effect E, class Source>
inline constexpr bool same_mint_cap_v =
    std::is_same_v<
        decltype(&::crucible::fixy::cap::mint_cap<E, Source>),
        decltype(&::crucible::effects::mint_cap<E, Source>)>;

}  // namespace crucible::fixy::cap::self_test
