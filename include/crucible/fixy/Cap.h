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

#include <crucible/cntp/CongestionControl.h>
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

// ── Cap-admission concepts (FIXY-V-217) ────────────────────────────
//
// Agent 8 Part 6 + Part 10 #9: production code reaches for
// `effects::is_subrow_v<effects::Row<E>, typename Ctx::row_type>`
// directly when it wants to gate a function on "this ctx admits cap
// E in its current row" (the row-axis check, distinct from
// CtxCanMint's permitted_row-axis check).  No soundness bug — the
// substrate enforcement holds — but the reach is a UX gap: there
// is no named concept, every band-3 site spells the same trait
// invocation, and a future tightening of the row-axis predicate has
// no canonical migration point.
//
// CtxAdmitsCap<Ctx, Cap> closes the gap.  Consumers write
//
//     void worker(Ctx const& ctx)
//         requires fixy::cap::CtxAdmitsCap<Ctx, Effect::Alloc>
//     { ... }
//
// instead of
//
//     void worker(Ctx const& ctx)
//         requires effects::is_subrow_v<effects::Row<Effect::Alloc>,
//                                       typename Ctx::row_type>
//     { ... }
//
// — same compile-time gate, named call site, grep-discoverable
// migration target.  The relationship to the existing concept
// family:
//
//   CtxCanMint<Ctx, E>    — `E ∈ cap_permitted_row<Ctx::cap_type>`
//                           (mint authority: the cap-source COULD
//                           grant E if the row promoted it).
//   CapMatchesCtx<C, Ctx> — `cap_of<C> ∈ Ctx::row_type` over a
//                           Capability<E, S> value type (the
//                           Capability-typed flavour of the
//                           row-axis check).
//   CtxAdmitsCap<Ctx, E>  — `E ∈ Ctx::row_type` (the row-axis
//                           check, taking the Effect enum directly
//                           — enum-arg parity with CtxCanMint, the
//                           form most call sites actually want).
//
// CtxAdmitsCap is the strictly-equivalent row-axis variant of
// CapMatchesCtx — the only difference is the parameter shape.
// Mirroring CtxCanMint's substrate spelling (row_contains_v rather
// than is_subrow_v over a singleton row) keeps the diagnostic
// surface consistent.

template <class Ctx, ::crucible::effects::Effect Cap>
concept CtxAdmitsCap =
    ::crucible::effects::IsExecCtx<Ctx>
    && ::crucible::effects::row_contains_v<
           ::crucible::effects::row_type_of_t<Ctx>, Cap>;

// ── Strict variant: row + NSDMI dual check (FIXY-V-217) ────────────
//
// CtxAdmitsCapStrict<Ctx, Cap> tightens CtxAdmitsCap by ALSO
// verifying that the Ctx's cap-source carries an NSDMI member for
// the requested Effect — i.e. the compile-time field layout
// actually exposes the cap-tag that the row claims authority for.
//
// Within the well-formed-ExecCtx universe this is redundant: the
// substrate's `WellFormedExecCtxAxes` static_assert enforces
// `Row ⊆ cap_permitted_row<Cap>`, and `cap_permitted_row<Cap>`
// matches the NSDMI layout of Cap (Bg has alloc/io/block, Init has
// alloc/io, Test has alloc/io/block).  So a row-admitted effect is
// ALWAYS NSDMI-backed.  CtxAdmitsCapStrict is therefore a
// **defense-in-depth** concept:
//
//   1. Documentation — it pins the row↔NSDMI relationship at the
//      call-site concept layer, not buried inside ExecCtx's class
//      body where the well-formedness static_assert lives.
//   2. Drift detection — if a future revision of
//      `cap_permitted_row` is updated without a corresponding
//      NSDMI update to Bg/Init/Test, CtxAdmitsCapStrict catches
//      the divergence at every consumer call site.
//   3. Synthetic-ctx hardening — if a future axis is added to
//      IsExecCtx that bypasses ExecCtx's well-formedness check
//      (e.g. a duck-typed concept that just demands a `row_type`
//      member), CtxAdmitsCapStrict still rejects synthetic
//      contexts that lie about their row vs cap.
//
// The check is two-leg:
//
//   • Row-axis: CtxAdmitsCap (delegated).
//   • NSDMI-axis: `detail::ctx_cap_source_has_nsdmi_v` — a
//     per-Effect SFINAE-friendly trait that maps each Effect to
//     either a `requires { src.<field>; }` expression for
//     value-effect tags (Alloc/IO/Block → field) or to a
//     `std::same_as<Source, Bg/Init/Test>` identity check for
//     thread-effect tags (the "field" IS the cap source itself).

