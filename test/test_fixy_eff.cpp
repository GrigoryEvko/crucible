// ── test_fixy_eff — sentinel TU for fixy/Eff.h ─────────────────────
//
// Pulls fixy/Eff.h into a TU compiled under project warning flags so
// the header's static_asserts execute under enforcement.  Witnesses:
//
//   1. Effect enum + Row<> template alias the substrate.
//   2. Computation<R, T> carrier aliases the substrate.
//   3. Capability<E, S> + concept gates pass through.
//   4. ExecCtx + 5 canonical contexts reachable; concepts pass.
//   5. F* aliases (PureRow / TotRow / GhostRow / DivRow / STRow /
//      AllRow) + IsPure / IsTot / IsGhost / IsDiv / IsST / IsAll
//      concepts pass through.
//   6. Resource axis tags + ConcurrentRow reachable.
//   7. EffectMask + bits_from_row<R>() projection works.
//   8. OsUniverse + Universe concept pass through.
//
// HS14: 2 fixy_neg fixtures live in test/fixy_neg/
//       neg_fixy_eff_*.cpp.

#include <crucible/fixy/Eff.h>

#include <type_traits>

namespace fe = crucible::fixy::eff;
namespace ce = crucible::effects;

// ─── 1. Effect enum + Row<> identity ──────────────────────────────

static_assert(fe::Effect::Alloc == ce::Effect::Alloc);
static_assert(fe::Effect::IO    == ce::Effect::IO);
static_assert(fe::Effect::Block == ce::Effect::Block);
static_assert(fe::Effect::Bg    == ce::Effect::Bg);
static_assert(fe::Effect::Init  == ce::Effect::Init);
static_assert(fe::Effect::Test  == ce::Effect::Test);
static_assert(fe::effect_count  == ce::effect_count);

static_assert(std::is_same_v<fe::Row<fe::Effect::Alloc, fe::Effect::IO>,
                             ce::Row<ce::Effect::Alloc, ce::Effect::IO>>,
    "fixy::eff::Row must alias effects::Row");

static_assert(std::is_same_v<fe::EmptyRow, ce::EmptyRow>);

// Subrow concept + row algebra pass through.
static_assert(fe::Subrow<fe::Row<>, fe::Row<fe::Effect::Alloc>>);
static_assert(!fe::Subrow<fe::Row<fe::Effect::Alloc>, fe::Row<>>);

static_assert(std::is_same_v<
    fe::row_union_t<fe::Row<fe::Effect::Alloc>, fe::Row<fe::Effect::IO>>,
    ce::row_union_t<ce::Row<ce::Effect::Alloc>, ce::Row<ce::Effect::IO>>>,
    "row_union_t must alias substrate");

// ─── 2. Computation<R, T> identity ────────────────────────────────

struct EffSentinel_PayloadA {};

static_assert(std::is_same_v<fe::Computation<fe::Row<>, EffSentinel_PayloadA>,
                             ce::Computation<ce::Row<>, EffSentinel_PayloadA>>,
    "fixy::eff::Computation must alias effects::Computation");

static_assert(fe::IsComputation<fe::Computation<fe::Row<>, int>>);
static_assert(!fe::IsComputation<int>);

// ─── 3. Capability + concept gates ────────────────────────────────

static_assert(std::is_same_v<fe::Capability<fe::Effect::Alloc, fe::Bg>,
                             ce::Capability<ce::Effect::Alloc, ce::Bg>>,
    "fixy::eff::Capability must alias effects::Capability");

static_assert(fe::CanMintCap<fe::Effect::Alloc, fe::Bg>);
static_assert(fe::CtxCanMint<fe::BgDrainCtx, fe::Effect::Alloc>);
static_assert(fe::IsCapability<fe::Capability<fe::Effect::Alloc, fe::Bg>>);
static_assert(fe::cap_of_v<fe::Capability<fe::Effect::Alloc, fe::Bg>>
              == fe::Effect::Alloc);

// ─── 4. ExecCtx + canonical 5 contexts ────────────────────────────

