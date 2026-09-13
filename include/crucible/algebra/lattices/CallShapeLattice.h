#pragma once

// Chain over the dispatch shapes a function exhibits, ordered by how
// little of the call a compiler can resolve statically.  bottom is
// Direct and top is Unbounded.  Each tier permits every shape below it
// plus its own, and a function declaring a tier asserts that its actual
// shapes fit inside that tier's set.
//
// Recursion sits above Direct because it defeats inlining while staying
// analyzable.  An indirect call sits above recursion because a
// recursion's target is known and a function pointer's is not.  A
// virtual call sits above an indirect one because it costs a second
// dependent load before the branch.  Unbounded is the case where
// neither depth nor target can be bounded at all.
//
// A recursion-depth bound is not part of the enum.  The lattice operates
// on plain enum ordinals, and any statically bounded recursion is more
// analyzable than any indirect call whatever the bound, so a bound could
// never reorder the chain.  It belongs with whatever derives a stack
// bound from it.
//
// join is shape union, which is the propagation reading.  A gate that
// admits only the most analyzable shape takes the meet.

#include <crucible/algebra/Graded.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/lattices/ChainLattice.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::algebra::lattices {

enum class CallShape : std::uint8_t {
    Direct = 0,  // every call statically resolved and inlinable
    BoundedRecurses = 1,  // recursion with a statically known depth bound
    Indirect = 2,  // a function-pointer call — concrete target, resolved at runtime
    Virtual = 3,  // vtable dispatch — load the vptr, index a slot, then call
    Unbounded = 4,  // unbounded recursion or a computed target
};

[[nodiscard]] consteval std::string_view call_shape_name(CallShape t) noexcept {
    switch (t) {
        case CallShape::Direct:
            return "Direct";
        case CallShape::BoundedRecurses:
            return "BoundedRecurses";
        case CallShape::Indirect:
            return "Indirect";
        case CallShape::Virtual:
            return "Virtual";
        case CallShape::Unbounded:
            return "Unbounded";
        default:
            return std::string_view{"<unknown CallShape>"};
    }
}

struct CallShapeLattice : ChainLatticeOps<CallShape> {
    [[nodiscard]] static constexpr CallShape bottom() noexcept { return CallShape::Direct; }
    [[nodiscard]] static constexpr CallShape top() noexcept { return CallShape::Unbounded; }
    [[nodiscard]] static consteval std::string_view name() noexcept { return "CallShapeLattice"; }

    template <CallShape T>
    struct At {
        struct element_type {
            using call_shape_value_type = CallShape;
            [[nodiscard]] constexpr operator call_shape_value_type() const noexcept { return T; }
            [[nodiscard]] constexpr bool operator==(element_type) const noexcept { return true; }
        };
        static constexpr CallShape tier = T;
        [[nodiscard]] static constexpr element_type bottom() noexcept { return {}; }
        [[nodiscard]] static constexpr element_type top() noexcept { return {}; }
        [[nodiscard]] static constexpr bool leq(element_type, element_type) noexcept { return true; }
        [[nodiscard]] static constexpr element_type join(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static constexpr element_type meet(element_type, element_type) noexcept { return {}; }
        [[nodiscard]] static consteval std::string_view name() noexcept {
            switch (T) {
                case CallShape::Direct:
                    return "CallShapeLattice::At<Direct>";
                case CallShape::BoundedRecurses:
                    return "CallShapeLattice::At<BoundedRecurses>";
                case CallShape::Indirect:
                    return "CallShapeLattice::At<Indirect>";
                case CallShape::Virtual:
                    return "CallShapeLattice::At<Virtual>";
                case CallShape::Unbounded:
                    return "CallShapeLattice::At<Unbounded>";
                default:
                    return "CallShapeLattice::At<?>";
            }
        }
    };
};

namespace detail::call_shape_lattice_self_test {

inline constexpr std::size_t call_shape_count = std::meta::enumerators_of(^^CallShape).size();

static_assert(call_shape_count == 5, "CallShape diverged from {Direct, BoundedRecurses, Indirect, "
                                     "Virtual, Unbounded}.  A new dispatch tier appends at the next "
                                     "free ordinal and needs the matching call_shape_name() arm and "
                                     "At<T>::name() arm.  Reusing an existing ordinal silently changes "
                                     "every stored row hash.");

static_assert(std::to_underlying(CallShape::Direct) == 0);

static_assert(std::to_underlying(CallShape::Unbounded) == 4);

static_assert(std::is_same_v<std::underlying_type_t<CallShape>, std::uint8_t>);

[[nodiscard]] consteval bool every_call_shape_has_name() noexcept {
    static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^CallShape));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        const auto n = call_shape_name([:en:]);
        if (n == std::string_view{"<unknown CallShape>"}) return false;
        if (n.empty()) return false;
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_call_shape_has_name(), "call_shape_name() switch missing an arm for at least one CallShape "
                                           "enumerator.  Add the arm or the new tier leaks the "
                                           "'<unknown CallShape>' sentinel.");

