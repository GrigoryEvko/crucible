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

inline constexpr std::size_t det_safe_tier_count = std::meta::enumerators_of(^^DetSafeTier).size();

[[nodiscard]] consteval std::string_view det_safe_tier_name(DetSafeTier t) noexcept {
    switch (t) {
        case DetSafeTier::NonDeterministicSyscall:
            return "NonDeterministicSyscall";
        case DetSafeTier::FilesystemMtime:
            return "FilesystemMtime";
        case DetSafeTier::EntropyRead:
            return "EntropyRead";
        case DetSafeTier::WallClockRead:
            return "WallClockRead";
        case DetSafeTier::MonotonicClockRead:
            return "MonotonicClockRead";
        case DetSafeTier::PhiloxRng:
            return "PhiloxRng";
        case DetSafeTier::Pure:
            return "Pure";
        default:
            return std::string_view{"<unknown DetSafeTier>"};
    }
}

struct DetSafeLattice : ChainLatticeOps<DetSafeTier> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return DetSafeTier::NonDeterministicSyscall; }
    [[nodiscard]] static constexpr element_type top() noexcept { return DetSafeTier::Pure; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "DetSafeLattice"; }

    template <DetSafeTier T>
    struct At {
        struct element_type {
            using det_safe_tier_value_type = DetSafeTier;
            [[nodiscard]] constexpr operator det_safe_tier_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr DetSafeTier tier = T;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case DetSafeTier::NonDeterministicSyscall:
                    return "DetSafeLattice::At<NonDeterministicSyscall>";
                case DetSafeTier::FilesystemMtime:
                    return "DetSafeLattice::At<FilesystemMtime>";
                case DetSafeTier::EntropyRead:
                    return "DetSafeLattice::At<EntropyRead>";
                case DetSafeTier::WallClockRead:
                    return "DetSafeLattice::At<WallClockRead>";
                case DetSafeTier::MonotonicClockRead:
                    return "DetSafeLattice::At<MonotonicClockRead>";
                case DetSafeTier::PhiloxRng:
                    return "DetSafeLattice::At<PhiloxRng>";
                case DetSafeTier::Pure:
                    return "DetSafeLattice::At<Pure>";
                default:
                    return "DetSafeLattice::At<?>";
            }
        }
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

