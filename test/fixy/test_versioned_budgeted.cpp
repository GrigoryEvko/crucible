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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace {

namespace fa = ::foundation::algebra;
namespace fd = ::foundation::diag;

static_assert(fa::GradedWrapper<fixy::EpochVersioned<int>>);
static_assert(fa::GradedWrapper<fixy::Budgeted<int>>);

static_assert(fd::row_hash_contribution_v<fixy::EpochVersioned<int>> != 0);
static_assert(fd::row_hash_contribution_v<fixy::Budgeted<int>> != 0);
static_assert(fd::row_hash_contribution_v<fixy::EpochVersioned<int>> != fd::row_hash_contribution_v<fixy::Budgeted<int>>);

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

void exercise_epoch_versioned() {
    using fixy::Epoch;
    using fixy::Generation;
    using EV = fixy::EpochVersioned<int>;

    std::uint64_t const s = g_seed;
    EV const older{10, Epoch{s}, Generation{1}};
    EV const newer{20, Epoch{s + 2}, Generation{2}};

    auto const fresher = fixy::select_fresher(older, newer);
    if (!fresher || fresher->peek() != 20 || !(fresher->version() == newer.version())) fail("select_fresher picks");

    EV const epoch_ahead{30, Epoch{s + 5}, Generation{0}};
    EV const gen_ahead{40, Epoch{s}, Generation{9}};
    auto const conflict = fixy::select_fresher(epoch_ahead, gen_ahead);
    if (conflict || conflict.error() != fixy::VersionConflict::Incomparable) fail("select_fresher refuses");

    if (!newer.is_at_least(Epoch{s + 2}, Generation{2}) || newer.is_at_least(Epoch{s + 3}, Generation{0})) {
        fail("is_at_least");
    }

    EV const genesis = EV::at_genesis(1);
    if (!(genesis.epoch() == Epoch{0}) || genesis.is_at_least(Epoch{1}, Generation{0})) fail("at_genesis");

    EV moved_from{50, Epoch{s}, Generation{3}};
    auto const moved = fixy::select_fresher(std::move(moved_from), EV{60, Epoch{s}, Generation{2}});
    if (!moved || moved->peek() != 50) fail("select_fresher moves");

    int const payload = EV{70, Epoch{s}, Generation{0}}.consume();
    if (payload != 70) fail("consume");
}

void exercise_budgeted() {
    using fixy::BitsBudget;
    using fixy::PeakBytes;
    using B = fixy::Budgeted<int>;

    std::uint64_t const s = g_seed;
    B const unmeasured{};
    if (!unmeasured.is_unbounded() || unmeasured.satisfies(BitsBudget{s * 1000}, PeakBytes{s * 1000})) {
        fail("the default claims nothing");
    }

    B const left{1, BitsBudget{s * 20}, PeakBytes{1024}};
    B const right{2, BitsBudget{s * 40}, PeakBytes{512}};
    B const joined = left.combine_max(right);
    if (joined.peek() != 1 || !(joined.bits() == right.bits()) || !(joined.peak_bytes() == left.peak_bytes())) {
        fail("combine_max");
    }

    B const summed = left.accumulate(right);
    if (!(summed.bits() == BitsBudget{s * 60}) || !(summed.peak_bytes() == PeakBytes{1536})) fail("accumulate");

    B const near_top{0, BitsBudget{std::numeric_limits<std::uint64_t>::max() - s}, PeakBytes{0}};
    if (!(near_top.accumulate(right).bits() == fixy::BitsBudgetLattice::top())) fail("accumulate clamps");

    if (!left.satisfies(BitsBudget{s * 20}, PeakBytes{1024}) || left.satisfies(BitsBudget{s * 20 - 1}, PeakBytes{1024})) {
        fail("satisfies");
    }

    // Each composition keeps the left payload, so each must give a grade at
    // or above the left operand's own.  The header proves this for the join
    // in a constant evaluation.  This grid proves it for the sum as well.
    // O(n^2) in the grid size, which is fixed at five.
    std::uint64_t const top = std::numeric_limits<std::uint64_t>::max();
    B const grid[] = {left, right, B{3, BitsBudget{0}, PeakBytes{0}}, B{4, BitsBudget{top - s}, PeakBytes{top}},
                      B{5, BitsBudget{s}, PeakBytes{top - 1}}};
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
    std::printf("test_versioned_budgeted: ok\n");
    return 0;
}
