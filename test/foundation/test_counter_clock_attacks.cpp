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
#include <foundation/effects/Ctx.h>

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
#include "../test_assert.h"

namespace {

namespace fa = ::foundation::algebra;
namespace fl = ::foundation::algebra::lattices;
namespace fd = ::foundation::diag;
namespace fe = ::foundation::effects;

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

// The authority for the carriers built below.  It hands its key out,
// which an authority in production code never does.
struct test_authority {
    [[nodiscard]] static constexpr fa::grade_key<test_authority> key() noexcept {
        return fa::grade_key<test_authority>{};
    }
};

// A test scope that owns IO, the capability the checked read needs.
using IoCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

// An image written by hand: the axis word, then one word per slot.  This
// is the only way a test reaches a count or a clock at a number of its
// choice, and it goes through the one door that reads bytes.
template <typename L, std::size_t Words>
typename L::image_type image_with(std::uint64_t axis, std::array<std::uint64_t, Words> const& words) {
    typename L::image_type image{};
    static_assert(image.size() == 8 * (Words + 1));
    for (std::size_t i = 0; i < 8; ++i)
        image[i] = static_cast<std::byte>((axis >> (8 * i)) & 0xFFu);
    for (std::size_t w = 0; w < Words; ++w) {
        for (std::size_t i = 0; i < 8; ++i) {
            image[8 * (w + 1) + i] = static_cast<std::byte>((words[w] >> (8 * i)) & 0xFFu);
        }
    }
    return image;
}

template <typename L>
typename L::element_type count_at(std::uint64_t count) {
    IoCtx const ctx{fe::testing::test()};
    auto const read = L::mint_from_image(ctx, image_with<L, 1>(L::image_axis(), {count}));
    if (!read) std::abort();
    return *read;
}

template <typename HB>
typename HB::element_type clock_at(std::array<std::uint64_t, HB::process_count> const& slots) {
    IoCtx const ctx{fe::testing::test()};
    auto const read = HB::mint_from_image(ctx, image_with<HB, HB::process_count>(HB::image_axis(), slots));
    if (!read) std::abort();
    return *read;
}

// ── Counters at the top ─────────────────────────────────────────────

template <typename L>
void attack_counter_top(char const* axis) {
    using E = typename L::element_type;
    std::uint64_t const s = g_seed;
    E const top = L::top();
    E const low = count_at<L>(s);
    E const near_top = count_at<L>(kMax - s);

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
    expect(L::successor(L::bottom()) == count_at<L>(1), axis);

    // An image carries its count through a round trip, at both ends.
    IoCtx const ctx{fe::testing::test()};
    expect(L::mint_from_image(ctx, L::image_of(top)) == top, axis);
    expect(L::mint_from_image(ctx, L::image_of(L::bottom())) == L::bottom(), axis);
    expect(L::mint_from_image(ctx, L::image_of(near_top)) == near_top, axis);

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
concept lattice_accepts = requires(A a, B b) { L::join(a, b); } || requires(A a, B b) { L::leq(a, b); };

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
// direction.  No constructor takes an integer, and a bound, which does,
// never becomes a count.
static_assert(!std::is_convertible_v<fl::Epoch, std::uint64_t> && !std::is_convertible_v<std::uint64_t, fl::Epoch>);
static_assert(!std::is_convertible_v<int, fl::Epoch> && !std::is_convertible_v<bool, fl::Epoch>);
static_assert(!std::is_constructible_v<fl::Epoch, std::uint64_t> && !std::is_constructible_v<fl::Generation, int>);
static_assert(!std::is_constructible_v<fl::Epoch, fl::EpochBound>
              && !std::is_constructible_v<fl::Epoch, fl::Generation>);
static_assert(std::is_constructible_v<fl::EpochBound, std::uint64_t>
              && std::is_constructible_v<fl::EpochBound, fl::Epoch>);

// A count is not buildable from bytes: std::bit_cast needs a trivially
// copyable type, and the checked lifetime start refuses the annotation.
// The copy stays trivial, so a count still passes in a register.
template <typename To, typename From>
concept bit_castable = requires(From from) { std::bit_cast<To>(from); };
static_assert(!bit_castable<fl::Generation, fl::Epoch> && !bit_castable<fl::Epoch, std::uint64_t>);
static_assert(!bit_castable<fl::HappensBeforeLattice<2>::element_type, std::array<std::uint64_t, 2>>);
static_assert(bit_castable<std::uint64_t, std::int64_t>, "the detector answers yes where a cast exists");
static_assert(std::is_trivially_copy_constructible_v<fl::Epoch> && std::is_trivially_destructible_v<fl::Epoch>);

// A product of two axes refuses its components in the swapped order.
using VersionPair = fl::ProductLattice<fl::EpochLattice, fl::GenerationLattice>::element_type;
static_assert(std::is_constructible_v<VersionPair, fl::Epoch, fl::Generation>);
static_assert(!std::is_constructible_v<VersionPair, fl::Generation, fl::Epoch>);
static_assert(!std::is_constructible_v<VersionPair, std::uint64_t, std::uint64_t>);

// ── The vector clock ───────────────────────────────────────────────

}  // namespace

// The clock tags and the forged tag below have a name outside an unnamed
// namespace, because a stable id refuses a type with internal linkage.
namespace test_counter_clock_types {
struct ReplayClock {};
struct KernelClock {};
}  // namespace test_counter_clock_types

namespace {

using namespace test_counter_clock_types;

// Clocks of two protocols, or of two widths, never meet.
static_assert(
    !lattice_accepts<fl::HappensBeforeLattice<2, ReplayClock>, fl::HappensBeforeLattice<2, ReplayClock>::element_type,
                     fl::HappensBeforeLattice<2, KernelClock>::element_type>);
static_assert(!lattice_accepts<fl::HappensBeforeLattice<2>, fl::HappensBeforeLattice<2>::element_type,
                               fl::HappensBeforeLattice<3>::element_type>);
static_assert(lattice_accepts<fl::HappensBeforeLattice<2>, fl::HappensBeforeLattice<2>::element_type,
                              fl::HappensBeforeLattice<2>::element_type>);
static_assert(!std::is_convertible_v<std::array<std::uint64_t, 2>, fl::HappensBeforeLattice<2>::element_type>
                  && !std::is_constructible_v<fl::HappensBeforeLattice<2>::element_type, std::array<std::uint64_t, 2>>,
              "no array of integers states a clock");

void attack_clock_single_slot() {
    using HB = fl::HappensBeforeLattice<1>;
    std::uint64_t const s = g_seed;
    // One slot is a scalar clock: every pair is ordered, none concurrent.
    for (std::uint64_t i = 0; i < 16; ++i) {
        for (std::uint64_t j = 0; j < 16; ++j) {
            HB::element_type const a = clock_at<HB>({s * i});
            HB::element_type const b = clock_at<HB>({s * j});
            expect(HB::comparable(a, b) && !HB::is_concurrent(a, b), "one slot is total");
            expect(HB::happens_before(a, b) == (s * i < s * j), "one slot orders by the count");
        }
    }
    HB::element_type const near_top = clock_at<HB>({kMax - 1});
    expect(HB::successor_at(near_top, 0) == HB::top(), "one step below the top reaches the top");
    expect(HB::causal_merge(clock_at<HB>({3}), near_top, 0) == HB::top(),
           "a merge that lands on the top is still a successor of both inputs");
}

// A clock carries its slots through an image, and an image of one
// protocol or width does not read back as another.
void attack_clock_images() {
    using Replay = fl::HappensBeforeLattice<3, ReplayClock>;
    using Kernel = fl::HappensBeforeLattice<3, KernelClock>;
    std::uint64_t const s = g_seed;
    IoCtx const ctx{fe::testing::test()};
    Replay::element_type const clock = clock_at<Replay>({s, s + 1, kMax});
    expect(Replay::mint_from_image(ctx, Replay::image_of(clock)) == clock, "a clock survives its image");
    auto const as_kernel = Kernel::mint_from_image(ctx, image_with<Kernel, 3>(Replay::image_axis(), {s, s + 1, kMax}));
    expect(!as_kernel && as_kernel.error() == fl::CountImageError::OtherAxis,
           "an image of one protocol does not read back as another");
}

void attack_clock_equal_and_concurrent() {
    using HB = fl::HappensBeforeLattice<3>;
    std::uint64_t const s = g_seed;
    HB::element_type const a = clock_at<HB>({s, 2, 5});
    HB::element_type const a_again = clock_at<HB>({s, 2, 5});

    // Equal clocks are one moment: neither precedes, neither is concurrent.
    expect(!HB::happens_before(a, a_again) && !HB::happens_before(a_again, a), "equal clocks do not precede");
    expect(!HB::is_concurrent(a, a_again) && HB::comparable(a, a_again), "equal clocks are not concurrent");
    expect((a <=> a_again) == std::partial_ordering::equivalent, "equal clocks compare equivalent");
    expect(HB::join(a, a_again) == a && HB::meet(a, a_again) == a, "join and meet are idempotent");

    // Three pairwise concurrent clocks: each leads on its own slot.
    HB::element_type const p = clock_at<HB>({s + 1, 0, 0});
    HB::element_type const q = clock_at<HB>({0, s + 1, 0});
    HB::element_type const r = clock_at<HB>({0, 0, s + 1});
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

}  // namespace

// A tag of another axis that reports the epoch's diagnostic name.  The
// name is a diagnostic and not an identity, so the fold must still keep
// the two apart.
namespace test_counter_clock_types {
struct forged_epoch_name {
    static constexpr std::string_view lattice_name = "EpochLattice";
    static constexpr ::foundation::algebra::ClaimOrientation claim_orientation =
        ::foundation::algebra::ClaimOrientation::stronger_is_higher;
};
}  // namespace test_counter_clock_types

namespace {

using ForgedEpochLattice = fl::StrongCounterLattice<forged_epoch_name>;
static_assert(ForgedEpochLattice::name() == fl::EpochLattice::name(), "the forged tag does report the same name");

// The wire identity is the source path of the tag, not its diagnostic
// name.  An image of the forged axis is not an image of the epoch.
static_assert(ForgedEpochLattice::image_axis() != fl::EpochLattice::image_axis(),
              "a tag that copies the name of the epoch shares its image identity");

// A tag in an unnamed namespace has no source path: every translation unit
// has its own such tag under one name.  Its counter has no image door.
struct hidden_axis {
    static constexpr std::string_view lattice_name = "HiddenAxis";
    static constexpr fa::ClaimOrientation claim_orientation = fa::ClaimOrientation::weaker_is_higher;
};
template <typename L>
concept has_image_door = requires(typename L::element_type count) { L::image_of(count); };
static_assert(has_image_door<fl::EpochLattice> && has_image_door<ForgedEpochLattice>);
static_assert(!has_image_door<fl::StrongCounterLattice<hidden_axis>>);
static_assert(!has_image_door<fl::HappensBeforeLattice<2, hidden_axis>>);

template <typename L>
using Dual = fl::DualLattice<L>;

// Graded refuses a version counter in its numeric order, and the version
// axes go in through their duals.  A use counter goes in as it is.
constexpr std::array<std::uint64_t, 13> kIdentities = {
    fd::row_hash_contribution_v<OnAxis<Dual<fl::EpochLattice>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<fl::GenerationLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::PeakBytesLattice>>,
    fd::row_hash_contribution_v<OnAxis<fl::BitsBudgetLattice>>,
    fd::row_hash_contribution_v<OnAxis<Dual<Dual<Dual<fl::EpochLattice>>>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<Dual<fl::PeakBytesLattice>>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<ForgedEpochLattice>>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<Dual<fl::EpochLattice>, Dual<fl::GenerationLattice>>>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<Dual<fl::GenerationLattice>, Dual<fl::EpochLattice>>>>,
    fd::row_hash_contribution_v<OnAxis<fl::ProductLattice<fl::BitsBudgetLattice, fl::PeakBytesLattice>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<fl::HappensBeforeLattice<4, ReplayClock>>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<fl::HappensBeforeLattice<4, KernelClock>>>>,
    fd::row_hash_contribution_v<OnAxis<Dual<fl::HappensBeforeLattice<5, ReplayClock>>>>,
};

// ── The orientation Graded reads ────────────────────────────────────

template <typename L>
concept can_grade = requires { typename OnAxis<L>; };

// A version counter in its numeric order is refused wherever it appears:
// alone, under a double dual, inside a product, or in a product beside a
// dual.  Its dual, and a use counter, are accepted.  The positive cases
// prove the detector can answer yes.
static_assert(!can_grade<fl::EpochLattice> && !can_grade<fl::GenerationLattice>);
static_assert(!can_grade<Dual<Dual<fl::EpochLattice>>>);
static_assert(!can_grade<fl::ProductLattice<fl::EpochLattice, fl::GenerationLattice>>);
static_assert(!can_grade<fl::ProductLattice<Dual<fl::EpochLattice>, fl::GenerationLattice>>);
static_assert(!can_grade<ForgedEpochLattice>);
static_assert(can_grade<Dual<fl::EpochLattice>> && can_grade<fl::PeakBytesLattice>);

// The dual of a use counter puts the stronger claim higher: fewer bytes
// than were used.  Graded refuses it too.
static_assert(!can_grade<Dual<fl::PeakBytesLattice>> && !can_grade<Dual<fl::BitsBudgetLattice>>);

// A clock claims the history it saw.  A larger clock is the stronger
// claim, as a newer version is.  The clock is refused in its pointwise
// order and accepted through its dual.
static_assert(fa::claim_orientation_v<fl::HappensBeforeLattice<4>> == fa::ClaimOrientation::stronger_is_higher);
static_assert(!can_grade<fl::HappensBeforeLattice<4>> && can_grade<Dual<fl::HappensBeforeLattice<4>>>);

[[nodiscard]] consteval bool all_distinct_and_nonzero(std::array<std::uint64_t, 13> const& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == 0 || values[i] == kMax) return false;
        for (std::size_t j = i + 1; j < values.size(); ++j) {
            if (values[i] == values[j]) return false;
        }
    }
    return true;
}
static_assert(all_distinct_and_nonzero(kIdentities),
              "two counter axes, a counter and a double dual of it, a forged name, a swapped product, or two clocks "
              "share one row-hash slot");

// The payload is part of the fold only when it carries a row, so a bare
// payload of another type folds to the same slot.  This is the fold's
// stated design: the payload's identity belongs to the content half of
// the cache key.
static_assert(fd::row_hash_contribution_v<OnAxis<fl::PeakBytesLattice>>
              == fd::row_hash_contribution_v<fa::Graded<fa::ModalityKind::Absolute, fl::PeakBytesLattice, double>>);

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
    {"an image written by hand under a context that owns IO",
     "A count that crosses a wire or goes to storage must come back, so a read from bytes exists, and the bytes that "
     "storage returns are whatever was written. The read names the axis and needs IO, so no integer and no count of "
     "another axis reaches it, and an image of one axis does not read as another. A program that writes an image of "
     "an axis on purpose is the author of that record, and only an authenticated store, not a type, can tell that "
     "record from one the owner wrote."},
};
static_assert(std::size(kLedger) <= 1, "the ledger only shrinks");

