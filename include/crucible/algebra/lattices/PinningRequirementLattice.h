#pragma once

// Chain over how far a thread may migrate between two timestamp reads
// without corrupting the delta.  bottom is NotRequired and top is
// CrossSocketSafe.  A broader coherence domain is higher, so leq(narrow,
// broad) reads "a narrow consumer is satisfied by a broad provider": a
// socket-coherent source serves a per-core consumer.
//
// NotRequired is the absence of a claim, not a verified result.  A
// source shown to need no pinning at all declares CrossSocketSafe.
//
// The hardware fact behind the axis: a timestamp counter is not
// necessarily coherent across cores or across sockets.  A read pair that
// straddles a migration over an offset boundary can report a delta that
// runs backwards and wraps.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>
#include <utility>

namespace crucible::algebra::lattices {

enum class PinningRequirement : std::uint8_t {
    NotRequired = 0,  // no coherence discipline declared
    PerCore = 1,  // coherent within one core, so the mask must be a singleton
    PerSocket = 2,  // coherent within one socket
    CrossSocketSafe = 3,  // coherent across every socket
};

inline constexpr std::size_t pinning_requirement_count = std::meta::enumerators_of(^^PinningRequirement).size();

[[nodiscard]] consteval std::string_view pinning_requirement_name(PinningRequirement p) noexcept {
    switch (p) {
        case PinningRequirement::NotRequired:
            return "NotRequired";
        case PinningRequirement::PerCore:
            return "PerCore";
        case PinningRequirement::PerSocket:
            return "PerSocket";
        case PinningRequirement::CrossSocketSafe:
            return "CrossSocketSafe";
        default:
            return std::string_view{"<unknown PinningRequirement>"};
    }
}

struct PinningRequirementLattice : ChainLatticeOps<PinningRequirement> {
    [[nodiscard]] static constexpr element_type bottom() noexcept { return PinningRequirement::NotRequired; }
    [[nodiscard]] static constexpr element_type top() noexcept { return PinningRequirement::CrossSocketSafe; }

    [[nodiscard]] static consteval std::string_view name() noexcept { return "PinningRequirementLattice"; }

    template <PinningRequirement P>
    struct At {
        struct element_type {
            using pinning_requirement_value_type = PinningRequirement;
            [[nodiscard]] constexpr operator pinning_requirement_value_type() const noexcept { return P; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };

        static constexpr PinningRequirement requirement = P;

        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }

        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (P) {
                case PinningRequirement::NotRequired:
                    return "PinningRequirementLattice::At<NotRequired>";
                case PinningRequirement::PerCore:
                    return "PinningRequirementLattice::At<PerCore>";
                case PinningRequirement::PerSocket:
                    return "PinningRequirementLattice::At<PerSocket>";
                case PinningRequirement::CrossSocketSafe:
                    return "PinningRequirementLattice::At<CrossSocketSafe>";
                default:
                    return "PinningRequirementLattice::At<?>";
            }
        }
    };
};

namespace pinning_requirement {
using NotRequiredPin = PinningRequirementLattice::At<PinningRequirement::NotRequired>;
using PerCorePin = PinningRequirementLattice::At<PinningRequirement::PerCore>;
using PerSocketPin = PinningRequirementLattice::At<PinningRequirement::PerSocket>;
using CrossSocketSafePin = PinningRequirementLattice::At<PinningRequirement::CrossSocketSafe>;
}  // namespace pinning_requirement

namespace detail::pinning_requirement_lattice_self_test {

static_assert(pinning_requirement_count == 4, "PinningRequirement catalog diverged from {NotRequired, PerCore, "
                                              "PerSocket, CrossSocketSafe}.  A new level needs both name "
                                              "switches extended and every composite that names a level "
                                              "rechecked.");

[[nodiscard]] consteval bool every_pinning_requirement_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^PinningRequirement));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (pinning_requirement_name([:en:]) == std::string_view{"<unknown PinningRequirement>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_pinning_requirement_has_name(), "pinning_requirement_name() switch missing an arm for at least one "
                                                    "level.  Add the arm or the new level leaks the "
                                                    "'<unknown PinningRequirement>' sentinel into diagnostic output.");

static_assert(Lattice<PinningRequirementLattice>);
static_assert(BoundedLattice<PinningRequirementLattice>);
static_assert(Lattice<pinning_requirement::NotRequiredPin>);
static_assert(Lattice<pinning_requirement::CrossSocketSafePin>);
static_assert(BoundedLattice<pinning_requirement::CrossSocketSafePin>);

static_assert(!UnboundedLattice<PinningRequirementLattice>);
static_assert(!Semiring<PinningRequirementLattice>);

static_assert(std::is_empty_v<pinning_requirement::NotRequiredPin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::PerCorePin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::PerSocketPin::element_type>);
static_assert(std::is_empty_v<pinning_requirement::CrossSocketSafePin::element_type>);

static_assert(verify_chain_lattice_exhaustive<PinningRequirementLattice>(),
              "PinningRequirementLattice chain-order lattice axioms fail at some "
              "triple.  The defect is in leq, join, meet or the enum encoding.");
static_assert(verify_chain_lattice_distributive_exhaustive<PinningRequirementLattice>(),
              "PinningRequirementLattice chain fails distributivity at some "
              "triple.  A chain order always satisfies it, so the defect is in "
              "join or meet.");

static_assert(PinningRequirementLattice::leq(PinningRequirement::NotRequired, PinningRequirement::PerCore));
static_assert(PinningRequirementLattice::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket));
static_assert(PinningRequirementLattice::leq(PinningRequirement::PerSocket, PinningRequirement::CrossSocketSafe));
static_assert(PinningRequirementLattice::leq(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe));
static_assert(PinningRequirementLattice::leq(PinningRequirement::PerCore, PinningRequirement::PerSocket),
              "A socket-coherent source serves a per-core consumer, because socket "
              "coherence contains core coherence.");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::PerSocket, PinningRequirement::PerCore),
              "A merely core-coherent source does not serve a consumer that migrates "
              "across the socket.  That pairing is the backwards-delta read this "
              "axis forbids.");