[[nodiscard]] consteval bool every_det_safe_tier_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DetSafeTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (det_safe_tier_name([:en:]) == std::string_view{"<unknown DetSafeTier>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_det_safe_tier_has_name(), "det_safe_tier_name() switch missing an arm for at least one "
                                              "tier.  Add the arm or the new tier leaks the '<unknown "
                                              "DetSafeTier>' sentinel into diagnostic output.");

static_assert(Lattice<DetSafeLattice>);
static_assert(BoundedLattice<DetSafeLattice>);
static_assert(Lattice<det_safe_tier::NdsTier>);
static_assert(Lattice<det_safe_tier::PureTier>);
static_assert(BoundedLattice<det_safe_tier::PureTier>);

static_assert(!UnboundedLattice<DetSafeLattice>);
static_assert(!Semiring<DetSafeLattice>);

static_assert(std::is_empty_v<det_safe_tier::NdsTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::PureTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::PhiloxTier::element_type>);
static_assert(std::is_empty_v<det_safe_tier::MonoClockTier::element_type>);

static_assert(verify_chain_lattice_exhaustive<DetSafeLattice>(),
              "DetSafeLattice chain-order lattice axioms fail at some triple.  "
              "The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<DetSafeLattice>(),
              "DetSafeLattice chain fails distributivity at some triple.  A chain "
              "order always satisfies it, so the defect is in join or meet.");

static_assert(DetSafeLattice::leq(DetSafeTier::NonDeterministicSyscall, DetSafeTier::FilesystemMtime));
static_assert(DetSafeLattice::leq(DetSafeTier::FilesystemMtime, DetSafeTier::EntropyRead));
static_assert(DetSafeLattice::leq(DetSafeTier::EntropyRead, DetSafeTier::WallClockRead));
static_assert(DetSafeLattice::leq(DetSafeTier::WallClockRead, DetSafeTier::MonotonicClockRead));
static_assert(DetSafeLattice::leq(DetSafeTier::MonotonicClockRead, DetSafeTier::PhiloxRng));
static_assert(DetSafeLattice::leq(DetSafeTier::PhiloxRng, DetSafeTier::Pure));
static_assert(DetSafeLattice::leq(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure));
static_assert(!DetSafeLattice::leq(DetSafeTier::Pure, DetSafeTier::NonDeterministicSyscall));
static_assert(!DetSafeLattice::leq(DetSafeTier::PhiloxRng, DetSafeTier::MonotonicClockRead));

static_assert(DetSafeLattice::bottom() == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::top() == DetSafeTier::Pure);

static_assert(DetSafeLattice::join(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure) == DetSafeTier::Pure);
static_assert(DetSafeLattice::join(DetSafeTier::PhiloxRng, DetSafeTier::MonotonicClockRead) == DetSafeTier::PhiloxRng);
static_assert(DetSafeLattice::meet(DetSafeTier::NonDeterministicSyscall, DetSafeTier::Pure)
              == DetSafeTier::NonDeterministicSyscall);
static_assert(DetSafeLattice::meet(DetSafeTier::PhiloxRng, DetSafeTier::MonotonicClockRead)
              == DetSafeTier::MonotonicClockRead);

static_assert(DetSafeLattice::name() == "DetSafeLattice");
static_assert(det_safe_tier::NdsTier::name() == "DetSafeLattice::At<NonDeterministicSyscall>");
static_assert(det_safe_tier::FsMtimeTier::name() == "DetSafeLattice::At<FilesystemMtime>");
static_assert(det_safe_tier::EntropyTier::name() == "DetSafeLattice::At<EntropyRead>");
static_assert(det_safe_tier::WallClockTier::name() == "DetSafeLattice::At<WallClockRead>");
static_assert(det_safe_tier::MonoClockTier::name() == "DetSafeLattice::At<MonotonicClockRead>");
static_assert(det_safe_tier::PhiloxTier::name() == "DetSafeLattice::At<PhiloxRng>");
static_assert(det_safe_tier::PureTier::name() == "DetSafeLattice::At<Pure>");

[[nodiscard]] consteval bool every_at_det_safe_tier_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^DetSafeTier));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (DetSafeLattice::At<([:en:])>::name() == std::string_view{"DetSafeLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_det_safe_tier_has_name(), "DetSafeLattice::At<T>::name() switch missing an arm for at "
                                                 "least one tier.  Add the arm or the new tier leaks the "
                                                 "'DetSafeLattice::At<?>' sentinel.");

static_assert(det_safe_tier::NdsTier::tier == DetSafeTier::NonDeterministicSyscall);
static_assert(det_safe_tier::FsMtimeTier::tier == DetSafeTier::FilesystemMtime);
static_assert(det_safe_tier::EntropyTier::tier == DetSafeTier::EntropyRead);
static_assert(det_safe_tier::WallClockTier::tier == DetSafeTier::WallClockRead);
static_assert(det_safe_tier::MonoClockTier::tier == DetSafeTier::MonotonicClockRead);
static_assert(det_safe_tier::PhiloxTier::tier == DetSafeTier::PhiloxRng);
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

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    DetSafeTier a = DetSafeTier::NonDeterministicSyscall;
    DetSafeTier b = DetSafeTier::Pure;
    [[maybe_unused]] bool l1 = DetSafeLattice::leq(a, b);
    [[maybe_unused]] DetSafeTier j1 = DetSafeLattice::join(a, b);
    [[maybe_unused]] DetSafeTier m1 = DetSafeLattice::meet(a, b);
    [[maybe_unused]] DetSafeTier bot = DetSafeLattice::bottom();
    [[maybe_unused]] DetSafeTier top = DetSafeLattice::top();

    DetSafeTier mono = DetSafeTier::MonotonicClockRead;
    DetSafeTier philox = DetSafeTier::PhiloxRng;
    [[maybe_unused]] DetSafeTier j2 = DetSafeLattice::join(mono, philox);
    [[maybe_unused]] DetSafeTier m2 = DetSafeLattice::meet(mono, philox);

    OneByteValue v{42};
    PureGraded<OneByteValue> initial{v, det_safe_tier::PureTier::bottom()};
    auto widened = initial.weaken(det_safe_tier::PureTier::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(det_safe_tier::PureTier::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    det_safe_tier::PureTier::element_type e{};
    [[maybe_unused]] DetSafeTier rec = e;
}

}  // namespace detail::det_safe_lattice_self_test

}  // namespace foundation::algebra::lattices
