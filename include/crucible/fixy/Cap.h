#pragma once

// ── crucible::fixy::cap — Capability minters under fixy:: ──────────
//
// Re-export per misc/16_05_2026_fixy.md.  Surfaces the two
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

// ── Cap-context mints — passkey-gated Bg/Init/Test factories ───────
//
// FIXY-U-116: `mint_bg_context` / `mint_init_context` / `mint_test_context`
// are §XXI passkey-gated token mints (substrate at
// effects/Capabilities.h:393-409).  Each takes a `detail::ctx_mint::*_key`
// by value and trades it for a fresh Bg/Init/Test context.  Passkey ctors
// are private + friend-restricted (BackgroundThread / Vigil /
// effects::testing::TestWitness), so calling the mint requires friend
// access — the re-export is purely the §XXI grep-target enumerating
// every authorization point.  The substrate is reachable through
// `fixy::eff::` via Eff.h's namespace alias too; this fixy::cap:: surface
// mirrors the `mint_cap` / `mint_from_ctx` dual-export precedent and
// gives the gen-mint-inventory scanner an explicit using-decl to find.
using ::crucible::effects::mint_bg_context;
using ::crucible::effects::mint_init_context;
using ::crucible::effects::mint_test_context;

}  // namespace crucible::fixy::cap

// ── Self-test ──────────────────────────────────────────────────────
//
// Three witnesses ride this header (fixy-M-02: previously the block
// defined a `same_mint_cap_v` template but never fired it — false
// advertising).  Each witness pins one structural promise:
//
//   1. `fixy::cap::mint_cap` is the substrate function, not a re-
//      declaration.  Function-pointer type identity at a concrete
//      (E, Source) pair that satisfies `CanMintCap` proves the
//      using-decl does NOT introduce a new overload.
//   2. `fixy::cap::mint_from_ctx` is the substrate function, same
//      mechanism over a (E, Ctx) pair that satisfies `CtxCanMint`.
//   3. `fixy::cap::Capability<E, S>` IS `effects::Capability<E, S>`
//      (template-identity, not an alias-template that happens to
//      have the same instantiations).

namespace crucible::fixy::cap::self_test {

template <::crucible::effects::Effect E, class Source>
inline constexpr bool same_mint_cap_v =
    std::is_same_v<
        decltype(&::crucible::fixy::cap::mint_cap<E, Source>),
        decltype(&::crucible::effects::mint_cap<E, Source>)>;

template <::crucible::effects::Effect E, class Ctx>
inline constexpr bool same_mint_from_ctx_v =
    std::is_same_v<
        decltype(&::crucible::fixy::cap::mint_from_ctx<E, Ctx>),
        decltype(&::crucible::effects::mint_from_ctx<E, Ctx>)>;

// (1) mint_cap identity — Bg is a cap_type whose permitted_row
//     contains Alloc and IO, so CanMintCap is satisfied for both.
static_assert(same_mint_cap_v<::crucible::effects::Effect::Alloc,
                              ::crucible::effects::Bg>,
    "fixy::cap::mint_cap must alias the substrate function — the "
    "using-decl did not introduce a new overload (Alloc row).");
static_assert(same_mint_cap_v<::crucible::effects::Effect::IO,
                              ::crucible::effects::Bg>,
    "fixy::cap::mint_cap must alias the substrate function (IO row).");

// (2) mint_from_ctx identity — BgDrainCtx's cap_type is Bg, whose
//     permitted_row contains Alloc, so CtxCanMint is satisfied.
static_assert(same_mint_from_ctx_v<::crucible::effects::Effect::Alloc,
                                   ::crucible::effects::BgDrainCtx>,
    "fixy::cap::mint_from_ctx must alias the substrate function.");

// (3) Capability type-carrier identity — alias preserves template
//     identity (same instantiation, not just convertible).
static_assert(std::is_same_v<
    ::crucible::fixy::cap::Capability<
        ::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>,
    ::crucible::effects::Capability<
        ::crucible::effects::Effect::Alloc, ::crucible::effects::Bg>>,
    "fixy::cap::Capability must alias effects::Capability.");

// (4) FIXY-U-116: cap-context mints — non-template free functions, so
//     pointer-identity is direct.  Each substrate mint takes a private-
//     ctor passkey by value; the using-decls do not duplicate the
//     overload (a `using ::ns::fn;` brings the name in without
//     introducing a new declaration), and these asserts pin that
//     promise.  Drift between fixy:: and substrate fails HERE.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_bg_context),
    decltype(&::crucible::effects::mint_bg_context)>,
    "FIXY-U-116: fixy::cap::mint_bg_context must alias effects::mint_bg_context.");
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_init_context),
    decltype(&::crucible::effects::mint_init_context)>,
    "FIXY-U-116: fixy::cap::mint_init_context must alias effects::mint_init_context.");
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_test_context),
    decltype(&::crucible::effects::mint_test_context)>,
    "FIXY-U-116: fixy::cap::mint_test_context must alias effects::mint_test_context.");

}  // namespace crucible::fixy::cap::self_test
