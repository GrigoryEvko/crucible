// Adversarial tests of the strong counters, their duals and the vector
// clock.  Each case uses the public surface as written and legal C++
// only: no cast that reinterprets storage, no cast that drops const, no
// reopened namespace, no undefined behaviour.  A case either proves that
// the surface gives the right answer, or it reproduces a limit that the
// surface cannot close.  Each such limit is an entry of the ledger at the
// foot of this file, and the ledger only shrinks.
//
// The attacks that the compiler refuses are not here.  Each of those is a
// negative fixture under test/foundation/neg/, registered beside this
// test.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Modality.h>
#include <foundation/algebra/lattices/DualLattice.h>
#include <foundation/algebra/lattices/HappensBefore.h>
#include <foundation/algebra/lattices/ProductLattice.h>
#include <foundation/algebra/lattices/StrongCounterLattice.h>
#include <foundation/diag/RowHash.h>

#include <array>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fd = ::foundation::diag;

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

// Read at run time, so the compiler cannot fold the attacks away.
volatile std::uint64_t g_seed = 7;

int g_failures = 0;

void expect(bool holds, char const* what) {
    if (!holds) {
        std::fprintf(stderr, "test_counter_clock_attacks: FAILED: %s\n", what);
        ++g_failures;
    }
}

template <typename L>
using OnAxis = fa::Graded<fa::ModalityKind::Absolute, L, int>;

// ── Counters at the top ─────────────────────────────────────────────

template <typename L>
void attack_counter_top(char const* axis) {
    using E = typename L::element_type;
    std::uint64_t const s = g_seed;
    E const top = L::top();
    E const low{s};
    E const near_top{kMax - s};

    expect(L::join(top, low) == top && L::join(low, top) == top, axis);
    expect(L::meet(top, low) == low && L::meet(low, top) == low, axis);
    expect(L::leq(low, top) && L::leq(near_top, top) && !L::leq(top, near_top), axis);
    expect(L::join(top, top) == top && L::meet(top, top) == top, axis);

    // successor walks up to the top one step at a time and never past it.
    E climbing = near_top;
    for (std::uint64_t i = 0; i < s; ++i) {
        E const next = L::successor(climbing);
        expect(L::leq(climbing, next) && !(next == climbing), axis);
        climbing = next;
    }
    expect(climbing == top, axis);
    expect(L::successor(L::bottom()) == E{1}, axis);

    // The dual turns the extremes over and keeps every element.
    using D = fl::DualLattice<L>;
    expect(D::bottom() == top && D::top() == L::bottom(), axis);
    expect(D::join(low, top) == low && D::meet(low, top) == top, axis);
    expect(D::leq(top, low) && !D::leq(low, top), axis);
}

// ── Mixing axes through a legal path ───────────────────────────────

template <typename A, typename B>
concept mix_implicitly = std::is_convertible_v<A, B> || std::is_convertible_v<B, A>;
template <typename A, typename B>
concept construct_across = std::is_constructible_v<A, B> || std::is_constructible_v<B, A>;
template <typename A, typename B>
concept assign_across = std::is_assignable_v<A&, B> || std::is_assignable_v<B&, A>;
template <typename A, typename B>
concept compare_across = requires(A a, B b) {
    { a == b };
} || requires(A a, B b) {
    { a < b };
} || requires(A a, B b) {
    { a <=> b };
};
template <typename L, typename A, typename B>
concept lattice_accepts = requires(A a, B b) {
    L::join(a, b);
} || requires(A a, B b) { L::leq(a, b); };

// Every pair of axes, every implicit path.  The positive controls prove
// that the detectors answer yes where a path exists.
static_assert(!mix_implicitly<fl::Epoch, fl::Generation> && !construct_across<fl::Epoch, fl::Generation>
              && !assign_across<fl::Epoch, fl::Generation> && !compare_across<fl::Epoch, fl::Generation>);
static_assert(!mix_implicitly<fl::PeakBytes, fl::BitsBudget> && !construct_across<fl::PeakBytes, fl::BitsBudget>
              && !assign_across<fl::PeakBytes, fl::BitsBudget> && !compare_across<fl::PeakBytes, fl::BitsBudget>);
