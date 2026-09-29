// The execution context, exercised as scenarios rather than as claims
// about the shipped type.
//
// Ctx.h keeps its static_assert wall, because each of those assertions
// reads a shipped concept or trait against the witness shapes.  This
// file holds the four things that construct a scenario: the promotion
// chain, the with_cap probe, the CanTakeInitCap concept, and the
// run-time body that drives every operation.

#include <foundation/effects/Ctx.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;
using fe::Row;
using fe::detail::ctx_witnesses::BgIoWitness;
using fe::detail::ctx_witnesses::BgWitness;
using fe::detail::ctx_witnesses::FgWitness;
using fe::detail::ctx_witnesses::InitWitness;
using fe::detail::ctx_witnesses::TestWitnessCtx;

// The returned values carry no meaning.  What the calls prove is that
// the overloads resolve at all.
template <class Ctx>
    requires fe::CtxOwnsAnyOf<Ctx, Effect::Init, Effect::Bg>
[[nodiscard]] int needs_init_or_bg(Ctx const&) noexcept {
    return 42;
}

template <class Ctx>
    requires fe::CtxOwnsAllOf<Ctx, Effect::Bg, Effect::Alloc>
[[nodiscard]] int needs_bg_and_alloc(Ctx const&) noexcept {
    return 99;
}

// The rejection side asserts on the concept rather than on a call.  It
// is the same predicate the requires-clause consults.
static_assert(fe::CtxOwnsAnyOf<BgWitness, Effect::Init, Effect::Bg>);
static_assert(fe::CtxOwnsAnyOf<InitWitness, Effect::Init, Effect::Bg>);
static_assert(!fe::CtxOwnsAnyOf<FgWitness, Effect::Init, Effect::Bg>);
static_assert(fe::CtxOwnsAllOf<BgWitness, Effect::Bg, Effect::Alloc>);
static_assert(!fe::CtxOwnsAllOf<InitWitness, Effect::Bg, Effect::Alloc>);
static_assert(!fe::CtxOwnsAllOf<FgWitness, Effect::Bg>);

// A context is two empty axes and one byte.
static_assert(sizeof(fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>) == 1);

// A promotion takes a real capability, and a row widens only from the
// source itself.  The foreground context and the source both come from
// the testing witnesses.  Production code takes the foreground context
// from the producer claim, and promotes with the capability its own mint
// returned.
constexpr auto ctx0 = fe::testing::foreground();
constexpr auto ctx1 = ctx0.with_cap(fe::testing::bg());
static_assert(std::is_same_v<typename decltype(ctx1)::cap_type, fe::Bg>);
static_assert(std::is_same_v<typename decltype(ctx1)::row_type, Row<>>);

// A context built from the source claims what the source permits, and
// it narrows its row twice.
constexpr fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>> ctx2{fe::testing::bg()};
constexpr auto ctx3 = ctx2.in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
static_assert(std::is_same_v<typename decltype(ctx3)::row_type, Row<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(std::is_same_v<typename decltype(ctx3)::cap_type, fe::Bg>);

constexpr auto ctx4 = ctx3.in_row<Row<Effect::Bg, Effect::Alloc>>();
static_assert(std::is_same_v<typename decltype(ctx4)::row_type, Row<Effect::Bg, Effect::Alloc>>);

// A narrowed context does not widen back.  The atom it dropped needs the
// source as evidence, and the narrowed context does not lend its source.
template <class C, class R>
concept CanClaim = requires(C const& c) { c.template in_row<R>(); };
template <class C>
concept LendsItsSource = requires(C const& c) { c.cap(); };
static_assert(!CanClaim<decltype(ctx4), Row<Effect::Bg, Effect::Alloc, Effect::IO>>);
static_assert(!CanClaim<decltype(ctx1), Row<Effect::Bg>>);
static_assert(LendsItsSource<decltype(ctx2)> && !LendsItsSource<decltype(ctx3)> && !LendsItsSource<decltype(ctx4)>);

// Every link of the chain is still one byte.
static_assert(sizeof(ctx0) == 1 && sizeof(ctx1) == 1 && sizeof(ctx2) == 1 && sizeof(ctx3) == 1 && sizeof(ctx4) == 1);

// Promoting the empty row to a background capability is admitted.
// Moving a background row to an initialization capability is not, and
// would have to narrow the row first.
constexpr auto bg_promoted = fe::testing::foreground().with_cap(fe::testing::bg());
static_assert(std::is_same_v<typename decltype(bg_promoted)::cap_type, fe::Bg>);
static_assert(std::is_same_v<typename decltype(bg_promoted)::row_type, Row<>>);

template <class C>
concept CanTakeInitCap = requires(C const& c) { c.with_cap(fe::testing::init()); };
static_assert(CanTakeInitCap<FgWitness>);
static_assert(!CanTakeInitCap<BgWitness>, "A row that names Bg cannot move under an Init source.");

// Every operation driven with non-constant arguments.  The
// static_assert walls only prove the constant-evaluated path.
void every_operation_runs_at_run_time() {
    // Each context is handed the capability it claims: a context is not
    // evidence of a capability, it carries one.  The foreground source
    // comes from the key of the producer claim, here the test door.
    FgWitness fg = fe::testing::foreground();
    // A copy of a held foreground context stays legal, and it is free.
    FgWitness fg_copy = fg;
    fg_copy = fg;
    BgWitness bg{fe::testing::bg()};
    [[maybe_unused]] BgIoWitness bg_io{fe::testing::bg()};
    [[maybe_unused]] InitWitness init{fe::testing::init()};
    [[maybe_unused]] TestWitnessCtx test_ctx{fe::testing::test()};

    [[maybe_unused]] auto s1 = sizeof(fg);
    [[maybe_unused]] auto s2 = sizeof(bg);

    // The capability member is reachable through the borrowing accessor,
    // from a context that claims every atom of it, and nowhere else.
    fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>> load{fe::testing::bg()};
    [[maybe_unused]] fe::Bg const& held = load.cap();
    [[maybe_unused]] fe::cap::Alloc alloc_tag = held.alloc;

    auto narrowed = load.in_row<Row<Effect::Bg, Effect::Alloc, Effect::Block>>();
    static_assert(std::is_same_v<typename decltype(narrowed)::row_type, Row<Effect::Bg, Effect::Alloc, Effect::Block>>);
    [[maybe_unused]] auto s3 = sizeof(narrowed);
    [[maybe_unused]] BgWitness drain = bg.in_row<Row<Effect::Bg, Effect::Alloc>>();
}

}  // namespace

int main() {
    every_operation_runs_at_run_time();

    // Each context is handed the capability it claims.
    BgWitness bg{fe::testing::bg()};
    InitWitness init{fe::testing::init()};
    if (needs_init_or_bg(bg) != 42) return 1;
    if (needs_init_or_bg(init) != 42) return 2;
    if (needs_bg_and_alloc(bg) != 99) return 3;

    // The borrowed capability is the source's own member, not a copy.
    fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>> load{fe::testing::bg()};
    fe::Bg const& held = load.cap();
    [[maybe_unused]] fe::cap::Block block = held.block;
    return 0;
}