static_assert(::crucible::algebra::Lattice<CallShapeLattice>);
static_assert(::crucible::algebra::BoundedLattice<CallShapeLattice>);
static_assert(!::crucible::algebra::Semiring<CallShapeLattice>);

static_assert(verify_chain_lattice_exhaustive<CallShapeLattice>(),
              "CallShapeLattice chain-order lattice axioms failed at some triple "
              "— leq/join/meet defect.");
static_assert(verify_chain_lattice_distributive_exhaustive<CallShapeLattice>(),
              "CallShapeLattice chain failed distributivity check — leq/join/meet "
              "defect.");

static_assert(CallShapeLattice::bottom() == CallShape::Direct);
static_assert(CallShapeLattice::top() == CallShape::Unbounded);

static_assert(CallShapeLattice::name() == std::string_view{"CallShapeLattice"});

static_assert(CallShapeLattice::leq(CallShape::Direct, CallShape::Unbounded));
static_assert(!CallShapeLattice::leq(CallShape::Unbounded, CallShape::Direct));

static_assert(CallShapeLattice::leq(CallShape::Direct, CallShape::BoundedRecurses));
static_assert(CallShapeLattice::leq(CallShape::BoundedRecurses, CallShape::Indirect));
static_assert(CallShapeLattice::leq(CallShape::Indirect, CallShape::Virtual));
static_assert(CallShapeLattice::leq(CallShape::Virtual, CallShape::Unbounded));

static_assert(!CallShapeLattice::leq(CallShape::BoundedRecurses, CallShape::Direct));
static_assert(!CallShapeLattice::leq(CallShape::Unbounded, CallShape::Virtual));

static_assert(CallShapeLattice::join(CallShape::Indirect, CallShape::Virtual) == CallShape::Virtual);
static_assert(CallShapeLattice::join(CallShape::Direct, CallShape::BoundedRecurses) == CallShape::BoundedRecurses);

static_assert(CallShapeLattice::meet(CallShape::Unbounded, CallShape::BoundedRecurses) == CallShape::BoundedRecurses);

static_assert(CallShapeLattice::join(CallShape::Direct, CallShape::Unbounded) == CallShape::Unbounded,
              "join returns the least analyzable shape, Unbounded.  A consumer "
              "that reads composition as shape minimization would silently admit "
              "it.  A gate that wants the Direct floor calls meet.");
static_assert(CallShapeLattice::meet(CallShape::Direct, CallShape::Unbounded) == CallShape::Direct,
              "meet returns the most analyzable shape, Direct.  A gate that "
              "admits only what every participant claims calls meet.");

static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Direct>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::BoundedRecurses>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Indirect>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Virtual>::element_type>);
static_assert(std::is_empty_v<CallShapeLattice::At<CallShape::Unbounded>::element_type>);

static_assert(CallShapeLattice::At<CallShape::Indirect>::tier == CallShape::Indirect);

// Static assertions alone can mask consteval, SFINAE and inline-body
// defects.  These calls pass non-constant arguments.
inline void call_shape_lattice_runtime_smoke_test() {
    CallShape a = CallShape::Direct;
    CallShape b = CallShape::Unbounded;
    [[maybe_unused]] bool rl = CallShapeLattice::leq(a, b);
    [[maybe_unused]] CallShape rj = CallShapeLattice::join(a, b);
    [[maybe_unused]] CallShape rm = CallShapeLattice::meet(a, b);

    CallShape c = CallShape::Indirect;
    CallShape d = CallShape::Virtual;
    [[maybe_unused]] CallShape rj2 = CallShapeLattice::join(c, d);
    [[maybe_unused]] CallShape rm2 = CallShapeLattice::meet(c, d);

    CallShapeLattice::At<CallShape::BoundedRecurses>::element_type rec_pin{};
    [[maybe_unused]] CallShape rec_recovered = rec_pin;
}

}  // namespace detail::call_shape_lattice_self_test

}  // namespace crucible::algebra::lattices