void reproduce_the_ledger() {
    std::uint64_t const s = g_seed;
    fl::Epoch const epoch = count_at<fl::EpochLattice>(s + 40);
    IoCtx const ctx{fe::testing::test()};
    auto const authored = fl::GenerationLattice::mint_from_image(
        ctx, image_with<fl::GenerationLattice, 1>(fl::GenerationLattice::image_axis(), {epoch.raw()}));
    expect(authored.has_value() && authored->raw() == epoch.raw(),
           "ledger: an image written by hand under IO still loads as a count of any axis");
}

// The closed routes, each with a check that it stays closed.  A count
// of one axis is not a count of another through raw(),
// through std::bit_cast or through an image, and no integer states a
// count or a clock.  The attacks that the compiler refuses are the
// negative fixtures neg_strong_counter_from_integer,
// neg_strong_counter_bit_cast_retag, neg_strong_counter_start_as_array and
// neg_happens_before_clock_from_integers.
void attack_the_closed_routes() {
    std::uint64_t const s = g_seed;
    IoCtx const ctx{fe::testing::test()};
    fl::Epoch const epoch = count_at<fl::EpochLattice>(s + 40);

    // An image of an epoch does not read back as a generation.
    auto const relabelled = fl::GenerationLattice::mint_from_image(ctx, fl::EpochLattice::image_of(epoch));
    expect(!relabelled && relabelled.error() == fl::CountImageError::OtherAxis,
           "an image of an epoch is refused as a generation");
    auto const as_bits =
        fl::BitsBudgetLattice::mint_from_image(ctx, fl::PeakBytesLattice::image_of(count_at<fl::PeakBytesLattice>(s)));
    expect(!as_bits && as_bits.error() == fl::CountImageError::OtherAxis, "an image of bytes is refused as bits");
    auto const as_forged = ForgedEpochLattice::mint_from_image(ctx, fl::EpochLattice::image_of(epoch));
    expect(!as_forged && as_forged.error() == fl::CountImageError::OtherAxis,
           "an image of the epoch is refused by a tag that copies its name");

    // The raw count is an integer, and an integer builds a bound, which
    // claims nothing: it cannot be joined, advanced or graded.
    fl::GenerationBound const bound{epoch.raw()};
    expect(bound.raw() == epoch.raw(), "raw() gives the count as an integer");
    static_assert(!std::is_constructible_v<fl::Generation, fl::GenerationBound>);

    // A count reached after a newer one is reached by steps from genesis,
    // so its count is the number of steps in its own derivation.
    fl::Epoch const older = fl::EpochLattice::successor(fl::EpochLattice::bottom());
    expect(older.raw() == 1 && fl::EpochLattice::leq(older, epoch), "a derived count is its number of steps");
}