static_assert(!construct_across<fl::Epoch, fl::PeakBytes> && !compare_across<fl::Generation, fl::BitsBudget>);
static_assert(!lattice_accepts<fl::EpochLattice, fl::Epoch, fl::Generation>);
static_assert(!lattice_accepts<fl::DualLattice<fl::EpochLattice>, fl::Epoch, fl::Generation>);
static_assert(!lattice_accepts<fl::BitsBudgetLattice, fl::PeakBytes, fl::PeakBytes>);
static_assert(construct_across<fl::Epoch, fl::Epoch> && compare_across<fl::Epoch, fl::Epoch>
              && lattice_accepts<fl::EpochLattice, fl::Epoch, fl::Epoch>);

// A counter is not an integer and an integer is not a counter, in either
// direction, except through the one explicit door.
static_assert(!std::is_convertible_v<fl::Epoch, std::uint64_t> && !std::is_convertible_v<std::uint64_t, fl::Epoch>);
static_assert(!std::is_convertible_v<int, fl::Epoch> && !std::is_convertible_v<bool, fl::Epoch>);
static_assert(std::is_constructible_v<fl::Epoch, std::uint64_t>);

// A product of two axes refuses its components in the swapped order.
using VersionPair = fl::ProductLattice<fl::EpochLattice, fl::GenerationLattice>::element_type;
static_assert(std::is_constructible_v<VersionPair, fl::Epoch, fl::Generation>);
static_assert(!std::is_constructible_v<VersionPair, fl::Generation, fl::Epoch>);
static_assert(!std::is_constructible_v<VersionPair, std::uint64_t, std::uint64_t>);

// ── The vector clock ───────────────────────────────────────────────

struct ReplayClock {};
struct KernelClock {};

// Clocks of two protocols, or of two widths, never meet.
static_assert(!lattice_accepts<fl::HappensBeforeLattice<2, ReplayClock>, fl::HappensBeforeLattice<2, ReplayClock>::element_type,
                               fl::HappensBeforeLattice<2, KernelClock>::element_type>);
static_assert(!lattice_accepts<fl::HappensBeforeLattice<2>, fl::HappensBeforeLattice<2>::element_type,
                               fl::HappensBeforeLattice<3>::element_type>);
static_assert(lattice_accepts<fl::HappensBeforeLattice<2>, fl::HappensBeforeLattice<2>::element_type,
                              fl::HappensBeforeLattice<2>::element_type>);
static_assert(!std::is_convertible_v<std::array<std::uint64_t, 2>, fl::HappensBeforeLattice<2>::element_type>,
              "a clock is built from an array only through the explicit constructor");

void attack_clock_single_slot() {
    using HB = fl::HappensBeforeLattice<1>;
    std::uint64_t const s = g_seed;
    // One slot is a scalar clock: every pair is ordered, none concurrent.
    for (std::uint64_t i = 0; i < 16; ++i) {
        for (std::uint64_t j = 0; j < 16; ++j) {
            HB::element_type const a{{s * i}};
            HB::element_type const b{{s * j}};
            expect(HB::comparable(a, b) && !HB::is_concurrent(a, b), "one slot is total");
            expect(HB::happens_before(a, b) == (s * i < s * j), "one slot orders by the count");
        }
    }
    HB::element_type const near_top{{kMax - 1}};
    expect(HB::successor_at(near_top, 0) == HB::top(), "one step below the top reaches the top");
    expect(HB::causal_merge(HB::element_type{{3}}, HB::element_type{{kMax - 1}}, 0) == HB::top(),
           "a merge that lands on the top is still a successor of both inputs");
}

