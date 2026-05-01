#pragma once

// ── crucible::effects::Capability — linear cap proof tokens ─────────
//
// A Capability<Cap, Source> is a move-only proof token recording
// that Source S has authorized Effect E in the surrounding scope.
// The bare cap::* tokens shipped in Capabilities.h (cap::Alloc /
// cap::IO / cap::Block) are value-level and copyable — they pass
// freely between functions that need "any cap of this kind".
// Capability<> is the strict-discipline overlay: minted by
// mint_cap<>() from a real Source, consumed exactly once at the
// function call boundary, source-tagged at the type level.
//
//   Axiom coverage: TypeSafe — Source phantom recorded at the type;
//                   cross-source mismatches at call sites surface
//                   as concept-violation diagnostics, not silent
//                   substitution.
//                   MemSafe — move-only; no double-consume.
//                   LeakSafe — empty class, trivially destructible;
//                   consume() is a documentation-grade no-op
//                   because the move IS the consumption.
//   Runtime cost:   zero.  sizeof(Capability<E, S>) == 1; the
//                   Source phantom EBO-collapses inside containing
//                   structs via [[no_unique_address]].
//
// ── When to use ─────────────────────────────────────────────────────
//
// Reach for Capability<> when a function signature needs to encode
// "the caller has proven Source S authorizes Effect E in this
// scope, AND the operation is single-use".  Examples:
//
//     // One-shot allocation in a fork-join task body:
//     void worker(Capability<Effect::Alloc, Bg>&& alloc_cap) {
//         arena.alloc(...);              // performs the alloc
//         std::move(alloc_cap).consume(); // (optional, documents intent)
//         // alloc_cap is moved-from; second alloc would require
//         // re-minting from a Bg in scope.
//     }
//
// The bare cap::* tokens stay valid for the generic case where a
// function just wants "any Alloc cap of any source".
//
// ── Mint authorization (built on cap_permitted_row) ─────────────────
//
// CanMintCap<E, S> is defined as
//
//     row_contains_v<cap_permitted_row_t<S>, E>
//
// — the same trait that gates ExecCtx's Row × Cap coherence
// invariant.  Foreground (ctx_cap::Fg) permits the empty row, so
// no Capability<> can be minted from it; Bg permits {Bg, Alloc, IO,
// Block}; Init permits {Init, Alloc, IO}; Test permits {Test, Alloc,
// IO, Block}.  Adding a new context type (or changing a permitted
// row) automatically updates which capabilities are mintable from
// it — one source of truth.
//
// ── Construction discipline ─────────────────────────────────────────
//
// Capability<>'s default constructor is PRIVATE.  The only path to
// a Capability<E, S> instance is `mint_cap<E>(s)` where `s` is a
// real S and `CanMintCap<E, S>` holds.  The friendship is narrow —
// only the mint_cap function template — so user code cannot forge
// a Capability<> by inheriting / subclassing / SFINAE-tricking.
// Construction is thus the type-level proof: holding a
// Capability<E, S> means SOMEONE in the call chain held a real S
// and called mint_cap<E>(s).
//
// ── Composition with the rest of the wrapper inventory ─────────────
//
//   • Capability<E, S> + ExecCtx<S, …, Row<…, E, …>>: ExecCtx
//     declares the row; Capability is the per-call proof token
//     that the row's Effect E is being exercised right now.
//
//   • Capability<E, S> + safety::Permission<UserTag>: Permission
//     handles ownership ("which thread may write to this channel");
//     Capability handles effect authorization ("which thread is
//     allowed to perform this kind of operation").  Orthogonal —
//     a function may take both: `(Capability<E, S>&&,
//     Permission<UserTag>&&, …)`.
//
//   • Capability<E, S> + safety::Linear<T>: Linear<T> is the
//     general single-consume container; Capability is the
//     specialised effect-cap variant.  No need to nest — Capability
//     already provides linearity.

#include <crucible/Platform.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>      // cap_permitted_row + is_cap_type

#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::effects {

// ── CanMintCap concept ──────────────────────────────────────────────
//
// The mint-authorization predicate.  Reduces to membership in
// Source's permitted row — same trait that powers ExecCtx's cap-row
// coherence static_assert.