static_assert(!PinningRequirementLattice::leq(PinningRequirement::CrossSocketSafe, PinningRequirement::NotRequired));

static_assert(PinningRequirementLattice::bottom() == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::top() == PinningRequirement::CrossSocketSafe);

static_assert(PinningRequirementLattice::join(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
              == PinningRequirement::CrossSocketSafe);
static_assert(PinningRequirementLattice::join(PinningRequirement::PerCore, PinningRequirement::PerSocket)
              == PinningRequirement::PerSocket);
static_assert(PinningRequirementLattice::meet(PinningRequirement::NotRequired, PinningRequirement::CrossSocketSafe)
              == PinningRequirement::NotRequired);
static_assert(PinningRequirementLattice::meet(PinningRequirement::PerCore, PinningRequirement::PerSocket)
              == PinningRequirement::PerCore);

static_assert(PinningRequirementLattice::name() == "PinningRequirementLattice");
static_assert(pinning_requirement::NotRequiredPin::name() == "PinningRequirementLattice::At<NotRequired>");
static_assert(pinning_requirement::PerCorePin::name() == "PinningRequirementLattice::At<PerCore>");
static_assert(pinning_requirement::PerSocketPin::name() == "PinningRequirementLattice::At<PerSocket>");
static_assert(pinning_requirement::CrossSocketSafePin::name() == "PinningRequirementLattice::At<CrossSocketSafe>");

[[nodiscard]] consteval bool every_at_pinning_requirement_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^PinningRequirement));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (PinningRequirementLattice::At<([:en:])>::name() == std::string_view{"PinningRequirementLattice::At<?>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_at_pinning_requirement_has_name(),
              "PinningRequirementLattice::At<P>::name() switch missing an arm for at "
              "least one level.  Add the arm or the new level leaks the "
              "'PinningRequirementLattice::At<?>' sentinel.");

static_assert(pinning_requirement::NotRequiredPin::requirement == PinningRequirement::NotRequired);
static_assert(pinning_requirement::PerCorePin::requirement == PinningRequirement::PerCore);
static_assert(pinning_requirement::PerSocketPin::requirement == PinningRequirement::PerSocket);
static_assert(pinning_requirement::CrossSocketSafePin::requirement == PinningRequirement::CrossSocketSafe);

struct OneByteValue {
    char c{0};
};
struct EightByteValue {
    unsigned long long v{0};
};

template <typename T_>
using CorePinnedGraded = Graded<ModalityKind::Absolute, pinning_requirement::PerCorePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, OneByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, EightByteValue);
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CorePinnedGraded, int);

template <typename T_>
using CrossSocketGraded = Graded<ModalityKind::Absolute, pinning_requirement::CrossSocketSafePin, T_>;
CRUCIBLE_GRADED_LAYOUT_INVARIANT(CrossSocketGraded, EightByteValue);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void runtime_smoke_test() {
    PinningRequirement a = PinningRequirement::NotRequired;
    PinningRequirement b = PinningRequirement::CrossSocketSafe;
    [[maybe_unused]] bool l1 = PinningRequirementLattice::leq(a, b);
    [[maybe_unused]] PinningRequirement j1 = PinningRequirementLattice::join(a, b);
    [[maybe_unused]] PinningRequirement m1 = PinningRequirementLattice::meet(a, b);
    [[maybe_unused]] PinningRequirement bot = PinningRequirementLattice::bottom();
    [[maybe_unused]] PinningRequirement top = PinningRequirementLattice::top();

    PinningRequirement core = PinningRequirement::PerCore;
    PinningRequirement socket = PinningRequirement::PerSocket;
    [[maybe_unused]] PinningRequirement j2 = PinningRequirementLattice::join(core, socket);
    [[maybe_unused]] PinningRequirement m2 = PinningRequirementLattice::meet(core, socket);

    OneByteValue v{42};
    CorePinnedGraded<OneByteValue> initial{v, pinning_requirement::PerCorePin::bottom()};
    auto widened = initial.weaken(pinning_requirement::PerCorePin::top());
    auto composed = initial.compose(widened);
    auto rv_widen = std::move(widened).weaken(pinning_requirement::PerCorePin::top());

    [[maybe_unused]] auto g = rv_widen.grade();
    [[maybe_unused]] auto vc = composed.peek().c;

    pinning_requirement::PerCorePin::element_type e{};
    [[maybe_unused]] PinningRequirement rec = e;
}

}  // namespace detail::pinning_requirement_lattice_self_test

}  // namespace crucible::algebra::lattices
