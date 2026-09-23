// Adversarial tests of EpochVersioned and Budgeted.  Each case uses the
// public surface as written and legal C++ only: no cast that reinterprets
// storage, no cast that drops const, no reopened namespace, no undefined
// behaviour.  The target is a wrong result that type-checks: a stale value
// read as fresh, a claim of less use than was made, a moved-from value
// that keeps a claim.  A case either proves that the surface refuses it,
// or it reproduces a limit the surface cannot close.  Each such limit is
// an entry of the ledger at the foot of this file, and the ledger only
// shrinks.
//
// The attacks the compiler refuses are negative fixtures under
// test/fixy/neg/, registered beside this test.

#include <fixy/Budgeted.h>
#include <fixy/EpochVersioned.h>
#include <foundation/Saturate.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fixy::BitsBudget;
using fixy::Budgeted;
using fixy::Epoch;
using fixy::EpochVersioned;
using fixy::Generation;
using fixy::PeakBytes;
using fixy::VersionConflict;

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

volatile std::uint64_t g_seed = 3;
int g_failures = 0;

void expect(bool holds, char const* what) {
    if (!holds) {
        std::fprintf(stderr, "test_versioned_budgeted_attacks: FAILED: %s\n", what);
        ++g_failures;
    }
}

// The numeric reading of "a is at or above b", written with no lattice,
// so the tests do not grade the wrapper by the wrapper's own order.
bool at_or_above(Epoch ae, Generation ag, Epoch be, Generation bg) {
    return ae.raw() >= be.raw() && ag.raw() >= bg.raw();
}

// ── Stale read marked fresh ─────────────────────────────────────────

// Every pair from a grid of versions and two payloads, in both orders.
// The result must be one operand whole, at or above the other; equal
// versions must agree on the payload or be refused; an incomparable pair
// must be refused.  O(n^2) in the grid size, which is 5 x 5 x 2.
void attack_select_fresher_grid() {
    std::uint64_t const s = g_seed;
    std::array<std::uint64_t, 5> const counts = {0, 1, s, s + 1, kMax};
    // Five epochs, five generations, two payloads: fifty values.
    auto const value_at = [&counts](std::size_t i) {
        int const payload = static_cast<int>(i % 2) + 1;
        return EpochVersioned<int>{payload, Epoch{counts[i / 10]}, Generation{counts[(i / 2) % 5]}};
    };
    auto const grid = [&value_at]<std::size_t... I>(std::index_sequence<I...>) {
        return std::array<EpochVersioned<int>, sizeof...(I)>{value_at(I)...};
    }(std::make_index_sequence<50>{});

    std::size_t divergent = 0;
    std::size_t incomparable = 0;
    for (auto const& a : grid) {
        for (auto const& b : grid) {
            auto const result = fixy::select_fresher(a, b);
            bool const a_above = at_or_above(a.epoch(), a.generation(), b.epoch(), b.generation());
            bool const b_above = at_or_above(b.epoch(), b.generation(), a.epoch(), a.generation());
            if (!a_above && !b_above) {
                expect(!result && result.error() == VersionConflict::Incomparable, "an incomparable pair is refused");
                ++incomparable;
                continue;
            }
            if (a_above && b_above && a.peek() != b.peek()) {
                expect(!result && result.error() == VersionConflict::Divergent, "a divergent pair is refused");
                ++divergent;
                continue;
            }
            expect(result.has_value(), "an ordered pair gives a result");
            if (!result) continue;
            bool const is_a = result->peek() == a.peek() && result->version() == a.version();
            bool const is_b = result->peek() == b.peek() && result->version() == b.version();
            expect(is_a || is_b, "the result is one operand whole");
            expect(at_or_above(result->epoch(), result->generation(), a.epoch(), a.generation())
                       && at_or_above(result->epoch(), result->generation(), b.epoch(), b.generation()),
                   "the result is at or above both operands");
            // The reversed call must agree on the payload.
            auto const reversed = fixy::select_fresher(b, a);
            expect(reversed.has_value() && reversed->peek() == result->peek() && reversed->version() == result->version(),
                   "the order of the operands does not change the answer");
        }
    }
    expect(divergent > 0 && incomparable > 0, "the grid reached both refusals");
}