namespace detail {

template <class CapSource, ::crucible::effects::Effect E>
inline constexpr bool ctx_cap_source_has_nsdmi_v = false;

// Value-effect tags — NSDMI is a per-effect field on the cap source.

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::Alloc>
    = requires(CapSource const& src) { src.alloc; };

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::IO>
    = requires(CapSource const& src) { src.io; };

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::Block>
    = requires(CapSource const& src) { src.block; };

// Thread-effect tags — "NSDMI" is type identity (Bg-cap carries Bg
// authority by being a Bg; Init-cap carries Init authority by being
// an Init; Test-cap carries Test authority by being a Test).

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::Bg>
    = std::is_same_v<CapSource, ::crucible::effects::Bg>;

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::Init>
    = std::is_same_v<CapSource, ::crucible::effects::Init>;

template <class CapSource>
inline constexpr bool ctx_cap_source_has_nsdmi_v<
    CapSource, ::crucible::effects::Effect::Test>
    = std::is_same_v<CapSource, ::crucible::effects::Test>;

}  // namespace detail

template <class Ctx, ::crucible::effects::Effect Cap>
concept CtxAdmitsCapStrict =
    CtxAdmitsCap<Ctx, Cap>
    && detail::ctx_cap_source_has_nsdmi_v<
           ::crucible::effects::cap_type_of_t<Ctx>, Cap>;

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

// ── crucible::fixy::cap::cntp — CNT-P congestion-control minters ────
//
// FIXY-V-212.  Re-exports the two CNT-P §XXI mint factories under
// `fixy::cap::cntp::` so callers who include only the fixy umbrella
// never have to descend into the cntp/ tree to mint a typed
// congestion-control choice.
//
// Both are token mints — they synthesize a fresh `DeclaredCcChoice`
// (a `Tagged<CcSelection, source::CcAlgorithm>`) whose authority
// derives from compile-time type witnesses (the algorithm-enum +
// link-class concept gate, or the CustomCcModule concept).  Neither
// is ctx-bound; both are `[[nodiscard]] constexpr noexcept` and gate
// behind a single concept per §XXI.
//
// ── Substrate consumed ─────────────────────────────────────────────
//
//   cntp::mint_cc_choice<CcAlgorithm, LinkClass>()      — algorithm/link mint
//   cntp::mint_custom_cc_choice<Module, LinkClass>()    — out-of-tree module mint
//   cntp::CcCompatible<Algorithm, Link>                 — concept gate (algorithm)
//   cntp::CustomCcModule<Module>                        — concept gate (module)
//   cntp::DeclaredCcChoice                              — return type carrier
//   cntp::{CcSelection, CcAlgorithm, LinkClass}         — value types
//
// ── Cost ───────────────────────────────────────────────────────────
//
// Zero.  Each `using cntp::*;` is a name-lookup directive; the call
// resolves to the same substrate function with the same concept gate.

