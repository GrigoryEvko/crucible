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
#include <foundation/effects/Ctx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using fixy::BitsBudgetBound;
using fixy::Budgeted;
using fixy::Epoch;
using fixy::EpochBound;
using fixy::EpochLattice;
using fixy::EpochVersioned;
using fixy::Generation;
using fixy::GenerationBound;
using fixy::GenerationLattice;
using fixy::PeakBytesBound;
using fixy::VersionConflict;
using fixy::VersionStamp;

namespace fe = ::foundation::effects;

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

volatile std::uint64_t g_seed = 3;
int g_failures = 0;

void expect(bool holds, char const* what) {
    if (!holds) {
        std::fprintf(stderr, "test_versioned_budgeted_attacks: FAILED: %s\n", what);
        ++g_failures;
    }
}

using InitCtx = fe::ExecCtx<fe::Init, fe::Row<fe::Effect::Init, fe::Effect::Alloc, fe::Effect::IO>>;
using IoCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

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

// The one version source and the one budget authority of this program.
// The source has reached the top of both counters, so it vouches for any
// version below it.
fixy::VersionSource& version_source() {
    static fixy::VersionSource source = fixy::mint_version_source(InitCtx{fe::testing::init()});
    static bool const reached_the_top = [] {
        (void)source.adopt(EpochLattice::top(), GenerationLattice::top());
        return true;
    }();
    (void)reached_the_top;
    return source;
}

fixy::BudgetAuthority& budget_authority() {
    static fixy::BudgetAuthority authority = fixy::mint_budget_authority(InitCtx{fe::testing::init()});
    return authority;
}

VersionStamp stamp_at(std::uint64_t epoch, std::uint64_t generation) {
    auto const stamp =
        version_source().stamp_received(count_at<EpochLattice>(epoch), count_at<GenerationLattice>(generation));
    if (!stamp) std::abort();
    return *stamp;
}

fixy::BudgetStamp grant(std::uint64_t bits, std::uint64_t peak) {
    return budget_authority().grant(BitsBudgetBound{bits}, PeakBytesBound{peak});
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
        return EpochVersioned<int>{payload, stamp_at(counts[i / 10], counts[(i / 2) % 5])};
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
            EpochVersioned<int> const value{7, stamp_at(e, g)};
            for (std::uint64_t const me : counts) {
                for (std::uint64_t const mg : counts) {
                    expect(value.is_at_least(EpochBound{me}, GenerationBound{mg}) == (e >= me && g >= mg),
                           "is_at_least is the numeric order on both counters");
                }
            }
        }
    }
    // A value at the top of both counters passes every gate, and genesis
    // passes only the genesis gate.
    EpochVersioned<int> const newest{1, stamp_at(kMax, kMax)};
    expect(newest.is_at_least(EpochBound{kMax}, GenerationBound{kMax}), "the top passes the top gate");
    EpochVersioned<int> const genesis = EpochVersioned<int>::at_genesis(1);
    expect(genesis.is_at_least(EpochBound{0}, GenerationBound{0}) && !genesis.is_at_least(EpochBound{0}, GenerationBound{1}),
           "genesis passes the genesis gate and no other");
}

// ── Moved-from values that keep a claim ─────────────────────────────

void attack_moved_from_version() {
    std::uint64_t const s = g_seed;
    EpochVersioned<std::string> source{std::string(64, 'x'), stamp_at(s + 5, s)};
    EpochVersioned<std::string> taken{std::move(source)};
    expect(taken.is_at_least(EpochBound{s + 5}, GenerationBound{s}), "the target keeps the claim");
    expect(!source.is_at_least(EpochBound{1}, GenerationBound{0}), "the moved-from source drops to genesis");

    EpochVersioned<std::string> assigned{std::string("old"), stamp_at(1, 1)};
    assigned = std::move(taken);
    expect(assigned.peek().size() == 64 && assigned.is_at_least(EpochBound{s + 5}, GenerationBound{s}),
           "assignment moves the claim");
    expect(!taken.is_at_least(EpochBound{1}, GenerationBound{0}), "the moved-from assignment source drops to genesis");

    // The rvalue selector moves the winner out, and the winner's source
    // drops to genesis.
    EpochVersioned<std::string> older{std::string("older"), stamp_at(2, 2)};
    EpochVersioned<std::string> newer{std::string("newer"), stamp_at(3, 3)};
    auto picked = fixy::select_fresher(std::move(older), std::move(newer));
    expect(picked.has_value() && picked->peek() == "newer", "the rvalue selector picks the newer value");
    expect(!newer.is_at_least(EpochBound{1}, GenerationBound{0}), "the moved winner leaves genesis behind");

    // A trivially copyable payload is copied by a move, so the source
    // keeps a claim that is still true of the bytes it still holds.
    EpochVersioned<int> copied_source{11, stamp_at(s, s)};
    EpochVersioned<int> copied_target{std::move(copied_source)};
    expect(copied_source.peek() == 11 && copied_source.is_at_least(EpochBound{s}, GenerationBound{s})
               && copied_target.peek() == 11,
           "a trivially copyable source keeps a true claim");

    // swap exchanges payload and version together.
    EpochVersioned<int> left{1, stamp_at(1, 9)};
    EpochVersioned<int> right{2, stamp_at(9, 1)};
    swap(left, right);
    expect(left.peek() == 2 && left.epoch().raw() == 9 && right.peek() == 1 && right.generation().raw() == 9,
           "swap moves each payload with its own version");
}