void attack_is_at_least_matches_the_numeric_order() {
    std::uint64_t const s = g_seed;
    std::array<std::uint64_t, 5> const counts = {0, 1, s, s + 1, kMax};
    for (std::uint64_t const e : counts) {
        for (std::uint64_t const g : counts) {
            EpochVersioned<int> const value{7, Epoch{e}, Generation{g}};
            for (std::uint64_t const me : counts) {
                for (std::uint64_t const mg : counts) {
                    expect(value.is_at_least(Epoch{me}, Generation{mg}) == (e >= me && g >= mg),
                           "is_at_least is the numeric order on both counters");
                }
            }
        }
    }
    // A value at the top of both counters passes every gate, and genesis
    // passes only the genesis gate.
    EpochVersioned<int> const newest{1, Epoch{kMax}, Generation{kMax}};
    expect(newest.is_at_least(Epoch{kMax}, Generation{kMax}), "the top passes the top gate");
    EpochVersioned<int> const genesis = EpochVersioned<int>::at_genesis(1);
    expect(genesis.is_at_least(Epoch{0}, Generation{0}) && !genesis.is_at_least(Epoch{0}, Generation{1}),
           "genesis passes the genesis gate and no other");
}

// ── Moved-from values that keep a claim ─────────────────────────────

void attack_moved_from_version() {
    std::uint64_t const s = g_seed;
    EpochVersioned<std::string> source{std::string(64, 'x'), Epoch{s + 5}, Generation{s}};
    EpochVersioned<std::string> taken{std::move(source)};
    expect(taken.is_at_least(Epoch{s + 5}, Generation{s}), "the target keeps the claim");
    expect(!source.is_at_least(Epoch{1}, Generation{0}), "the moved-from source drops to genesis");

    EpochVersioned<std::string> assigned{std::string("old"), Epoch{1}, Generation{1}};
    assigned = std::move(taken);
    expect(assigned.peek().size() == 64 && assigned.is_at_least(Epoch{s + 5}, Generation{s}), "assignment moves the claim");
    expect(!taken.is_at_least(Epoch{1}, Generation{0}), "the moved-from assignment source drops to genesis");

    // The rvalue selector moves the winner out, and the winner's source
    // drops to genesis.
    EpochVersioned<std::string> older{std::string("older"), Epoch{2}, Generation{2}};
    EpochVersioned<std::string> newer{std::string("newer"), Epoch{3}, Generation{3}};
    auto picked = fixy::select_fresher(std::move(older), std::move(newer));
    expect(picked.has_value() && picked->peek() == "newer", "the rvalue selector picks the newer value");
    expect(!newer.is_at_least(Epoch{1}, Generation{0}), "the moved winner leaves genesis behind");

    // A trivially copyable payload is copied by a move, so the source
    // keeps a claim that is still true of the bytes it still holds.
    EpochVersioned<int> copied_source{11, Epoch{s}, Generation{s}};
    EpochVersioned<int> copied_target{std::move(copied_source)};
    expect(copied_source.peek() == 11 && copied_source.is_at_least(Epoch{s}, Generation{s})
               && copied_target.peek() == 11,
           "a trivially copyable source keeps a true claim");

    // swap exchanges payload and version together.
    EpochVersioned<int> left{1, Epoch{1}, Generation{9}};
    EpochVersioned<int> right{2, Epoch{9}, Generation{1}};
    swap(left, right);
    expect(left.peek() == 2 && left.epoch() == Epoch{9} && right.peek() == 1 && right.generation() == Generation{9},
           "swap moves each payload with its own version");
}

void attack_moved_from_budget() {
    Budgeted<std::string> source{std::string(64, 'y'), BitsBudget{8}, PeakBytes{64}};
    Budgeted<std::string> taken{std::move(source)};
    expect(taken.satisfies(BitsBudget{8}, PeakBytes{64}), "the target keeps the claim");
    expect(source.is_unbounded() && !source.satisfies(BitsBudget{kMax - 1}, PeakBytes{kMax - 1}),
           "the moved-from source is unbounded");

    Budgeted<std::string> assigned{std::string("old"), BitsBudget{1}, PeakBytes{1}};
    assigned = std::move(taken);
    expect(taken.is_unbounded() && assigned.satisfies(BitsBudget{8}, PeakBytes{64}), "assignment moves the claim");
}

