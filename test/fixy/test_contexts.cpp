// Sentinel TU for fixy/Ctx.h: the production contexts are the rows the
// old tree declared, or one of those rows widened by Block, and the
// shapes foundation recorded.  A body gated on a row resolves for the
// contexts that own it and for no other, and every context is built at
// run time from the capability it claims.
//
// foundation/effects/Ctx.h pins the shapes in its record of the named
// contexts, and fixy/Ctx.h names that record.  This file restates each
// row through the production names, so a rewrite of the record reddens
// here too.  It adds the scenarios: a gated call, a row narrowed from one
// production context into another, and the runtime construction, which
// needs a real capability that a test takes from foundation's testing
// witness.

#include <fixy/Ctx.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>
#include <foundation/effects/Row.h>

#include <type_traits>

namespace {

namespace fe = ::foundation::effects;
using fe::Effect;
using fe::Row;
using ::fixy::BgCompileCtx;
using ::fixy::BgDrainCtx;
using ::fixy::BgLoadCtx;
using ::fixy::ColdInitCtx;
using ::fixy::HotFgCtx;
using ::fixy::InitLoadCtx;
using ::fixy::TestRunnerCtx;

// ---------------------------------------------------------------------
// The rows are the old tree's, restated so a rewrite of Ctx.h reddens
// here and not only in the header.

static_assert(std::is_same_v<HotFgCtx, fe::ExecCtx<fe::ctx_cap::Fg, Row<>>>);
static_assert(std::is_same_v<BgDrainCtx, fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc>>>);
static_assert(std::is_same_v<BgCompileCtx, fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO>>>);
static_assert(std::is_same_v<ColdInitCtx, fe::ExecCtx<fe::Init, Row<Effect::Init, Effect::Alloc, Effect::IO>>>);
static_assert(std::is_same_v<TestRunnerCtx,
                             fe::ExecCtx<fe::Test, Row<Effect::Test, Effect::Alloc, Effect::IO, Effect::Block>>>);
static_assert(
    std::is_same_v<BgLoadCtx, fe::ExecCtx<fe::Bg, Row<Effect::Bg, Effect::Alloc, Effect::IO, Effect::Block>>>);
static_assert(
    std::is_same_v<InitLoadCtx, fe::ExecCtx<fe::Init, Row<Effect::Init, Effect::Alloc, Effect::IO, Effect::Block>>>);

// No context builds from nothing.
static_assert(!std::is_default_constructible_v<HotFgCtx> && !std::is_default_constructible_v<BgDrainCtx>
              && !std::is_default_constructible_v<BgCompileCtx> && !std::is_default_constructible_v<BgLoadCtx>
              && !std::is_default_constructible_v<ColdInitCtx> && !std::is_default_constructible_v<InitLoadCtx>
              && !std::is_default_constructible_v<TestRunnerCtx>);

// Only a row that names Block admits Block, and the hot path never does.
static_assert(!fe::CtxAdmits<HotFgCtx, Row<Effect::Block>>, "The hot path never admits Block.");
static_assert(!fe::CtxAdmits<ColdInitCtx, Row<Effect::Block>>);
static_assert(fe::CtxAdmits<InitLoadCtx, Row<Effect::Block>>);
static_assert(fe::CtxAdmits<BgLoadCtx, Row<Effect::Block>>);
static_assert(!fe::CtxAdmits<BgCompileCtx, Row<Effect::Block>>);

// And the shapes foundation recorded.
static_assert(std::is_same_v<HotFgCtx, fe::detail::ctx_witnesses::FgWitness>);
static_assert(std::is_same_v<BgDrainCtx, fe::detail::ctx_witnesses::BgWitness>);
static_assert(std::is_same_v<BgCompileCtx, fe::detail::ctx_witnesses::BgIoWitness>);
static_assert(std::is_same_v<ColdInitCtx, fe::detail::ctx_witnesses::InitWitness>);
static_assert(std::is_same_v<TestRunnerCtx, fe::detail::ctx_witnesses::TestWitnessCtx>);

// Five distinct types.
static_assert(!std::is_same_v<BgDrainCtx, BgCompileCtx>);
static_assert(!std::is_same_v<HotFgCtx, TestRunnerCtx>);
static_assert(!std::is_same_v<ColdInitCtx, TestRunnerCtx>);

// ---------------------------------------------------------------------
// A body gated on a row resolves for the contexts that own it.

template <class Ctx>
    requires fe::CtxOwnsAllOf<Ctx, Effect::Bg, Effect::Alloc>
[[nodiscard]] int needs_drain(Ctx const&) noexcept {
    return 99;
}

template <class Ctx>
    requires fe::CtxOwnsCapability<Ctx, Effect::IO>
[[nodiscard]] int needs_io(Ctx const&) noexcept {
    return 7;
}

template <class Ctx>
    requires fe::CtxAdmits<Ctx, Row<>>
[[nodiscard]] int needs_nothing(Ctx const&) noexcept {
    return 1;
}

template <class Ctx>
concept CanDrain = requires(Ctx const& ctx) { needs_drain(ctx); };
template <class Ctx>
concept CanDoIo = requires(Ctx const& ctx) { needs_io(ctx); };
template <class Ctx>
concept CanDoNothing = requires(Ctx const& ctx) { needs_nothing(ctx); };

static_assert(CanDrain<BgDrainCtx>);
static_assert(CanDrain<BgCompileCtx>);
static_assert(!CanDrain<HotFgCtx>);
static_assert(!CanDrain<ColdInitCtx>);
static_assert(!CanDrain<TestRunnerCtx>, "a fixture cannot stand in for the background context");

static_assert(!CanDoIo<BgDrainCtx>, "the drain row claims no IO; the compile row does");
static_assert(CanDoIo<BgCompileCtx>);
static_assert(CanDoIo<ColdInitCtx>);
static_assert(CanDoIo<TestRunnerCtx>);
static_assert(!CanDoIo<HotFgCtx>);

static_assert(CanDoNothing<HotFgCtx> && CanDoNothing<BgDrainCtx> && CanDoNothing<BgCompileCtx>
              && CanDoNothing<ColdInitCtx> && CanDoNothing<TestRunnerCtx>);

// ---------------------------------------------------------------------
// Narrowing the compile row by IO is the drain context: the two
// production names are one narrowing apart, not two spellings.

static_assert(std::is_same_v<decltype(std::declval<BgCompileCtx const&>().in_row<Row<Effect::Bg, Effect::Alloc>>()),
                             BgDrainCtx>);

// A row only narrows.  The drain context does not widen by Block,
// although its source permits Block: the atom needs the source as
// evidence, and the drain context does not lend its source.  The
// foreground context cannot claim Bg at all.
template <class Ctx>
concept CanWidenByBlock = requires(Ctx const& ctx) {
    ctx.template in_row<Row<Effect::Bg, Effect::Alloc, Effect::Block>>();
};
template <class Ctx>
concept CanClaimBg = requires(Ctx const& ctx) { ctx.template in_row<Row<Effect::Bg>>(); };
template <class Ctx>
concept LendsItsSource = requires(Ctx const& ctx) { ctx.cap(); };
static_assert(!CanWidenByBlock<BgDrainCtx>);
static_assert(CanWidenByBlock<BgLoadCtx>, "the load row covers the narrower row");
static_assert(!CanClaimBg<HotFgCtx>);
static_assert(!CanWidenByBlock<ColdInitCtx>, "the init row is not a subrow of the drain row, and an init source permits no Bg");
static_assert(!LendsItsSource<BgDrainCtx> && !LendsItsSource<BgCompileCtx> && !LendsItsSource<ColdInitCtx>);
static_assert(LendsItsSource<BgLoadCtx> && LendsItsSource<InitLoadCtx> && LendsItsSource<TestRunnerCtx>);

// The startup load context narrowed by Block is the cold init context.
// The two production names are one narrowing apart, as the compile and
// the drain contexts are.
static_assert(std::is_same_v<decltype(std::declval<InitLoadCtx const&>()
                                          .in_row<Row<Effect::Init, Effect::Alloc, Effect::IO>>()),
                             ColdInitCtx>);

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.
// Each context is handed the capability it claims.  The foreground one
// comes from the key of the producer claim, here the test door.

[[nodiscard]] int check_runtime_paths() {
    HotFgCtx fg = fe::testing::foreground();
    BgDrainCtx drain{fe::testing::bg()};
    BgCompileCtx compile{fe::testing::bg()};
    ColdInitCtx cold{fe::testing::init()};
    TestRunnerCtx test_ctx{fe::testing::test()};

    if (needs_nothing(fg) != 1) return 1;
    if (needs_drain(drain) != 99) return 2;
    if (needs_drain(compile) != 99) return 3;
    if (needs_io(compile) != 7) return 4;
    if (needs_io(cold) != 7) return 5;
    if (needs_io(test_ctx) != 7) return 6;

    // The capability member is reachable through the borrowing accessor,
    // from a context that claims every atom of it, and nowhere else.
    BgLoadCtx bg_load{fe::testing::bg()};
    fe::Bg const& held = bg_load.cap();
    [[maybe_unused]] fe::cap::Alloc alloc_tag = held.alloc;
    InitLoadCtx load{fe::testing::init()};
    fe::Init const& held_init = load.cap();
    [[maybe_unused]] fe::cap::IO io_tag = held_init.io;
    [[maybe_unused]] fe::cap::Block block_tag = load.cap().block;

    // The compile context narrowed by IO is the drain context, and it
    // carries the capability it was built with.
    BgDrainCtx narrowed = compile.in_row<Row<Effect::Bg, Effect::Alloc>>();
    if (needs_drain(narrowed) != 99) return 7;

    // The startup load context narrowed by Block is the cold init
    // context, and it carries the init capability.
    ColdInitCtx narrowed_init = load.in_row<Row<Effect::Init, Effect::Alloc, Effect::IO>>();
    if (needs_io(narrowed_init) != 7) return 9;
    if (needs_io(load) != 7) return 10;

    if (sizeof(fg) != 1 || sizeof(drain) != 1 || sizeof(compile) != 1 || sizeof(cold) != 1 || sizeof(load) != 1
        || sizeof(test_ctx) != 1) {
        return 8;
    }
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
