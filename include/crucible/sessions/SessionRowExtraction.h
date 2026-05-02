#pragma once

// ── crucible::sessions::payload_row<T> — extract a payload's row ────
//
// `payload_row_t<T>` projects a session-payload type to the
// effects::Row it "carries".  Used by SessionMint.h's
// CtxFitsProtocol concept to walk a session-protocol tree and
// verify that every Send<T, K>'s payload is row-admitted by the
// surrounding Ctx; reused by Tier 3's Stage to validate body
// input/output payloads against the stage's Ctx.
//
// Default: bare T carries no effects (Row<>).  Specialisations:
//
//   Computation<R, T>      → R                      (canonical row carrier)
//   Capability<E, S>       → Row<E>                 (sending a cap conveys E)
//   Refined<P, T>          → payload_row<T>          (transparent unwrap)
//   SealedRefined<P, T>    → payload_row<T>          (transparent unwrap)
//   Tagged<T, S>           → payload_row<T>          (transparent unwrap)
//   Linear<T>              → payload_row<T>          (transparent unwrap)
//   Stale<T>               → payload_row<T>          (transparent unwrap)
//
// Composed payloads (Refined<P, Linear<Tagged<Computation<R, T>, S>>>)
// unwrap transparently — every value-level wrapper that "passes
// through" effects specialises to recurse on its element type.  The
// base case (bare T) yields Row<>.
//
//   Axiom coverage: TypeSafe — pure metafunction; mismatches surface
//                   at template-substitution.
//                   InitSafe — no construction at this layer.
//                   DetSafe — consteval throughout.
//   Runtime cost:   zero.
//
// ── Why this lives in sessions/ ─────────────────────────────────────
//
// The trait is consumed by SessionMint's protocol walker AND by
// Tier 3's Stage body-row check.  Putting it in sessions/ keeps it
// near the SessionMint consumer; Tier 3 includes it from
// sessions/SessionRowExtraction.h directly.  Putting it in effects/
// would invert the layering — effects/ is the row vocabulary, not
// the row-extraction infrastructure.
//
// ── Extending payload_row<> for new wrappers ────────────────────────
//
// User code that ships its own value-level wrapper (e.g., a custom
// Refined-style wrapper) can specialise payload_row<MyWrapper<T>> in
// the user's own translation unit.  The default-Row<> base case
// covers any unrecognised type.

#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Capability.h>
#include <crucible/effects/Computation.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Stale.h>
#include <crucible/safety/Tagged.h>
#include <crucible/sessions/SessionContentAddressed.h>

#include <type_traits>

namespace crucible::safety::proto {

// ── payload_row<T> — primary trait + canonical specialisations ──────

// Default: a bare T carries no effects.  Bare ints, doubles, POD
// structs, raw pointers — all yield the empty row.  This is the
// "base of the recursion" for transparent-unwrap specialisations.
template <class T>
struct payload_row {
    using type = ::crucible::effects::Row<>;
};

// Computation<R, T> is THE canonical row carrier.  Sending a
// Computation<R, T> means "this payload was produced under row R";
// the receiver inherits the obligation that R was authorized.
template <class R, class T>
struct payload_row<::crucible::effects::Computation<R, T>> {
    using type = R;
};

// Capability<E, S>: sending the cap CONVEYS its effect.  The
// receiver gains authority to perform E.  Source S is informational
// at the row level — the row carries only E.
template <::crucible::effects::Effect E, class S>
struct payload_row<::crucible::effects::Capability<E, S>> {
    using type = ::crucible::effects::Row<E>;
};

// ── Transparent-unwrap specialisations ──────────────────────────────
//
// Each value-level wrapper that "passes through" effects (Refined,
// SealedRefined, Tagged, Linear, Stale) specialises payload_row to
// recurse on its element type.  The default-base-case handles bare
// T at the bottom of the chain.

template <auto Pred, class T>
struct payload_row<::crucible::safety::Refined<Pred, T>>
    : payload_row<T> {};

template <auto Pred, class T>
struct payload_row<::crucible::safety::SealedRefined<Pred, T>>
    : payload_row<T> {};

template <class T, class Tag>
struct payload_row<::crucible::safety::Tagged<T, Tag>>
    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Linear<T>>
    : payload_row<T> {};

template <class T>
struct payload_row<::crucible::safety::Stale<T>>
    : payload_row<T> {};

// ContentAddressed<T> is a session-level payload marker that quotients
// payloads by content hash (Appendix D.5).  It carries no effects of
// its own — the underlying T is what carries the row.  Transparent
// unwrap.
template <class T>
struct payload_row<ContentAddressed<T>>
    : payload_row<T> {};

// ── User alias ──────────────────────────────────────────────────────

template <class T>
using payload_row_t = typename payload_row<T>::type;

// ── Self-test block ─────────────────────────────────────────────────
namespace detail::payload_row_self_test {

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

// ── Default: bare types yield Row<> ────────────────────────────────
static_assert(std::is_same_v<payload_row_t<int>,    eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<double>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<void*>,  eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<char>,   eff::Row<>>);

struct UserPod { int x; double y; };
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

// ── Computation<R, T> yields R ─────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<>, int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg>, int>>,
    eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, double>>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

// ── Capability<E, S> yields Row<E> ─────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::Alloc, eff::Bg>>,
    eff::Row<eff::Effect::Alloc>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::IO, eff::Init>>,
    eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<
    payload_row_t<eff::Capability<eff::Effect::Block, eff::Test>>,
    eff::Row<eff::Effect::Block>>);

// ── Refined<P, T> unwraps ──────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Refined<saf::positive, int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Refined<saf::positive,
                                eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
    eff::Row<eff::Effect::Bg>>);