// ── Budgets at the edge ─────────────────────────────────────────────

void attack_budget_edges() {
    std::uint64_t const s = g_seed;
    Budgeted<int> const unmeasured{};
    expect(unmeasured.is_unbounded(), "the default is unbounded");
    expect(!unmeasured.satisfies(BitsBudget{kMax - 1}, PeakBytes{kMax}), "the default fails a finite bits gate");
    expect(!unmeasured.satisfies(BitsBudget{kMax}, PeakBytes{kMax - 1}), "the default fails a finite peak gate");
    // A gate at the top of both axes is no gate, and admits anything.
    expect(unmeasured.satisfies(BitsBudget{kMax}, PeakBytes{kMax}), "a gate at the top admits the unbounded claim");

    Budgeted<int> const near_top{1, BitsBudget{kMax - s}, PeakBytes{kMax - s}};
    Budgeted<int> const small{2, BitsBudget{s * 2}, PeakBytes{s * 2}};
    Budgeted<int> const summed = near_top.accumulate(small);
    expect(summed.is_unbounded(), "a sum past the top clamps to unbounded");
    expect(!summed.satisfies(BitsBudget{kMax - 1}, PeakBytes{kMax - 1}), "a clamped sum fails every finite gate");

    // The two compositions commute and associate on the grade.
    Budgeted<int> const a{1, BitsBudget{s}, PeakBytes{s * 5}};
    Budgeted<int> const b{2, BitsBudget{s * 3}, PeakBytes{s}};
    Budgeted<int> const c{3, BitsBudget{s * 7}, PeakBytes{s * 2}};
    expect(a.combine_max(b).budget() == b.combine_max(a).budget(), "the join commutes on the grade");
    expect(a.accumulate(b).budget() == b.accumulate(a).budget(), "the sum commutes on the grade");
    expect(a.combine_max(b).combine_max(c).budget() == a.combine_max(b.combine_max(c)).budget(),
           "the join associates on the grade");
    expect(a.accumulate(b).accumulate(c).budget() == a.accumulate(b.accumulate(c)).budget(),
           "the sum associates on the grade");
    expect(a.combine_max(unmeasured).is_unbounded() && a.accumulate(unmeasured).is_unbounded(),
           "a composition with the unbounded claim is unbounded");

    // Each composition keeps the left payload, and never claims less use
    // than the left operand had.  O(n^2) over the grid of five.
    std::array<Budgeted<int>, 5> const grid = {a, b, c, near_top, Budgeted<int>{9, BitsBudget{0}, PeakBytes{0}}};
    for (auto const& x : grid) {
        for (auto const& y : grid) {
            expect(fixy::BudgetLattice::leq(x.budget(), x.accumulate(y).budget()), "the sum never tightens");
            expect(fixy::BudgetLattice::leq(x.budget(), x.combine_max(y).budget()), "the join never tightens");
            expect(x.accumulate(y).peek() == x.peek() && x.combine_max(y).peek() == x.peek(), "the left payload stays");
        }
    }
}

// ── Payloads that reach outside the value ───────────────────────────

template <typename T>
concept can_version = requires { typename EpochVersioned<T>; };
template <typename T>
concept can_budget = requires { typename Budgeted<T>; };
// The two wrappers share one payload rule, so each case must agree.
template <typename T>
concept carried_by_both = can_version<T> && can_budget<T>;
template <typename T>
concept refused_by_both = !can_version<T> && !can_budget<T>;

struct HoldsName {
    int id = 0;
    std::string_view name;
};
struct HoldsScratch {
    int value = 0;
    mutable std::vector<int> scratch;
};
// A range that declares no view and no borrow, whose elements live
// outside it.
struct BorrowedRange {
    int const* first = nullptr;
    int const* last = nullptr;
    [[nodiscard]] int const* begin() const noexcept { return first; }
    [[nodiscard]] int const* end() const noexcept { return last; }
};