template <Effect E, class Source>
concept CanMintCap = is_cap_type_v<Source>
                  && row_contains_v<cap_permitted_row_t<Source>, E>;

// ── Capability<Cap, Source> ─────────────────────────────────────────

template <Effect Cap, class Source>
class [[nodiscard]] Capability {
    // Default constructor is PRIVATE — only mint_cap<>() can call.
    constexpr Capability() noexcept = default;

    // Friendship is intentionally narrow: only the mint factory.
    // Inheritance / subclassing / SFINAE cannot reach the default
    // ctor.  The friend declaration matches mint_cap's signature
    // exactly so substitution failure is structural.
    template <Effect E, class S>
        requires CanMintCap<E, S>
    friend constexpr Capability<E, S> mint_cap(S const&) noexcept;

public:
    // ── Move-only linearity ─────────────────────────────────────────
    Capability(Capability const&)            = delete;
    Capability& operator=(Capability const&) = delete;
    Capability(Capability&&)            noexcept = default;
    Capability& operator=(Capability&&) noexcept = default;
    ~Capability()                                = default;

    // ── Type-level accessors ────────────────────────────────────────
    static constexpr Effect cap_v   = Cap;
    using source_type               = Source;

    // ── Consume marker ──────────────────────────────────────────────
    //
    // Documentation-grade no-op.  rvalue-ref-qualified so callers
    // express "I'm consuming this cap" via `std::move(c).consume()`
    // rather than relying on implicit lifetime end.  The MOVE is
    // the actual consumption (move-from leaves the source unusable
    // by linearity discipline); consume() makes the intent
    // grep-visible at the call site.
    constexpr void consume() && noexcept {}

    // ── Diagnostic ──────────────────────────────────────────────────
    [[nodiscard]] static consteval std::string_view kind_name() noexcept {
        return "Capability";
    }
};

// ── Mint factory ────────────────────────────────────────────────────
//
// The only path to a Capability<E, S> instance.  Constrained on
// CanMintCap<E, S>; the call site fails with a clean concept-
// violation diagnostic if the source-effect pairing is unauthorized.

template <Effect E, class Source>
    requires CanMintCap<E, Source>
[[nodiscard]] constexpr Capability<E, Source> mint_cap(Source const&) noexcept {
    return Capability<E, Source>{};  // friend access to private default ctor
}

// ── Recognition trait + concept ─────────────────────────────────────

template <class T>            struct is_capability                      : std::false_type {};
template <Effect E, class S>  struct is_capability<Capability<E, S>>    : std::true_type  {};
template <class T>            inline constexpr bool is_capability_v = is_capability<T>::value;
template <class T>            concept IsCapability = is_capability_v<T>;

// ── Discrimination ──────────────────────────────────────────────────
//
// cap_matches<T, E>: T is a Capability minted for Effect E, any
// source.  Useful for "I need any Alloc cap, don't care who minted".
//
// HasCapAndSource<T, E, S>: T is exactly Capability<E, S>.  Useful
// when the source matters (e.g., a function that requires a
// Bg-minted cap specifically).

template <class T, Effect E>
struct cap_matches                                : std::false_type {};
template <Effect E, class S>
struct cap_matches<Capability<E, S>, E>           : std::true_type  {};
template <class T, Effect E>
inline constexpr bool cap_matches_v = cap_matches<T, E>::value;

template <class T, Effect E, class S>
concept HasCapAndSource = std::is_same_v<T, Capability<E, S>>;

// ── Extractors ──────────────────────────────────────────────────────
//
// cap_of_v<T>: the Effect atom T was minted for.
// source_of_t<T>: the Source phantom T was minted from.
// Both undefined on non-Capability T (hard error — correct).
//
// Declared HERE (before ExecCtx-driven minting / CapMatchesCtx)
// because those downstream consumers reference cap_of_v.

template <class T> struct cap_of;
template <Effect E, class S>
struct cap_of<Capability<E, S>> { static constexpr Effect value = E; };
template <class T> inline constexpr Effect cap_of_v = cap_of<T>::value;

template <class T> struct source_of;
template <Effect E, class S>
struct source_of<Capability<E, S>> { using type = S; };
template <class T> using source_of_t = typename source_of<T>::type;