void attack_clock_equal_and_concurrent() {
    using HB = fl::HappensBeforeLattice<3>;
    std::uint64_t const s = g_seed;
    HB::element_type const a = fl::make_clock<HB>(s, 2u, 5u);
    HB::element_type const a_again = fl::make_clock<HB>(s, 2u, 5u);

    // Equal clocks are one moment: neither precedes, neither is concurrent.
    expect(!HB::happens_before(a, a_again) && !HB::happens_before(a_again, a), "equal clocks do not precede");
    expect(!HB::is_concurrent(a, a_again) && HB::comparable(a, a_again), "equal clocks are not concurrent");
    expect((a <=> a_again) == std::partial_ordering::equivalent, "equal clocks compare equivalent");
    expect(HB::join(a, a_again) == a && HB::meet(a, a_again) == a, "join and meet are idempotent");

    // Three pairwise concurrent clocks: each leads on its own slot.
    HB::element_type const p = fl::make_clock<HB>(s + 1, 0u, 0u);
    HB::element_type const q = fl::make_clock<HB>(0u, s + 1, 0u);
    HB::element_type const r = fl::make_clock<HB>(0u, 0u, s + 1);
    expect(HB::is_concurrent(p, q) && HB::is_concurrent(q, r) && HB::is_concurrent(p, r), "an antichain of three");
    expect((p <=> q) == std::partial_ordering::unordered, "concurrent clocks are unordered");

    // After a receive, the merged clock follows both inputs and is
    // concurrent with neither.
    HB::element_type const merged = HB::causal_merge(p, q, 0);
    expect(HB::happens_before(p, merged) && HB::happens_before(q, merged), "a receive follows both sides");
    expect(!HB::is_concurrent(p, merged) && !HB::is_concurrent(q, merged), "a receive is ordered after both");
    expect(HB::is_concurrent(merged, r), "a receive stays concurrent with a third process it never heard of");

    // A merge of the whole antichain is above every member.
    HB::element_type const everything = HB::join(HB::join(p, q), r);
    expect(HB::leq(p, everything) && HB::leq(q, everything) && HB::leq(r, everything), "the join is an upper bound");
    expect(HB::meet(HB::meet(p, q), r) == HB::bottom(), "the meet of an antichain of units is the bottom");

    // The top is absorbing and follows everything below it.
    expect(HB::join(HB::top(), a) == HB::top() && HB::happens_before(a, HB::top()), "the top absorbs and follows");
    expect(HB::join(HB::bottom(), a) == a && HB::happens_before(HB::bottom(), a), "the bottom is the identity");
}

// A deterministic run of four processes that exchange messages.  The
// causal order is computed a second time from the events themselves,
// with no clock involved: program order on each process, plus one edge
// from each send to its receive, closed under transitivity.  The clock
// must agree with that order on every pair of events.  This is the
// Mattern-Fidge theorem, checked on one run: e precedes f exactly when
// clock(e) < clock(f).
//
// O(E^3) in the event count E for the closure, and E is fixed at 144.
void attack_clock_against_the_causal_order() {
    constexpr std::size_t kProcesses = 4;
    constexpr std::size_t kEvents = 144;
    using HB = fl::HappensBeforeLattice<kProcesses>;

    std::array<HB::element_type, kEvents> clock_of{};
    std::array<std::array<bool, kEvents>, kEvents> precedes{};

    std::array<HB::element_type, kProcesses> now{};
    std::array<std::size_t, kProcesses> last_event{};
    std::array<bool, kProcesses> has_event{};

    // A message in flight carries the clock and the index of its send.
    struct Message {
        HB::element_type clock{};
        std::size_t send_event = 0;
        std::size_t to = 0;
        bool in_flight = false;
    };
    std::array<Message, kEvents> mailbox{};
    std::size_t mailbox_used = 0;
    std::size_t receive_count = 0;

    std::uint64_t state = g_seed * 0x9E3779B97F4A7C15ULL + 1;
    auto next_random = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    };

    for (std::size_t e = 0; e < kEvents; ++e) {
        std::size_t const p = next_random() % kProcesses;
        std::uint64_t const kind = next_random() % 3;
        if (has_event[p]) precedes[last_event[p]][e] = true;

        // Receive the oldest message addressed to p, if one waits.
        bool received = false;
        if (kind == 2) {
            for (std::size_t m = 0; m < mailbox_used; ++m) {
                if (mailbox[m].in_flight && mailbox[m].to == p) {
                    now[p] = HB::causal_merge(now[p], mailbox[m].clock, p);
                    precedes[mailbox[m].send_event][e] = true;
                    mailbox[m].in_flight = false;
                    received = true;
                    ++receive_count;
                    break;
                }
            }
        }
        if (!received) now[p] = HB::successor_at(now[p], p);

        // A send is a local event whose clock travels with the message.
        if (kind == 1) {
            std::size_t const to = (p + 1 + next_random() % (kProcesses - 1)) % kProcesses;
            mailbox[mailbox_used++] = Message{now[p], e, to, true};
        }
        clock_of[e] = now[p];
        last_event[p] = e;
        has_event[p] = true;
    }

    for (std::size_t k = 0; k < kEvents; ++k) {
        for (std::size_t i = 0; i < kEvents; ++i) {
            if (!precedes[i][k]) continue;
            for (std::size_t j = 0; j < kEvents; ++j) {
                if (precedes[k][j]) precedes[i][j] = true;
            }
        }
    }

    std::size_t concurrent_pairs = 0;
    for (std::size_t i = 0; i < kEvents; ++i) {
        for (std::size_t j = 0; j < kEvents; ++j) {
            if (i == j) continue;
            bool const by_clock = HB::happens_before(clock_of[i], clock_of[j]);
            expect(by_clock == precedes[i][j], "the clock agrees with the causal order");
            if (!precedes[i][j] && !precedes[j][i]) {
                expect(HB::is_concurrent(clock_of[i], clock_of[j]), "unrelated events are concurrent");
                ++concurrent_pairs;
            }
        }
    }
    // The run must exercise both answers, or the check proves little.
    expect(concurrent_pairs > 0, "the run produced concurrent events");
    expect(mailbox_used > 0 && receive_count > 0, "the run sent and received messages");
}