static_assert(fe::IsExecCtx<fe::HotFgCtx>);
static_assert(fe::IsExecCtx<fe::BgDrainCtx>);
static_assert(fe::IsExecCtx<fe::BgCompileCtx>);
static_assert(fe::IsExecCtx<fe::ColdInitCtx>);
static_assert(fe::IsExecCtx<fe::TestRunnerCtx>);

static_assert( fe::IsFgCtx<fe::HotFgCtx>);
static_assert( fe::IsBgCtx<fe::BgDrainCtx>);
static_assert( fe::IsBgCtx<fe::BgCompileCtx>);
static_assert( fe::IsInitCtx<fe::ColdInitCtx>);
static_assert( fe::IsTestCtx<fe::TestRunnerCtx>);
static_assert(!fe::IsFgCtx<fe::BgDrainCtx>);

// Cap/Bg/Init/Test struct layout invariant.
static_assert(sizeof(fe::Bg)         == 1);
static_assert(sizeof(fe::Init)       == 1);
static_assert(sizeof(fe::Test)       == 1);
static_assert(sizeof(fe::cap::Alloc) == 1);
static_assert(sizeof(fe::cap::IO)    == 1);
static_assert(sizeof(fe::cap::Block) == 1);

// ─── 5. F* alias rows + concept identity ──────────────────────────

static_assert(std::is_same_v<fe::PureRow, ce::PureRow>);
static_assert(std::is_same_v<fe::TotRow,  ce::TotRow>);
static_assert(std::is_same_v<fe::AllRow,  ce::AllRow>);

static_assert(fe::IsPure<fe::PureRow>);
static_assert(fe::IsTot<fe::TotRow>);
static_assert(fe::IsGhost<fe::GhostRow>);
static_assert(fe::IsDiv<fe::DivRow>);
static_assert(fe::IsST<fe::STRow>);
static_assert(fe::IsAll<fe::AllRow>);

// Chain implications via the substitution principle.
static_assert(fe::IsAll<fe::PureRow>);    // bottom satisfies top
static_assert(fe::IsST <fe::PureRow>);
static_assert(fe::IsDiv<fe::PureRow>);
static_assert(!fe::IsPure<fe::DivRow>);   // strict containment

// ─── 6. Resources + ConcurrentRow ─────────────────────────────────

static_assert(std::is_same_v<fe::SmBudget<1024>, ce::SmBudget<1024>>);
static_assert(std::is_same_v<fe::HbmBytes<1<<30>, ce::HbmBytes<1<<30>>);

static_assert(fe::ResourceTag<fe::SmBudget<1>>);
static_assert(fe::resource_kind_count > 0);

// ConcurrentRow shape.
using EffSentinel_Concurrent =
    fe::ConcurrentRow<fe::SmBudget<128>, fe::HbmBytes<4096>>;
static_assert(std::is_same_v<EffSentinel_Concurrent,
                             ce::ConcurrentRow<ce::SmBudget<128>, ce::HbmBytes<4096>>>);

// ─── 7. EffectMask + Row → mask projection ────────────────────────

static_assert(fe::bits_for<fe::Effect::Alloc>().raw() != 0);
static_assert(fe::bits_from_row<fe::Row<fe::Effect::Alloc>>().raw() != 0);
static_assert(fe::bits_from_row<fe::Row<>>().raw() == 0);

// ─── 8. OsUniverse + Universe concept ─────────────────────────────

static_assert(fe::Universe<fe::OsUniverse>);

// ─── 9. Runtime sanity ────────────────────────────────────────────

int main() {
    auto bg = fe::testing::bg();
    {
        auto cap = fe::mint_cap<fe::Effect::Alloc>(bg);
        std::move(cap).consume();
    }
    {
        fe::BgCompileCtx ctx{};
        auto cap = fe::mint_from_ctx<fe::Effect::IO>(ctx);
        std::move(cap).consume();
    }
    {
        // Computation round-trip.
        auto pure = fe::Computation<fe::Row<>, int>::mk(42);
        int x = std::move(pure).extract();
        (void)x;
    }
    return 0;
}