// ── ExecCtx-driven minting ──────────────────────────────────────────
//
// mint_from_ctx<E>(Ctx const&) — mint a Capability<E, Source> where
// Source is automatically extracted from the Ctx's cap_type.  The
// constraint CtxCanMint<Ctx, E> is the same authorization gate as
// the bare mint_cap factory; this just saves the caller from spelling
// `typename Ctx::cap_type{}`.
//
// Example:
//   void worker(Ctx const& ctx)
//       requires CtxCanMint<Ctx, Effect::Alloc>
//   {
//       auto alloc_cap = mint_from_ctx<Effect::Alloc>(ctx);
//       arena_alloc(std::move(alloc_cap), ...);
//   }

template <Effect E, IsExecCtx Ctx>
    requires CtxCanMint<Ctx, E>
[[nodiscard]] constexpr Capability<E, cap_type_of_t<Ctx>>
mint_from_ctx(Ctx const&) noexcept {
    using Source = cap_type_of_t<Ctx>;
    return mint_cap<E>(Source{});
}

// ── Cap-Ctx alignment concept ──────────────────────────────────────
//
// CapMatchesCtx<Cap, Ctx>: the Effect carried by Cap is in the Ctx's
// row (i.e., the surrounding scope claims authority for this Effect).
// Useful as a defensive constraint when a function accepts both a
// Capability and a Ctx — the cap might have been minted in a
// different scope, but the current Ctx must still be authorized to
// "see" it.

template <class Cap, class Ctx>
concept CapMatchesCtx = IsCapability<Cap>
                     && IsExecCtx<Ctx>
                     && row_contains_v<row_type_of_t<Ctx>, cap_of_v<Cap>>;

// ── Bare-cap extraction ────────────────────────────────────────────
//
// extract_bare(Capability<E, S>&&) consumes the linear cap and
// returns the corresponding existing cap::* value-token.  This is
// the migration bridge: production code that takes effects::Alloc
// (the bare value-token) by value can be wrapped by callers minting
// a Capability and consuming it via extract_bare.
//
// Only defined for Effect::Alloc / IO / Block — the thread-effect
// atoms (Bg / Init / Test) have no value-level token.
//
// The function is a friend of Capability to access the consume()
// path, but the move IS the consumption — extract_bare just exposes
// the bare token after the move.

template <Effect E, class S>
[[nodiscard]] constexpr cap::Alloc extract_bare(Capability<E, S>&& c) noexcept
    requires (E == Effect::Alloc) {
    std::move(c).consume();
    return cap::Alloc{};
}

template <Effect E, class S>
[[nodiscard]] constexpr cap::IO extract_bare(Capability<E, S>&& c) noexcept
    requires (E == Effect::IO) {
    std::move(c).consume();
    return cap::IO{};
}