// ── Row-hash identity ───────────────────────────────────────────────

// A tag of another axis that reports the epoch's diagnostic name.  The
// name is a diagnostic and not an identity, so the fold must still keep
// the two apart.
struct forged_epoch_name {
    static constexpr std::string_view lattice_name = "EpochLattice";
};
using ForgedEpochLattice = fl::StrongCounterLattice<forged_epoch_name>;
static_assert(ForgedEpochLattice::name() == fl::EpochLattice::name(), "the forged tag does report the same name");

constexpr std::array<std::uint64_t, 16> kIdentities = {
    fd::row_hash_contribution_v<OnAxis<fl::EpochLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::GenerationLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::PeakBytesLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::BitsBudgetLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::DualLattice<fl::EpochLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::DualLattice<fl::GenerationLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::DualLattice<fl::PeakBytesLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::DualLattice<fl::BitsBudgetLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::DualLattice<fl::DualLattice<fl::EpochLattice>>>>,
    fd::row_hash_contribution_v<OnAxis<ForgedEpochLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<fl::EpochLattice, fl::GenerationLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<fl::GenerationLattice, fl::EpochLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<fl::BitsBudgetLattice, fl::PeakBytesLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<4, ReplayClock>>>,
    fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<4, KernelClock>>>,
    fd::row_hash_contribution_v<OnAxis<fl::HappensBeforeLattice<5, ReplayClock>>>,
};

[[nodiscard]] consteval bool all_distinct_and_nonzero(std::array<std::uint64_t, 16> const& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0 || values[i] == kMax) return false;
        for (std::size_t j = i + 1; j < values.size(); ++j) {
            if (values[i] == values[j]) return false;
        }
    }
    return true;
}
static_assert(all_distinct_and_nonzero(kIdentities),
              "two counter axes, a counter and its dual, a double dual and its source, a forged name, a swapped "
              "product, or two clocks share one row-hash slot");

// The payload is part of the fold only when it carries a row, so a bare
// payload of another type folds to the same slot.  This is the fold's
// stated design: the payload's identity belongs to the content half of
// the cache key.
static_assert(fd::row_hash_contribution_v<OnAxis<fl::EpochLattice>>
              == fd::row_hash_contribution_v<fa::Graded<fa::ModalityKind::Absolute, fl::EpochLattice, double>>);

// ── The ledger ───────────────────────────────────────────────────────
//
// Each entry is an attack that compiles and gives a wrong answer through
// legal code.  Each has a reproducer below that must keep reproducing: a
// fix that closes an entry makes its reproducer fail, and the fix then
// removes the entry and lowers the bound.  The bound only goes down.