static_assert(refused_by_both<int*> && refused_by_both<int const*> && refused_by_both<int&>);
static_assert(refused_by_both<std::span<int const>> && refused_by_both<std::string_view>);
static_assert(refused_by_both<std::unique_ptr<int>> && refused_by_both<std::shared_ptr<int>>);
static_assert(refused_by_both<std::reference_wrapper<int>> && refused_by_both<std::function<int()>>);
static_assert(refused_by_both<HoldsName>, "a view in a member reaches out");
static_assert(refused_by_both<std::vector<std::string_view>>, "a container's elements are read");
static_assert(refused_by_both<std::optional<int*>>);
static_assert(refused_by_both<HoldsScratch>, "a mutable member is written through peek()");
static_assert(refused_by_both<BorrowedRange>, "a range the standard does not declare is walked by its members");
static_assert(refused_by_both<std::initializer_list<int>>);

static_assert(carried_by_both<int> && carried_by_both<std::string> && carried_by_both<std::vector<int>>);
static_assert(carried_by_both<std::optional<std::string>> && carried_by_both<std::map<int, std::string>>);
static_assert(carried_by_both<std::array<int, 4>> && carried_by_both<std::variant<int, std::string>>);

// An owned payload is a copy, so a write to the source after the claim
// does not reach it.
void attack_payload_owns_what_it_reaches() {
    std::uint64_t const s = g_seed;
    std::vector<int> source{1, 2, 3};
    EpochVersioned<std::vector<int>> const versioned{source, Epoch{s}, Generation{s}};
    Budgeted<std::vector<int>> const budgeted{source, BitsBudget{s}, PeakBytes{s}};
    source[0] = 99;
    source.push_back(4);
    expect(versioned.peek() == std::vector<int>{1, 2, 3} && budgeted.peek() == std::vector<int>{1, 2, 3},
           "a write to the source does not reach an owned payload");
    expect(versioned.is_at_least(Epoch{s}, Generation{s}) && budgeted.satisfies(BitsBudget{s}, PeakBytes{s}),
           "the claims stay true of the payload they describe");
}

// ── Equal versions ──────────────────────────────────────────────────

struct NoEquality {
    int v = 0;
};
template <typename T>
concept can_select_copies = requires(EpochVersioned<T> const& a, EpochVersioned<T> const& b) {
    fixy::select_fresher(a, b);
};
template <typename T>
concept can_select_moves = requires(EpochVersioned<T>&& a, EpochVersioned<T>&& b) {
    fixy::select_fresher(std::move(a), std::move(b));
};
static_assert(!can_select_copies<NoEquality> && !can_select_moves<NoEquality>,
              "a payload that cannot compare cannot show that two values at one version are one event");
static_assert(can_select_copies<std::string> && can_select_moves<std::string>);

void attack_equal_versions_compare_payloads() {
    std::uint64_t const s = g_seed;
    EpochVersioned<std::string> const left{std::string("same"), Epoch{s}, Generation{s}};
    EpochVersioned<std::string> const agrees{std::string("same"), Epoch{s}, Generation{s}};
    EpochVersioned<std::string> const differs{std::string("other"), Epoch{s}, Generation{s}};
    auto const one_event = fixy::select_fresher(left, agrees);
    expect(one_event.has_value() && one_event->peek() == "same", "agreeing payloads at one version are one event");
    auto const conflict = fixy::select_fresher(left, differs);
    expect(!conflict && conflict.error() == VersionConflict::Divergent, "differing payloads at one version conflict");
    auto const moved = fixy::select_fresher(EpochVersioned<std::string>{differs}, EpochVersioned<std::string>{left});
    expect(!moved && moved.error() == VersionConflict::Divergent, "the moving form refuses the same conflict");
}

// ── The substrate under each wrapper ────────────────────────────────