template <Effect E, class S>
[[nodiscard]] constexpr cap::Block extract_bare(Capability<E, S>&& c) noexcept
    requires (E == Effect::Block) {
    std::move(c).consume();
    return cap::Block{};
}

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::capability_self_test {

// ── Layout ──────────────────────────────────────────────────────────
static_assert(sizeof(Capability<Effect::Alloc, Bg>) == 1,
    "Capability<E, S> must be 1 byte (empty class with phantom Source)");
static_assert(sizeof(Capability<Effect::IO,    Init>) == 1);
static_assert(sizeof(Capability<Effect::Block, Test>) == 1);

// ── Linearity ───────────────────────────────────────────────────────
static_assert(!std::is_copy_constructible_v<Capability<Effect::Alloc, Bg>>,
    "Capability must NOT be copyable (linearity discipline)");
static_assert(!std::is_copy_assignable_v<Capability<Effect::Alloc, Bg>>);
static_assert( std::is_move_constructible_v<Capability<Effect::Alloc, Bg>>);
static_assert( std::is_move_assignable_v<Capability<Effect::Alloc, Bg>>);
static_assert( std::is_nothrow_move_constructible_v<Capability<Effect::Alloc, Bg>>);

// ── Construction discipline ─────────────────────────────────────────
//
// Default-constructibility is FALSE from outside the class — the
// default ctor is private; only mint_cap<>() reaches it.  Forging
// a Capability<> via direct instantiation is a compile error.
static_assert(!std::is_default_constructible_v<Capability<Effect::Alloc, Bg>>);
static_assert(!std::is_default_constructible_v<Capability<Effect::IO,    Init>>);
static_assert(!std::is_default_constructible_v<Capability<Effect::Block, Test>>);

// ── CanMintCap coverage ─────────────────────────────────────────────
//
// Pin every legal and every illegal (Effect, Source) pair so a
// future revision of cap_permitted_row catches drift.

// Bg permits all four atoms it aggregates plus its own thread tag.
static_assert( CanMintCap<Effect::Alloc, Bg>);
static_assert( CanMintCap<Effect::IO,    Bg>);
static_assert( CanMintCap<Effect::Block, Bg>);
static_assert( CanMintCap<Effect::Bg,    Bg>);
static_assert(!CanMintCap<Effect::Init,  Bg>);
static_assert(!CanMintCap<Effect::Test,  Bg>);

// Init permits Alloc, IO, and its own thread tag — NO Block.
static_assert( CanMintCap<Effect::Alloc, Init>);
static_assert( CanMintCap<Effect::IO,    Init>);
static_assert( CanMintCap<Effect::Init,  Init>);
static_assert(!CanMintCap<Effect::Block, Init>);
static_assert(!CanMintCap<Effect::Bg,    Init>);
static_assert(!CanMintCap<Effect::Test,  Init>);

// Test permits Alloc, IO, Block, and its own thread tag.
static_assert( CanMintCap<Effect::Alloc, Test>);
static_assert( CanMintCap<Effect::IO,    Test>);
static_assert( CanMintCap<Effect::Block, Test>);
static_assert( CanMintCap<Effect::Test,  Test>);
static_assert(!CanMintCap<Effect::Bg,    Test>);
static_assert(!CanMintCap<Effect::Init,  Test>);

// Foreground permits NOTHING.
static_assert(!CanMintCap<Effect::Alloc, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::IO,    ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Block, ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Bg,    ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Init,  ctx_cap::Fg>);
static_assert(!CanMintCap<Effect::Test,  ctx_cap::Fg>);

// Non-cap-type sources are rejected by the is_cap_type_v gate
// (the first conjunct of CanMintCap).
static_assert(!CanMintCap<Effect::Alloc, int>);
static_assert(!CanMintCap<Effect::Alloc, void>);

// ── Recognition + extractors ────────────────────────────────────────
static_assert( is_capability_v<Capability<Effect::Alloc, Bg>>);
static_assert(!is_capability_v<int>);
static_assert(!is_capability_v<Bg>);
static_assert(!is_capability_v<cap::Alloc>);  // bare cap is NOT a Capability

static_assert(cap_of_v<Capability<Effect::Alloc, Bg>> == Effect::Alloc);
static_assert(cap_of_v<Capability<Effect::IO,    Init>> == Effect::IO);
static_assert(cap_of_v<Capability<Effect::Block, Test>> == Effect::Block);

static_assert(std::is_same_v<source_of_t<Capability<Effect::Alloc, Bg>>,   Bg>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::IO,    Init>>, Init>);
static_assert(std::is_same_v<source_of_t<Capability<Effect::Block, Test>>, Test>);

// ── Discrimination ──────────────────────────────────────────────────
static_assert( cap_matches_v<Capability<Effect::Alloc, Bg>,   Effect::Alloc>);
static_assert(!cap_matches_v<Capability<Effect::Alloc, Bg>,   Effect::IO>);
static_assert( cap_matches_v<Capability<Effect::Alloc, Init>, Effect::Alloc>);  // any source
static_assert(!cap_matches_v<int,                              Effect::Alloc>);  // non-cap

static_assert( HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Bg>);
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::Alloc, Init>);  // wrong source
static_assert(!HasCapAndSource<Capability<Effect::Alloc, Bg>, Effect::IO,    Bg>);    // wrong cap

// ── CapMatchesCtx ───────────────────────────────────────────────────
//
// Effect of the Cap must be in Ctx's row.  Source-matching is NOT
// required (caps from different scopes can flow through ctxs that
// authorize the same effect — caps are universal proofs of effect-
// authorization, not source-locked).