// ── SealedRefined unwraps similarly ────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::SealedRefined<saf::positive,
                                      eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
    eff::Row<eff::Effect::IO>>);

// ── Linear<T> unwraps ──────────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Linear<eff::Computation<eff::Row<eff::Effect::IO>, int>>>,
    eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Linear<int>>,
    eff::Row<>>);

// ── Tagged<T, Tag> unwraps ─────────────────────────────────────────
struct ProvTag {};
static_assert(std::is_same_v<
    payload_row_t<saf::Tagged<eff::Computation<eff::Row<eff::Effect::Bg>, int>, ProvTag>>,
    eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<
    payload_row_t<saf::Tagged<int, ProvTag>>,
    eff::Row<>>);

// ── Stale<T> unwraps ───────────────────────────────────────────────
static_assert(std::is_same_v<
    payload_row_t<saf::Stale<eff::Computation<eff::Row<eff::Effect::Alloc>, int>>>,
    eff::Row<eff::Effect::Alloc>>);

// ── Composed unwrap chains ─────────────────────────────────────────
//
// The payload_row<> chain handles arbitrary nesting transparently:
// Refined<P, Linear<Tagged<Computation<R, T>, Tag>>> → R.

using ComposedT =
    saf::Refined<saf::positive,
        saf::Linear<
            saf::Tagged<
                eff::Computation<eff::Row<eff::Effect::Bg, eff::Effect::Alloc>, int>,
                ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<ComposedT>,
    eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>);

using FourLayerT =
    saf::Linear<
        saf::Refined<saf::positive,
            saf::Tagged<
                saf::Stale<eff::Computation<eff::Row<eff::Effect::IO>, int>>,
                ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<FourLayerT>,
    eff::Row<eff::Effect::IO>>);

// ── Composed unwrap that bottoms out at bare T → Row<> ─────────────
using BarelyComposedT =
    saf::Refined<saf::positive,
        saf::Linear<
            saf::Tagged<int, ProvTag>>>;
static_assert(std::is_same_v<
    payload_row_t<BarelyComposedT>,
    eff::Row<>>);

// ── ContentAddressed<T> unwraps ────────────────────────────────────
//
// CA is a session-level quotient marker; the underlying T's row is
// preserved.  Composes with the other unwrap chains.

static_assert(std::is_same_v<
    payload_row_t<ContentAddressed<int>>,
    eff::Row<>>);

static_assert(std::is_same_v<
    payload_row_t<ContentAddressed<eff::Computation<eff::Row<eff::Effect::Bg>, int>>>,
    eff::Row<eff::Effect::Bg>>);

// Composed: ContentAddressed<Refined<P, Computation<R, T>>> → R.
using CaRefinedT =
    ContentAddressed<saf::Refined<saf::positive,
        eff::Computation<eff::Row<eff::Effect::IO>, int>>>;
static_assert(std::is_same_v<
    payload_row_t<CaRefinedT>,
    eff::Row<eff::Effect::IO>>);

}  // namespace detail::payload_row_self_test

// ── Runtime smoke test ──────────────────────────────────────────────

[[gnu::cold]] inline void runtime_smoke_test_payload_row() noexcept {
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;

    // Bare T → Row<>.  Compile-time check on a runtime-context type.
    static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);

    // Capability minted at runtime; payload_row recovers the effect.
    eff::Bg bg;
    auto cap = eff::mint_cap<eff::Effect::Alloc>(bg);
    static_assert(std::is_same_v<payload_row_t<decltype(cap)>,
                                  eff::Row<eff::Effect::Alloc>>);
    static_cast<void>(cap);

    // Composed payload at runtime (just type-level, no construction —
    // the composed type uses Trusted refined construction below).
    using ComposedT =
        saf::Refined<saf::positive,
            eff::Computation<eff::Row<eff::Effect::Bg>, int>>;
    static_assert(std::is_same_v<payload_row_t<ComposedT>,
                                  eff::Row<eff::Effect::Bg>>);
}

}  // namespace crucible::safety::proto