namespace crucible::fixy::cap::cntp {

// ── §XXI mint factories ────────────────────────────────────────────
using ::crucible::cntp::mint_cc_choice;
using ::crucible::cntp::mint_custom_cc_choice;

// ── Concept gates (grep-discoverable surface for review) ───────────
using ::crucible::cntp::CcCompatible;
using ::crucible::cntp::CustomCcModule;

// ── Type carriers ──────────────────────────────────────────────────
using ::crucible::cntp::CcAlgorithm;
using ::crucible::cntp::CcSelection;
using ::crucible::cntp::DeclaredCcChoice;
using ::crucible::cntp::LinkClass;

}  // namespace crucible::fixy::cap::cntp

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
// FIXY-V-017: mint_bg_context is a function template gated on
// CanMintBgContext<Key> (§XXI single-concept rule).  Pointer identity
// is taken on the concrete instantiation with the only valid Key.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_bg_context<
                 ::crucible::effects::detail::ctx_mint::bg_key>),
    decltype(&::crucible::effects::mint_bg_context<
                 ::crucible::effects::detail::ctx_mint::bg_key>)>,
    "FIXY-V-017: fixy::cap::mint_bg_context must alias effects::mint_bg_context.");
// FIXY-V-018: mint_init_context is a function template gated on
// CanMintInitContext<Key>.  Identity taken on the concrete instantiation.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_init_context<
                 ::crucible::effects::detail::ctx_mint::init_key>),
    decltype(&::crucible::effects::mint_init_context<
                 ::crucible::effects::detail::ctx_mint::init_key>)>,
    "FIXY-V-018: fixy::cap::mint_init_context must alias effects::mint_init_context.");
// FIXY-V-019: mint_test_context is a function template gated on
// CanMintTestContext<Key>.  Identity taken on the concrete instantiation.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::mint_test_context<
                 ::crucible::effects::detail::ctx_mint::test_key>),
    decltype(&::crucible::effects::mint_test_context<
                 ::crucible::effects::detail::ctx_mint::test_key>)>,
    "FIXY-U-116: fixy::cap::mint_test_context must alias effects::mint_test_context.");

// (5) FIXY-V-212: cntp::mint_cc_choice — template gated on
//     CcCompatible<Algorithm, Link>.  Identity at a concrete
//     (Cubic, CrossDatacenter) instantiation that satisfies the gate.
static_assert(std::is_same_v<
    decltype(&::crucible::fixy::cap::cntp::mint_cc_choice<
                 ::crucible::cntp::CcAlgorithm::Cubic,
                 ::crucible::cntp::LinkClass::CrossDatacenter>),
    decltype(&::crucible::cntp::mint_cc_choice<
                 ::crucible::cntp::CcAlgorithm::Cubic,
                 ::crucible::cntp::LinkClass::CrossDatacenter>)>,
    "FIXY-V-212: fixy::cap::cntp::mint_cc_choice must alias cntp::mint_cc_choice.");

// (6) FIXY-V-212: DeclaredCcChoice type-carrier identity — alias
//     preserves the safety::Tagged<CcSelection, source::CcAlgorithm>
//     instantiation, not just a convertible-to substitute.
static_assert(std::is_same_v<
    ::crucible::fixy::cap::cntp::DeclaredCcChoice,
    ::crucible::cntp::DeclaredCcChoice>,
    "FIXY-V-212: fixy::cap::cntp::DeclaredCcChoice must alias cntp::DeclaredCcChoice.");

// (7) FIXY-V-217: CtxAdmitsCap positive cases — BgDrainCtx claims
//     {Bg, Alloc} in its row, so the concept evaluates true for
//     both effects and false for any effect NOT in the row
//     (e.g. IO, which Bg PERMITS but BgDrainCtx does not CLAIM —
//     the deliberate distinction from CtxCanMint).
static_assert(::crucible::fixy::cap::CtxAdmitsCap<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::Bg>,
    "FIXY-V-217: BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
    "accept Effect::Bg (atom present in the claimed row).");