static_assert( CapMatchesCtx<Capability<Effect::Bg,    Bg>, BgDrainCtx>);    // Bg in Row<Bg, Alloc>
static_assert( CapMatchesCtx<Capability<Effect::Alloc, Bg>, BgDrainCtx>);    // Alloc in Row<Bg, Alloc>
static_assert(!CapMatchesCtx<Capability<Effect::IO,    Bg>, BgDrainCtx>);    // IO not in Row<Bg, Alloc>
static_assert( CapMatchesCtx<Capability<Effect::IO,    Bg>, BgCompileCtx>);  // IO in compile row
static_assert(!CapMatchesCtx<Capability<Effect::Bg,    Bg>, HotFgCtx>);      // Fg row is empty
static_assert( CapMatchesCtx<Capability<Effect::Test,  Test>, TestRunnerCtx>);

// Source-mismatch is permitted (caps are not source-locked).
static_assert( CapMatchesCtx<Capability<Effect::Alloc, Init>, BgDrainCtx>);  // Init-minted cap, Bg-ctx
static_assert( CapMatchesCtx<Capability<Effect::Alloc, Test>, BgCompileCtx>);

}  // namespace detail::capability_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_capability() noexcept {
    // ── Mint from each authorized source ────────────────────────────
    Bg bg;
    auto bg_alloc = mint_cap<Effect::Alloc>(bg);
    auto bg_io    = mint_cap<Effect::IO>(bg);
    auto bg_block = mint_cap<Effect::Block>(bg);
    auto bg_self  = mint_cap<Effect::Bg>(bg);

    Init init;
    auto init_alloc = mint_cap<Effect::Alloc>(init);
    auto init_io    = mint_cap<Effect::IO>(init);
    auto init_self  = mint_cap<Effect::Init>(init);

    Test test;
    auto test_alloc = mint_cap<Effect::Alloc>(test);
    auto test_block = mint_cap<Effect::Block>(test);

    // ── Move + consume discipline ───────────────────────────────────
    //
    // Linear move: the source is consumed; second use would be a
    // moved-from access (caught by tools like clang-tidy bugprone-
    // use-after-move; the type system itself doesn't model it but
    // the discipline is in place).
    Capability<Effect::Alloc, Bg> moved = std::move(bg_alloc);
    std::move(moved).consume();   // documentation-grade no-op

    // ── Type-level extraction at runtime ────────────────────────────
    static_assert(cap_of_v<decltype(bg_io)>     == Effect::IO);
    static_assert(cap_of_v<decltype(test_block)> == Effect::Block);
    static_assert(std::is_same_v<source_of_t<decltype(init_io)>,   Init>);
    static_assert(std::is_same_v<source_of_t<decltype(test_alloc)>, Test>);

    // ── Concept-based capability check ──────────────────────────────
    static_assert( IsCapability<decltype(bg_io)>);
    static_assert(!IsCapability<int>);

    static_cast<void>(bg_io);
    static_cast<void>(bg_block);
    static_cast<void>(bg_self);
    static_cast<void>(init_alloc);
    static_cast<void>(init_io);
    static_cast<void>(init_self);
    static_cast<void>(test_alloc);
    static_cast<void>(test_block);

    // ── ExecCtx-driven minting ──────────────────────────────────────
    BgDrainCtx     bg_ctx;
    BgCompileCtx   bg_compile_ctx;
    auto from_ctx_alloc = mint_from_ctx<Effect::Alloc>(bg_ctx);
    auto from_ctx_io    = mint_from_ctx<Effect::IO>(bg_compile_ctx);
    static_assert(std::is_same_v<decltype(from_ctx_alloc),
                                  Capability<Effect::Alloc, Bg>>);
    static_assert(std::is_same_v<decltype(from_ctx_io),
                                  Capability<Effect::IO, Bg>>);
    static_cast<void>(from_ctx_alloc);
    static_cast<void>(from_ctx_io);

    // ── Bare-cap extraction (migration bridge) ──────────────────────
    auto a = mint_cap<Effect::Alloc>(bg);
    [[maybe_unused]] cap::Alloc bare_a = extract_bare(std::move(a));

    auto i = mint_cap<Effect::IO>(bg);
    [[maybe_unused]] cap::IO bare_i = extract_bare(std::move(i));

    auto b = mint_cap<Effect::Block>(bg);
    [[maybe_unused]] cap::Block bare_b = extract_bare(std::move(b));
}

}  // namespace crucible::effects
