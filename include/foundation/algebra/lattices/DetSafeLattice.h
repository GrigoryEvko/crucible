#pragma once

// Chain over the determinism of the sources a value's bytes came from.
// bottom is NonDeterministicSyscall and top is Pure.  Stronger
// replay-safety is higher, so leq(weak, strong) reads "a consumer that
// tolerates the weaker tier accepts a stronger provider".  join takes
// the purer of two providers and meet takes the less pure.
//
// The rungs rank how reproducible a source is on replay, not how
// expensive it is to read.  A monotonic clock outranks a wall clock
// because it never steps backwards, and a wall clock outranks entropy
// because entropy is unreproducible by construction.  Philox sits just
// below Pure: the same counter and key give the same bits on any
// machine, but the generator state is still observable.

#include <foundation/algebra/Graded.h>
#include <foundation/algebra/Lattice.h>
#include <foundation/algebra/lattices/ChainLattice.h>
#include <foundation/reflect/Enumerate.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace foundation::algebra::lattices {

enum class DetSafeTier : std::uint8_t {
    NonDeterministicSyscall = 0,  // any syscall whose result cannot be pinned
    FilesystemMtime = 1,  // an externally mutable timestamp
    EntropyRead = 2,  // /dev/urandom, getrandom(2), hardware RNG
    WallClockRead = 3,  // system_clock::now() — can step backwards
    MonotonicClockRead = 4,  // steady_clock::now() — bounded within one run
    PhiloxRng = 5,  // counter-based PRNG, replay-deterministic
    Pure = 6,  // a pure function of the declared inputs
};

inline constexpr std::size_t det_safe_tier_count = ::foundation::reflect::enum_count<DetSafeTier>;

// The identifier of t, or "<unknown DetSafeTier>" for a value outside
// the enum.
[[nodiscard]] consteval std::string_view det_safe_tier_name(DetSafeTier t) noexcept {
    return ::foundation::reflect::enum_name(t);
}

struct DetSafeLattice : ChainLatticeOps<DetSafeTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return DetSafeTier::NonDeterministicSyscall; }
    [[nodiscard]] static constexpr element_type top() noexcept { return DetSafeTier::Pure; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "DetSafeLattice"; }

    // The element keeps the alias that spells the pinned value in this
    // lattice's vocabulary.
    template <DetSafeTier T>
    struct AtElement : PinnedElement<T> {
        using det_safe_tier_value_type = DetSafeTier;
    };

    template <DetSafeTier T>
    struct At : PinnedAt<DetSafeLattice, T, AtElement<T>> {
        static constexpr DetSafeTier tier = T;
    };
};

namespace det_safe_tier {
using NdsTier = DetSafeLattice::At<DetSafeTier::NonDeterministicSyscall>;
using FsMtimeTier = DetSafeLattice::At<DetSafeTier::FilesystemMtime>;
using EntropyTier = DetSafeLattice::At<DetSafeTier::EntropyRead>;
using WallClockTier = DetSafeLattice::At<DetSafeTier::WallClockRead>;
using MonoClockTier = DetSafeLattice::At<DetSafeTier::MonotonicClockRead>;
using PhiloxTier = DetSafeLattice::At<DetSafeTier::PhiloxRng>;
using PureTier = DetSafeLattice::At<DetSafeTier::Pure>;
}  // namespace det_safe_tier

namespace detail::det_safe_lattice_self_test {

static_assert(det_safe_tier_count == 7, "DetSafeTier catalog diverged from {NDS, FsMtime, Entropy, WallClock, "
                                        "MonoClock, Philox, Pure}.  Confirm intent and update the callers "
                                        "that pin a tier.");

// The generic walk covers the declaration order, the exhaustive axioms,
// the reflected names and the shape of every At<tier>.
static_assert(verify_chain_lattice<DetSafeLattice>(), "DetSafeLattice: the chain order, the pinned grades or the "
                                                      "reflected names diverged from the DetSafeTier enumerator list.");

static_assert(!UnboundedLattice<DetSafeLattice>);
static_assert(!Semiring<DetSafeLattice>);

// The specific pins: which enumerators bound the chain, and the exact
// spellings that the reflection builds.
static_assert(DetSafeLattice::bottom() == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::top() == DetSafeTier::Pure);

static_assert(DetSafeLattice::name() == "DetSafeLattice");
static_assert(det_safe_tier::NdsTier::name() == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(det_safe_tier::PureTier::name() == "DetSafeLattice::At<Pure>");
static_assert(DetSafeLattice::At<static_cast<DetSafeTier>(255)>::name() == "DetSafeLattice::At<?>");

static_assert(det_safe_tier_name(DetSafeTier::MonotonicClockRead) == "MonotonicClockRead");
static_assert(det_safe_tier_name(static_cast<DetSafeTier>(255)) == "<unknown DetSafeTier>");

static_assert(det_safe_tier::NdsTier::tier == DetSafeTier::NonDeterministicSyscall);
static_assert(det_safe_tier::PureTier::tier == DetSafeTier::Pure);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using PureGraded = Graded<ModalityKind::Absolute, det_safe_tier::PureTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, int);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PureGraded, double);

template <typename T_>
using PhiloxGraded = Graded<ModalityKind::Absolute, det_safe_tier::PhiloxTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(PhiloxGraded, EightByteValue);

template <typename T_>
using NdsGraded = Graded<ModalityKind::Absolute, det_safe_tier::NdsTier, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(NdsGraded, EightByteValue);

}  // namespace detail::det_safe_lattice_self_test

}  // namespace foundation::algebra::lattices