struct KnownLimit {
    std::string_view name;
    std::string_view why_it_stays_open;
};

inline constexpr KnownLimit kLedger[] = {
    {"retag through raw()",
     "Generation{epoch.raw()} states a generation from an epoch's count. The explicit constructor is the door "
     "that reads a count off the wire, and it cannot tell where its integer came from."},
    {"retag through std::bit_cast",
     "Every counter is trivially copyable and eight bytes, so std::bit_cast between two axes is legal. A counter "
     "that must cross a wire or sit in a register has to stay trivially copyable."},
    {"a count or a clock from integers claims any history",
     "The order sees values and not events. Epoch{older} after Epoch{newer}, or a clock with every slot at the top, "
     "is a legal construction. Forward progress belongs to the code that advances the value."},
    {"Graded over a version counter in its numeric order",
     "Graded<Absolute, EpochLattice, T>::weaken raises the epoch, because Graded reads up as the weaker claim and "
     "a newer epoch is the stronger one. A value graded by its version must use DualLattice, as fixy/EpochVersioned.h "
     "does. Graded cannot know which orientation a lattice means."},
};
static_assert(std::size(kLedger) <= 4, "the ledger only shrinks");

void reproduce_the_ledger() {
    std::uint64_t const s = g_seed;
    fl::Epoch const epoch{s + 40};

    fl::Generation const retagged{epoch.raw()};
    expect(retagged.raw() == epoch.raw(), "ledger: retag through raw() still reproduces");

    auto const bit_retagged = std::bit_cast<fl::Generation>(epoch);
    expect(bit_retagged.raw() == epoch.raw(), "ledger: retag through std::bit_cast still reproduces");

    fl::Epoch const newer{s + 100};
    fl::Epoch const older_after_newer{newer.raw() - 60};
    expect(fl::EpochLattice::leq(older_after_newer, newer), "ledger: a count from an integer still claims any history");
    using HB = fl::HappensBeforeLattice<2>;
    HB::element_type const forged{{kMax, kMax}};
    expect(HB::leq(fl::make_clock<HB>(s, s), forged), "ledger: a clock from integers still claims any history");

    using NumericVersion = fa::Graded<fa::ModalityKind::Absolute, fl::EpochLattice, int>;
    NumericVersion const stale{1, fl::Epoch{s}};
    NumericVersion const marked_fresh = stale.weaken(fl::Epoch{s + 9});
    expect(marked_fresh.grade() == fl::Epoch{s + 9} && marked_fresh.peek() == 1,
           "ledger: Graded over the numeric epoch order still lets weaken raise the epoch");

    // The dual closes that case on the same substrate: weaken moves to an
    // older epoch only.
    using DualVersion = fa::Graded<fa::ModalityKind::Absolute, fl::DualLattice<fl::EpochLattice>, int>;
    DualVersion const current{1, fl::Epoch{s + 9}};
    expect(current.weaken(fl::Epoch{s}).grade() == fl::Epoch{s}, "the dual weakens toward the older epoch");
    expect(DualVersion{2, fl::Epoch{s}}.compose(current).grade() == fl::Epoch{s},
           "the dual composes to the older epoch");
}

}  // namespace

int main() {
    attack_counter_top<fl::EpochLattice>("counter top: epoch");
    attack_counter_top<fl::GenerationLattice>("counter top: generation");
    attack_counter_top<fl::PeakBytesLattice>("counter top: peak bytes");
    attack_counter_top<fl::BitsBudgetLattice>("counter top: bits");
    attack_clock_single_slot();
    attack_clock_equal_and_concurrent();
    attack_clock_against_the_causal_order();
    reproduce_the_ledger();
    for (std::uint64_t const identity : kIdentities) expect(identity != 0, "an identity reached run time as zero");

    if (g_failures != 0) {
        std::fprintf(stderr, "test_counter_clock_attacks: %d case(s) failed\n", g_failures);
        return 1;
    }
    std::printf("test_counter_clock_attacks: ok\n");
    return 0;
}