static_assert(::crucible::fixy::cap::CtxAdmitsCap<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
    "accept Effect::Alloc (atom present in the claimed row).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::IO>,
    "FIXY-V-217: BgDrainCtx::row = Row<Bg, Alloc> — CtxAdmitsCap must "
    "REJECT Effect::IO (not claimed in row, even though Bg permits it; "
    "this is the row-axis check, not the permitted-row-axis check).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<
                  ::crucible::effects::HotFgCtx,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: HotFgCtx::row = Row<> (empty) — CtxAdmitsCap must "
    "REJECT every Effect (no row claim ⇒ no row-axis authority).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCap<
                  int, ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: int is not an ExecCtx — IsExecCtx<int> = false short-"
    "circuits the row-axis clause; the concept evaluates to false "
    "without hard-erroring on row_type_of_t<int>.");

// (8) FIXY-V-217: CtxAdmitsCapStrict — within well-formed ExecCtx
//     (Row ⊆ cap_permitted_row<Cap>) the strict concept is
//     equivalent to the non-strict.  Pinned here as compile-time
//     documentation of the row↔NSDMI invariant.
static_assert(::crucible::fixy::cap::CtxAdmitsCapStrict<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: BgDrainCtx claims Alloc in row AND Bg cap source has "
    "alloc NSDMI ⇒ CtxAdmitsCapStrict accepts.");
static_assert(::crucible::fixy::cap::CtxAdmitsCapStrict<
                  ::crucible::effects::BgCompileCtx,
                  ::crucible::effects::Effect::IO>,
    "FIXY-V-217: BgCompileCtx claims IO in row AND Bg cap source has "
    "io NSDMI ⇒ CtxAdmitsCapStrict accepts.");
static_assert(::crucible::fixy::cap::CtxAdmitsCapStrict<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::Bg>,
    "FIXY-V-217: BgDrainCtx claims Bg in row AND cap source IS Bg ⇒ "
    "CtxAdmitsCapStrict accepts (thread-effect tag path).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCapStrict<
                  ::crucible::effects::HotFgCtx,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: HotFgCtx::row = Row<> ⇒ CtxAdmitsCap fails ⇒ strict "
    "variant fails by conjunction (no row claim).");
static_assert(!::crucible::fixy::cap::CtxAdmitsCapStrict<
                  ::crucible::effects::BgDrainCtx,
                  ::crucible::effects::Effect::Init>,
    "FIXY-V-217: BgDrainCtx cap source is Bg, not Init — even if a "
    "synthetic row claimed Init the cap-source-NSDMI leg would still "
    "fail (defense-in-depth against thread-effect tag confusion).");

// (9) FIXY-V-217: per-Effect NSDMI presence on each canonical cap
//     source — pinned so a future change to cap_permitted_row that
//     adds/removes a value-effect must also touch the NSDMI layout
//     of Bg/Init/Test, OR these assertions catch the drift.
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Bg,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: Bg cap source must expose `alloc` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Bg,
                  ::crucible::effects::Effect::IO>,
    "FIXY-V-217: Bg cap source must expose `io` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Bg,
                  ::crucible::effects::Effect::Block>,
    "FIXY-V-217: Bg cap source must expose `block` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Init,
                  ::crucible::effects::Effect::Alloc>,
    "FIXY-V-217: Init cap source must expose `alloc` NSDMI field.");
static_assert(::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Init,
                  ::crucible::effects::Effect::IO>,
    "FIXY-V-217: Init cap source must expose `io` NSDMI field.");
static_assert(!::crucible::fixy::cap::detail::ctx_cap_source_has_nsdmi_v<
                  ::crucible::effects::Init,
                  ::crucible::effects::Effect::Block>,
    "FIXY-V-217: Init cap source MUST NOT expose `block` NSDMI field — "
    "Init's permitted_row is {Init, Alloc, IO}, no Block; this pin "
    "catches NSDMI drift if Init ever gains a block field without a "
    "permitted_row update.");

}  // namespace crucible::fixy::cap::self_test