void attack_the_substrate() {
    std::uint64_t const s = g_seed;
    using VersionGrade = EpochVersioned<int>::graded_type;
    using V = EpochVersioned<int>::version_t;
    VersionGrade const current{1, V{Epoch{s + 4}, Generation{s + 4}}};
    // Weakening a version moves it older, which is the weaker claim.
    VersionGrade const aged = current.weaken(V{Epoch{s}, Generation{s}});
    expect(aged.grade() == V{Epoch{s}, Generation{s}}, "the version substrate weakens toward older");
    // Composing two versions reports the older of the two.
    VersionGrade const other{2, V{Epoch{s + 9}, Generation{s + 1}}};
    expect(current.compose(other).grade() == V{Epoch{s + 4}, Generation{s + 1}},
           "the version substrate composes to the older counter on each axis");

    using BudgetGrade = Budgeted<int>::graded_type;
    BudgetGrade const measured{1, fixy::BudgetLattice::element_type{BitsBudget{s}, PeakBytes{s}}};
    BudgetGrade const looser = measured.weaken(fixy::BudgetLattice::element_type{BitsBudget{s + 1}, PeakBytes{s}});
    expect(looser.grade().first == BitsBudget{s + 1}, "the budget substrate weakens toward more use");
}

// ── The ledger ───────────────────────────────────────────────────────
//
// Each entry is an attack that compiles and gives a wrong answer through
// legal code.  Each has a reproducer that must keep reproducing: a fix
// that closes an entry makes its reproducer fail, and the fix then
// removes the entry and lowers the bound.  The bound only goes down.

struct KnownLimit {
    std::string_view name;
    std::string_view why_it_stays_open;
};

inline constexpr KnownLimit kLedger[] = {
    {"an equality that is not equality-preserving",
     "Two values at one version are one event when the payload type says they are equal. A type whose operator== "
     "holds for values that differ in what a reader can observe does not model std::equality_comparable "
     "([concepts.equality]), and select_fresher cannot know sameness better than the type that defines it."},
    {"a claim stated by the producer",
     "The constructor takes the grade the producer states, and nothing in foundation or fixy can produce a grade "
     "instead: a version comes from a committed fleet membership change, and a budget from a measurement of the "
     "work. A version stated too high, or a zero budget computed by subtraction, passes the gate until a mint "
     "that holds that source is the only door."},
};
static_assert(std::size(kLedger) <= 2, "the ledger only shrinks");

// Every value compares equal, so two values at one version always read as
// one event, whatever they hold.
struct EqualToEverything {
    int v = 0;
    [[nodiscard]] constexpr bool operator==(EqualToEverything const&) const noexcept { return true; }
};

void reproduce_the_ledger() {
    std::uint64_t const s = g_seed;

    EpochVersioned<EqualToEverything> const first{EqualToEverything{1}, Epoch{s}, Generation{s}};
    EpochVersioned<EqualToEverything> const second{EqualToEverything{2}, Epoch{s}, Generation{s}};
    auto const pick = fixy::select_fresher(first, second);
    expect(pick.has_value() && pick->peek().v == 1,
           "ledger: an equality that holds for different values still hides a conflict at one version");

    std::uint64_t const allowance = s * 1000;
    std::uint64_t const over_budget = allowance + 500;
    BitsBudget const zero_by_arithmetic{allowance - allowance};
    BitsBudget const zero_by_saturation{::foundation::sat::sub_sat(allowance, over_budget)};
    Budgeted<int> const claims_nothing{1, zero_by_arithmetic, PeakBytes{zero_by_saturation.raw()}};
    expect(claims_nothing.satisfies(BitsBudget{0}, PeakBytes{0}), "ledger: a producer-stated zero still passes");
    EpochVersioned<int> const claims_future{1, Epoch{kMax}, Generation{kMax}};
    expect(claims_future.is_at_least(Epoch{kMax}, Generation{kMax}),
           "ledger: a producer-stated version still passes");
}

}  // namespace

int main() {
    attack_select_fresher_grid();
    attack_is_at_least_matches_the_numeric_order();
    attack_moved_from_version();
    attack_moved_from_budget();
    attack_budget_edges();
    attack_payload_owns_what_it_reaches();
    attack_equal_versions_compare_payloads();
    attack_the_substrate();
    reproduce_the_ledger();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_versioned_budgeted_attacks: %d case(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_versioned_budgeted_attacks: ok\n");
    return 0;
}