// Graded over a version counter in its numeric order does not compile, so
// no weaken() can raise an epoch.  The dual is the one that compiles, and
// on it weaken() and compose() move toward the older epoch only.  Operands
// come from run time, so a body that only folds in a constant expression
// cannot pass.
void attack_graded_version_orientation() {
    std::uint64_t const s = g_seed;
    using DualVersion = fa::Graded<fa::ModalityKind::Absolute, Dual<fl::EpochLattice>, int>;
    fl::Epoch const older = count_at<fl::EpochLattice>(s);
    fl::Epoch const newer = count_at<fl::EpochLattice>(s + 9);
    DualVersion const current{test_authority::key(), 1, newer};
    expect(current.weaken(older).grade() == older, "the dual weakens toward the older epoch");
    expect(DualVersion{test_authority::key(), 2, older}.compose(current).grade() == older,
           "the dual composes to the older epoch");
    expect(!Dual<fl::EpochLattice>::leq(older, newer),
           "an older epoch is not below a newer one in the dual, so no weaken reaches the newer");
}

}  // namespace

int main() {
    attack_counter_top<fl::EpochLattice>("counter top: epoch");
    attack_counter_top<fl::GenerationLattice>("counter top: generation");
    attack_counter_top<fl::PeakBytesLattice>("counter top: peak bytes");
    attack_counter_top<fl::BitsBudgetLattice>("counter top: bits");
    attack_clock_single_slot();
    attack_clock_images();
    attack_clock_equal_and_concurrent();
    attack_clock_against_the_causal_order();
    attack_graded_version_orientation();
    attack_the_closed_routes();
    reproduce_the_ledger();
    for (std::uint64_t const identity : kIdentities)
        expect(identity != 0, "an identity reached run time as zero");

    if (g_failures != 0) {
        std::fprintf(stderr, "test_counter_clock_attacks: %d case(s) failed\n", g_failures);
        return 1;
    }
    crucible::test::pass("test_counter_clock_attacks: ok\n");
    return 0;
}
