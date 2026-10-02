// EpochVersioned and Budgeted on operands the optimizer cannot see.  Each
// header carries its own static assertions; this file makes them compile
// and runs the operations at run time, which is what catches a body that
// only ever instantiates in a constant expression.
//
// It also pins the two row-hash identities.  Each wrapper publishes the
// graded shape, so the fold reaches it, and the two product lattices are
// different axes, so the two wrappers take two slots.

#include <fixy/Budgeted.h>
#include <fixy/EpochVersioned.h>
#include <foundation/algebra/GradedTrait.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>
#include "../test_assert.h"

namespace {

namespace fa = ::foundation::algebra;
namespace fd = ::foundation::diag;
namespace fe = ::foundation::effects;

// The scope of process startup, which alone mints a version source and a
// budget authority, and a test scope that owns IO, which reads a count
// image.
using InitCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using IoCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

static_assert(fa::GradedWrapper<fixy::EpochVersioned<int>>);
static_assert(fa::GradedWrapper<fixy::Budgeted<int>>);

static_assert(fd::row_hash_contribution_v<fixy::EpochVersioned<int>> != 0);
static_assert(fd::row_hash_contribution_v<fixy::Budgeted<int>> != 0);
static_assert(fd::row_hash_contribution_v<fixy::EpochVersioned<int>>
              != fd::row_hash_contribution_v<fixy::Budgeted<int>>);

// The payload recurses into the fold, so a version over a budget and a
// budget over a version are two nestings and two slots.
static_assert(fd::row_hash_contribution_v<fixy::EpochVersioned<fixy::Budgeted<int>>>
              != fd::row_hash_contribution_v<fixy::Budgeted<fixy::EpochVersioned<int>>>);

// The detection concepts ask reflection for the template a type was
// instantiated from, so a class that derives from the wrapper is not
// taken for it.
struct DerivedVersion : fixy::EpochVersioned<int> {};
struct DerivedBudget : fixy::Budgeted<int> {};
static_assert(!fixy::IsEpochVersioned<DerivedVersion>);
static_assert(!fixy::IsBudgeted<DerivedBudget>);
static_assert(fixy::IsEpochVersioned<fixy::EpochVersioned<DerivedBudget>>);

volatile std::uint64_t g_seed = 5;

[[noreturn]] void fail(char const* what) {
    std::fprintf(stderr, "test_versioned_budgeted: %s\n", what);
    std::abort();
}

// A count at a number, read through the checked image door.
template <typename L>
typename L::element_type count_at(std::uint64_t count) {
    IoCtx const ctx{fe::testing::test()};
    typename L::image_type image{};
    for (std::size_t i = 0; i < 8; ++i) {
        image[i] = static_cast<std::byte>((L::image_axis() >> (8 * i)) & 0xFFu);
        image[8 + i] = static_cast<std::byte>((count >> (8 * i)) & 0xFFu);
    }
    auto const read = L::mint_from_image(ctx, image);
    if (!read) std::abort();
    return *read;
}

void exercise_epoch_versioned() {
    using fixy::EpochBound;
    using fixy::EpochLattice;
    using fixy::GenerationBound;
    using fixy::GenerationLattice;
    using EV = fixy::EpochVersioned<int>;

    std::uint64_t const s = g_seed;
    InitCtx const init{fe::testing::init()};
    fixy::VersionSource source = fixy::mint_version_source(init);
    source.adopt(count_at<EpochLattice>(s + 5), count_at<GenerationLattice>(9));
    auto const stamp_at = [&source](std::uint64_t epoch, std::uint64_t generation) {
        auto const stamp =
            source.stamp_received(count_at<EpochLattice>(epoch), count_at<GenerationLattice>(generation));
        if (!stamp) fail("the source vouches for a version it has reached");
        return *stamp;
    };

    EV const older{10, stamp_at(s, 1)};
    EV const newer{20, stamp_at(s + 2, 2)};
    auto const fresher = fixy::select_fresher(older, newer);
    if (!fresher || fresher->peek() != 20 || !(fresher->version() == newer.version())) fail("select_fresher picks");

    EV const epoch_ahead{30, stamp_at(s + 5, 0)};
    EV const gen_ahead{40, stamp_at(s, 9)};
    auto const conflict = fixy::select_fresher(epoch_ahead, gen_ahead);
    if (conflict || conflict.error() != fixy::VersionConflict::Incomparable) fail("select_fresher refuses");

    if (!newer.is_at_least(EpochBound{s + 2}, GenerationBound{2})
        || newer.is_at_least(EpochBound{s + 3}, GenerationBound{0})) {
        fail("is_at_least");
    }

    EV const genesis = EV::at_genesis(1);
    if (!(genesis.epoch() == EpochLattice::bottom()) || genesis.is_at_least(EpochBound{1}, GenerationBound{0})) {
        fail("at_genesis");
    }

    EV moved_from{50, stamp_at(s, 3)};
    auto const moved = fixy::select_fresher(std::move(moved_from), EV{60, stamp_at(s, 2)});
    if (!moved || moved->peek() != 50) fail("select_fresher moves");

    int const payload = EV{70, source.stamp()}.consume();
    if (payload != 70) fail("consume");

    // The source advances by one step and never falls, and it refuses to
    // vouch for a version it has not reached.
    fixy::VersionStamp const before = source.stamp();
    fixy::VersionStamp const after = source.advance_epoch();
    if (after.epoch().raw() != before.epoch().raw() + 1 || !(after.generation() == before.generation())) {
        fail("advance_epoch");
    }
    if (!(source.adopt(count_at<EpochLattice>(1), count_at<GenerationLattice>(1)).epoch() == after.epoch())) {
        fail("adopt never lowers");
    }
    auto const ahead = source.stamp_received(EpochLattice::successor(after.epoch()), after.generation());
    if (ahead || ahead.error() != fixy::VersionConflict::AheadOfSource) fail("stamp_received refuses the future");
}

void exercise_budgeted() {
    using fixy::BitsBudgetBound;
    using fixy::PeakBytesBound;
    using B = fixy::Budgeted<int>;

    std::uint64_t const s = g_seed;
    InitCtx const init{fe::testing::init()};
    fixy::BudgetAuthority authority = fixy::mint_budget_authority(init);
    auto const grant = [&authority](std::uint64_t bits, std::uint64_t peak) {
        return authority.grant(BitsBudgetBound{bits}, PeakBytesBound{peak});
    };

    B const unmeasured{};
    if (!unmeasured.is_unbounded() || unmeasured.satisfies(BitsBudgetBound{s * 1000}, PeakBytesBound{s * 1000})) {
        fail("the default claims nothing");
    }

    B const left{1, grant(s * 20, 1024)};
    B const right{2, grant(s * 40, 512)};
    B const joined = left.combine_max(right);
    if (joined.peek() != 1 || !(joined.bits() == right.bits()) || !(joined.peak_bytes() == left.peak_bytes())) {
        fail("combine_max");
    }

    B const summed = left.accumulate(right);
    if (summed.bits().raw() != s * 60 || summed.peak_bytes().raw() != 1536) fail("accumulate");

    B const near_top{0, grant(std::numeric_limits<std::uint64_t>::max() - s, 0)};
    if (!(near_top.accumulate(right).bits() == fixy::BitsBudgetLattice::top())) fail("accumulate clamps");

    if (!left.satisfies(BitsBudgetBound{s * 20}, PeakBytesBound{1024})
        || left.satisfies(BitsBudgetBound{s * 20 - 1}, PeakBytesBound{1024})) {
        fail("satisfies");
    }

    // Each composition keeps the left payload, so each must give a grade at
    // or above the left operand's own.  This grid proves it for the join and
    // for the sum.  O(n^2) in the grid size, which is fixed at five.
    std::uint64_t const top = std::numeric_limits<std::uint64_t>::max();
    B const grid[] = {left, right, B{3, grant(0, 0)}, B{4, grant(top - s, top)}, B{5, grant(s, top - 1)}};
    for (B const& a : grid) {
        for (B const& b : grid) {
            if (!fixy::BudgetLattice::leq(a.budget(), a.combine_max(b).budget())) fail("combine_max tightens");
            if (!fixy::BudgetLattice::leq(a.budget(), a.accumulate(b).budget())) fail("accumulate tightens");
            if (a.accumulate(b).peek() != a.peek() || a.combine_max(b).peek() != a.peek()) fail("left payload");
        }
    }
}

}  // namespace

int main() {
    exercise_epoch_versioned();
    exercise_budgeted();
    crucible::test::pass("test_versioned_budgeted: ok\n");
    return 0;
}