void attack_moved_from_budget() {
    Budgeted<std::string> source{std::string(64, 'y'), grant(8, 64)};
    Budgeted<std::string> taken{std::move(source)};
    expect(taken.satisfies(BitsBudgetBound{8}, PeakBytesBound{64}), "the target keeps the claim");
    expect(source.is_unbounded() && !source.satisfies(BitsBudgetBound{kMax - 1}, PeakBytesBound{kMax - 1}),
           "the moved-from source is unbounded");

    Budgeted<std::string> assigned{std::string("old"), grant(1, 1)};
    assigned = std::move(taken);
    expect(taken.is_unbounded() && assigned.satisfies(BitsBudgetBound{8}, PeakBytesBound{64}),
           "assignment moves the claim");
}

// ── Budgets at the edge ─────────────────────────────────────────────

void attack_budget_edges() {
    std::uint64_t const s = g_seed;
    Budgeted<int> const unmeasured{};
    expect(unmeasured.is_unbounded(), "the default is unbounded");
    expect(!unmeasured.satisfies(BitsBudgetBound{kMax - 1}, PeakBytesBound{kMax}), "the default fails a finite bits gate");
    expect(!unmeasured.satisfies(BitsBudgetBound{kMax}, PeakBytesBound{kMax - 1}), "the default fails a finite peak gate");
    // A gate at the top of both axes is no gate, and admits anything.
    expect(unmeasured.satisfies(BitsBudgetBound{kMax}, PeakBytesBound{kMax}),
           "a gate at the top admits the unbounded claim");

    Budgeted<int> const near_top{1, grant(kMax - s, kMax - s)};
    Budgeted<int> const small{2, grant(s * 2, s * 2)};
    Budgeted<int> const summed = near_top.accumulate(small);
    expect(summed.is_unbounded(), "a sum past the top clamps to unbounded");
    expect(!summed.satisfies(BitsBudgetBound{kMax - 1}, PeakBytesBound{kMax - 1}), "a clamped sum fails every finite gate");

    // The two compositions commute and associate on the grade.
    Budgeted<int> const a{1, grant(s, s * 5)};
    Budgeted<int> const b{2, grant(s * 3, s)};
    Budgeted<int> const c{3, grant(s * 7, s * 2)};
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
    std::array<Budgeted<int>, 5> const grid = {a, b, c, near_top, Budgeted<int>{9, grant(0, 0)}};
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

// A standard container is read by its elements and its type arguments.
// These cases sit here and not in SelfContained.h, so that a header that
// includes it does not pay for them.
struct OwnsElements {
    std::vector<int> items;
    [[nodiscard]] auto begin() const noexcept { return items.begin(); }
    [[nodiscard]] auto end() const noexcept { return items.end(); }
};
struct OrderFromOutside {
    bool const* reversed = nullptr;
    [[nodiscard]] bool operator()(int lhs, int rhs) const noexcept { return *reversed ? rhs < lhs : lhs < rhs; }
};
static_assert(carried_by_both<std::vector<std::vector<int>>> && carried_by_both<OwnsElements>,
              "a program range whose members hold a standard container owns what that container owns");
static_assert(carried_by_both<std::set<int>> && refused_by_both<std::set<int, OrderFromOutside>>,
              "a comparator is part of the container, and one that reads outside state reaches out");
static_assert(refused_by_both<std::pmr::vector<int>>,
              "an allocator that names a memory resource reaches out, because the owner of the resource can free it");
static_assert(refused_by_both<std::vector<void (*)()>>);

// An owned payload is a copy, so a write to the source after the claim
// does not reach it.
void attack_payload_owns_what_it_reaches() {
    std::uint64_t const s = g_seed;
    std::vector<int> source{1, 2, 3};
    EpochVersioned<std::vector<int>> const versioned{source, stamp_at(s, s)};
    Budgeted<std::vector<int>> const budgeted{source, grant(s, s)};
    source[0] = 99;
    source.push_back(4);
    expect(versioned.peek() == std::vector<int>{1, 2, 3} && budgeted.peek() == std::vector<int>{1, 2, 3},
           "a write to the source does not reach an owned payload");
    expect(versioned.is_at_least(EpochBound{s}, GenerationBound{s})
               && budgeted.satisfies(BitsBudgetBound{s}, PeakBytesBound{s}),
           "the claims stay true of the payload they describe");
}

// ── Equal versions ──────────────────────────────────────────────────

// A payload with no operator== compares by its members, so it can show
// that two values at one version are one event.  A payload whose
// operator== is hand-written is refused: that equality can hold for values
// a reader tells apart.
struct NoEquality {
    int v = 0;
    double weight = 0.0;
};
struct EqualToEverything {
    int v = 0;
    [[nodiscard]] constexpr bool operator==(EqualToEverything const&) const noexcept { return true; }
};
struct HoldsEqualToEverything {
    int id = 0;
    EqualToEverything inner{};
};
struct FriendDefaultedEquality {
    int v = 0;
    friend bool operator==(FriendDefaultedEquality const&, FriendDefaultedEquality const&) = default;
};
template <typename T>
concept can_select_copies = requires(EpochVersioned<T> const& a, EpochVersioned<T> const& b) {
    fixy::select_fresher(a, b);
};
template <typename T>
concept can_select_moves = requires(EpochVersioned<T>&& a, EpochVersioned<T>&& b) {
    fixy::select_fresher(std::move(a), std::move(b));
};
static_assert(can_select_copies<NoEquality> && can_select_moves<NoEquality>,
              "a payload with no equality compares by its members");
static_assert(can_select_copies<std::string> && can_select_moves<std::string>);
static_assert(!can_select_copies<EqualToEverything> && !can_select_moves<EqualToEverything>,
              "a hand-written operator== is refused");
static_assert(!can_select_copies<HoldsEqualToEverything>, "a hand-written operator== in a member is refused too");
static_assert(!can_select_copies<FriendDefaultedEquality>,
              "an operator== that reflection cannot see as defaulted is refused, which errs toward safety");
static_assert(std::is_same_v<fixy::uncomparable_part_t<HoldsEqualToEverything>, EqualToEverything>,
              "the refusal names the part whose equality is hand-written");

void attack_equal_versions_compare_payloads() {
    std::uint64_t const s = g_seed;
    EpochVersioned<std::string> const left{std::string("same"), stamp_at(s, s)};
    EpochVersioned<std::string> const agrees{std::string("same"), stamp_at(s, s)};
    EpochVersioned<std::string> const differs{std::string("other"), stamp_at(s, s)};
    auto const one_event = fixy::select_fresher(left, agrees);
    expect(one_event.has_value() && one_event->peek() == "same", "agreeing payloads at one version are one event");
    auto const conflict = fixy::select_fresher(left, differs);
    expect(!conflict && conflict.error() == VersionConflict::Divergent, "differing payloads at one version conflict");
    auto const moved = fixy::select_fresher(EpochVersioned<std::string>{differs}, EpochVersioned<std::string>{left});
    expect(!moved && moved.error() == VersionConflict::Divergent, "the moving form refuses the same conflict");

    // The derived equality reads every member, the bits of a double
    // included, so a payload that differs in one member conflicts.
    EpochVersioned<NoEquality> const first{NoEquality{1, 0.5}, stamp_at(s, s)};
    EpochVersioned<NoEquality> const second{NoEquality{1, 0.25}, stamp_at(s, s)};
    EpochVersioned<NoEquality> const same_as_first{NoEquality{1, 0.5}, stamp_at(s, s)};
    auto const differs_in_one_member = fixy::select_fresher(first, second);
    expect(!differs_in_one_member && differs_in_one_member.error() == VersionConflict::Divergent,
           "a payload that differs in one member conflicts");
    expect(fixy::select_fresher(first, same_as_first).has_value() && first == same_as_first && !(first == second),
           "payloads with equal members are one event");
    EpochVersioned<NoEquality> const signed_zero{NoEquality{1, -0.0}, stamp_at(s, s)};
    EpochVersioned<NoEquality> const unsigned_zero{NoEquality{1, 0.0}, stamp_at(s, s)};
    expect(!fixy::select_fresher(signed_zero, unsigned_zero).has_value(),
           "two zeros of different sign are two values, as a reader of the sign sees");
}

// ── The substrate under each wrapper ────────────────────────────────

void attack_the_substrate() {
    std::uint64_t const s = g_seed;
    using VersionGrade = EpochVersioned<int>::graded_type;
    using V = EpochVersioned<int>::version_t;
    auto const version = [](std::uint64_t epoch, std::uint64_t generation) {
        return V{count_at<EpochLattice>(epoch), count_at<GenerationLattice>(generation)};
    };
    VersionGrade const current{1, version(s + 4, s + 4)};
    // Weakening a version moves it older, which is the weaker claim.
    VersionGrade const aged = current.weaken(version(s, s));
    expect(aged.grade() == version(s, s), "the version substrate weakens toward older");
    // Composing two versions reports the older of the two.
    VersionGrade const other{2, version(s + 9, s + 1)};
    expect(current.compose(other).grade() == version(s + 4, s + 1),
           "the version substrate composes to the older counter on each axis");

    using BudgetGrade = Budgeted<int>::graded_type;
    using Budget = fixy::BudgetLattice::element_type;
    BudgetGrade const measured{1, Budget{count_at<fixy::BitsBudgetLattice>(s), count_at<fixy::PeakBytesLattice>(s)}};
    BudgetGrade const looser =
        measured.weaken(Budget{count_at<fixy::BitsBudgetLattice>(s + 1), count_at<fixy::PeakBytesLattice>(s)});
    expect(looser.grade().first.raw() == s + 1, "the budget substrate weakens toward more use");
}

// ── The doors the old ledger held, each shown closed ────────────────

// No producer states a claim.  A version comes only from a stamp of the
// one source, a budget only from a grant of the one authority, and each is
// minted only with an Init context.
static_assert(!std::is_constructible_v<EpochVersioned<int>, int, Epoch, Generation>);
static_assert(!std::is_constructible_v<Budgeted<int>, int, fixy::BitsBudget, fixy::PeakBytes>);
static_assert(!std::is_constructible_v<VersionStamp, Epoch, Generation>);
static_assert(!std::is_constructible_v<fixy::BudgetStamp, fixy::BitsBudget, fixy::PeakBytes>);
template <typename Ctx>
concept mints_a_source = requires(Ctx const& ctx) { fixy::mint_version_source(ctx); };
static_assert(!mints_a_source<IoCtx> && !mints_a_source<fe::ExecCtx<>> && mints_a_source<InitCtx>,
              "only a context that owns Init mints a version source");

void attack_the_closed_doors() {
    // A stamp never names a version above its source.  A fresh source is at
    // genesis and refuses every later version.
    fixy::VersionSource fresh = fixy::mint_version_source(InitCtx{fe::testing::init()});
    auto const future = fresh.stamp_received(count_at<EpochLattice>(1), GenerationLattice::bottom());
    expect(!future && future.error() == VersionConflict::AheadOfSource, "a fresh source vouches for no later version");
    VersionStamp const now = fresh.stamp();
    expect(now.epoch() == EpochLattice::bottom() && now.generation() == GenerationLattice::bottom(),
           "a fresh source is at genesis");
    VersionStamp const advanced = fresh.advance_generation();
    expect(advanced.generation().raw() == 1 && fresh.stamp_received(EpochLattice::bottom(), advanced.generation()),
           "the source vouches for what it reached");
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
    {"work beyond its grant",
     "A budget is an allowance that the authority grants, and a grant bounds the work only when the work spends "
     "through resources that draw on it. operator new, a system call and a store through a pointer pass through no "
     "object that a type can watch, so C++ cannot route every allocation and every byte of a production through one "
     "meter. A producer that uses more than its grant still carries the grant."},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

void reproduce_the_ledger() {
    // A grant of zero bytes, and a payload that holds a mebibyte.
    Budgeted<std::vector<int>> const holds_more{std::vector<int>(262144, 1), grant(0, 0)};
    expect(holds_more.satisfies(BitsBudgetBound{0}, PeakBytesBound{0}),
           "ledger: work beyond its grant still passes the gate of the grant");
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
    attack_the_closed_doors();
    reproduce_the_ledger();
    if (g_failures != 0) {
        std::fprintf(stderr, "test_versioned_budgeted_attacks: %d case(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_versioned_budgeted_attacks: ok\n");
    return 0;
}
