// Sentinel TU for fixy/Ctx.h: the five production contexts are the
// rows the old tree declared and the shapes foundation recorded, a
// body gated on a row resolves for the contexts that own it and for no
// other, and every context is built at run time from the capability
// it claims.
//
// The header self-test pins the shapes.  What this file adds is the
// scenarios: a gated call, a row widened from one production context
// into another, and the runtime construction, which needs a real
// capability that a test takes from foundation's testing witness.

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
using ::fixy::ColdInitCtx;
using ::fixy::HotFgCtx;
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
// Widening the drain row by IO is the compile context: the two
// production names are one promotion apart, not two spellings.

static_assert(std::is_same_v<decltype(std::declval<BgDrainCtx const&>().in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>()),
                             BgCompileCtx>);

// A row only grows, and only within what the source permits.  The
// drain context widens by Block, which its source permits; the
// foreground context cannot claim Bg at all, because its source
// permits the empty row only.
template <class Ctx>
concept CanWidenByBlock = requires(Ctx const& ctx) {
    ctx.template in_row<Row<Effect::Bg, Effect::Alloc, Effect::Block>>();
};
template <class Ctx>
concept CanClaimBg = requires(Ctx const& ctx) { ctx.template in_row<Row<Effect::Bg>>(); };
static_assert(CanWidenByBlock<BgDrainCtx>);
static_assert(!CanClaimBg<HotFgCtx>);
static_assert(!CanWidenByBlock<ColdInitCtx>, "an init source never permits Block, and its row is not the drain row");

// ---------------------------------------------------------------------
// A static_assert proves the constant-evaluated path only.  These run.
// Only the foreground context builds from nothing; each of the others
// is handed the capability it claims.

[[nodiscard]] int check_runtime_paths() {
    HotFgCtx fg{};
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

    // The capability member is reachable through the borrowing accessor
    // and nowhere else.
    fe::Bg const& held = drain.cap();
    [[maybe_unused]] fe::cap::Alloc alloc_tag = held.alloc;
    fe::Init const& held_init = cold.cap();
    [[maybe_unused]] fe::cap::IO io_tag = held_init.io;

    // The drain context widened by IO is the compile context, and it
    // carries the capability it was built with.
    BgCompileCtx widened = drain.in_row<Row<Effect::Bg, Effect::Alloc, Effect::IO>>();
    if (needs_io(widened) != 7) return 7;

    if (sizeof(fg) != 1 || sizeof(drain) != 1 || sizeof(compile) != 1 || sizeof(cold) != 1 || sizeof(test_ctx) != 1) {
        return 8;
    }
    return 0;
}

}  // namespace

int main() {
    if (int rc = check_runtime_paths(); rc != 0) return rc;
    return 0;
}
