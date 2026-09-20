#pragma once

// The memory-scope atoms.  Every atom here engages Axis::MemoryScope.
//
// The grades are foundation's MemoryScopeLattice, and its own words say
// what the shape is: two chains that share a bottom and a top, not one
// chain.  The accelerator scopes (Warp, Cta, Cluster, Gpu) and the host
// shareability domains (Inner, Outer) order internally and have no
// relation to one another — a fence at block scope on a GPU neither
// subsumes nor is subsumed by an inner-shareable barrier on the host.
// They meet at a single top because full-system visibility is one
// concept: once every observer on the machine can see a value, there is
// nothing further to distinguish.  Thread is the shared bottom.
//
// That is why fixy/Axis.h files this axis under Tier L rather than Tier
// S: a non-distributive partial order, two vendor trunks that meet only
// at the shared bottom and top.  The trunk is the high nibble of the
// enumerator value, and the lattice exports mem_scope_same_trunk so the
// rules and this header read the division from one place.
//
// MemoryScope is the visibility a publication REACHES; BarrierStrength
// on its own axis is the strength of the fence that publishes it.  The
// two compose by nesting, never through one lattice operation, and V401
// in fixy/Collision.h is the rule that reads them together.
//
// ---------------------------------------------------------------------
// No lift
//
// The third of fixy/atoms/Sync.h's three readings, as in Hw.h and
// Barrier.h.  A scope is how far a publication is visible, not an
// operation performed on a surface a context must admit.  What a scope
// needs from a fence is V401's business, and whether a scope is coherent
// with the binding's pinned architecture is V402's; both are collision
// rules.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <foundation/algebra/lattices/MemoryScopeLattice.h>
#include <foundation/effects/Lift.h>

#include <cstddef>
#include <meta>
#include <tuple>
#include <type_traits>

namespace fixy::atom::scope {

namespace fal = ::foundation::algebra::lattices;

// The shared bottom.
struct thread final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Thread;
};

// The accelerator trunk.
struct warp final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Warp;
};
struct cta final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Cta;
};
struct cluster final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Cluster;
};
struct gpu final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Gpu;
};

// The host shareability trunk.
struct inner final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Inner;
};
struct outer final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::Outer;
};

// The shared top.
struct system final : atom_of<Axis::MemoryScope> {
    static constexpr fal::MemoryScope scope = fal::MemoryScope::System;
};

// Whether a scope reaches at least as far as a floor.  Reads the
// lattice's own leq, so a scope on the OTHER trunk answers false for any
// floor above the shared bottom: Inner is not at or above Cluster,
// because the two are incomparable, not because one is smaller.
[[nodiscard]] consteval bool at_or_above(fal::MemoryScope reached, fal::MemoryScope floor) noexcept {
    return fal::MemoryScopeLattice::leq(floor, reached);
}

// Whether a scope is pinned to ONE trunk — neither the shared bottom nor
// the shared top.  The two shared points are coherent with anything, so
// V402 stands down for them.
[[nodiscard]] consteval bool is_trunk_pinned(fal::MemoryScope scope) noexcept {
    return scope != fal::MemoryScope::Thread && scope != fal::MemoryScope::System;
}

// Whether a pinned scope is on the host shareability trunk, which is the
// ARM DMB ISH/OSH family, as opposed to the accelerator trunk.
[[nodiscard]] consteval bool on_host_trunk(fal::MemoryScope scope) noexcept {
    return fal::mem_scope_same_trunk(scope, fal::MemoryScope::Inner);
}

}  // namespace fixy::atom::scope

namespace fixy::atom::detail {

using scope_atom_roster = std::tuple<scope::thread, scope::warp, scope::cta, scope::cluster, scope::gpu, scope::inner,
                                     scope::outer, scope::system>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::scope_atom_self_test {

namespace fal = ::foundation::algebra::lattices;

static_assert(every_atom_in_is_rostered_<^^::fixy::atom::scope, scope_atom_roster>(),
              "fixy/atoms/Scope.h: an atom declared in fixy::atom::scope is missing from scope_atom_roster.");
static_assert(every_roster_member_is_atom_<scope_atom_roster>(),
              "fixy/atoms/Scope.h: a member of scope_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<scope_atom_roster, Axis::MemoryScope>(),
              "fixy/atoms/Scope.h: every scope atom engages Axis::MemoryScope.");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"

template <fal::MemoryScope S>
[[nodiscard]] consteval std::size_t atoms_claiming_() noexcept {
    std::size_t claims = 0;
    template for (constexpr auto member : roster_members_v<scope_atom_roster>) {
        using A = [:member:];
        if constexpr (A::scope == S) ++claims;
    }
    return claims;
}

[[nodiscard]] consteval bool every_scope_has_exactly_one_atom_() noexcept {
    bool exact = true;
    static constexpr auto scopes = std::define_static_array(std::meta::enumerators_of(^^fal::MemoryScope));
    template for (constexpr auto scope_member : scopes) {
        constexpr fal::MemoryScope scope = [:scope_member:];
        exact = exact && (atoms_claiming_<scope>() == 1);
    }
    return exact;
}

[[nodiscard]] consteval bool no_member_lifts_() noexcept {
    bool none_lift = true;
    template for (constexpr auto member : roster_members_v<scope_atom_roster>) {
        using A = [:member:];
        none_lift = none_lift && !::foundation::effects::LiftsToRow<A>;
    }
    return none_lift;
}

#pragma GCC diagnostic pop

static_assert(every_scope_has_exactly_one_atom_(),
              "fixy/atoms/Scope.h: every MemoryScope enumerator must be claimed by exactly one atom in "
              "fixy::atom::scope.  A scope with no atom cannot be written, and one with two is unreachable.");

static_assert(no_member_lifts_(), "fixy/atoms/Scope.h: a visibility scope names no operation, so no atom here "
                                  "declares lifts_to.  The head of this file says why.");

// The order the rules read, including the cross-trunk cell that makes
// V401 a two-trunk rule rather than a threshold: Inner is incomparable
// with Cluster, so it is neither at or above it nor below it.
static_assert(scope::at_or_above(fal::MemoryScope::Gpu, fal::MemoryScope::Cluster));
static_assert(scope::at_or_above(fal::MemoryScope::Cluster, fal::MemoryScope::Cluster));
static_assert(scope::at_or_above(fal::MemoryScope::System, fal::MemoryScope::Cluster), "the shared top reaches everywhere");
static_assert(!scope::at_or_above(fal::MemoryScope::Cta, fal::MemoryScope::Cluster));
static_assert(!scope::at_or_above(fal::MemoryScope::Inner, fal::MemoryScope::Cluster), "incomparable, not smaller");
static_assert(!scope::at_or_above(fal::MemoryScope::Cluster, fal::MemoryScope::Inner));

// The trunk predicates V402 reads.
static_assert(!scope::is_trunk_pinned(fal::MemoryScope::Thread));
static_assert(!scope::is_trunk_pinned(fal::MemoryScope::System));
static_assert(scope::is_trunk_pinned(fal::MemoryScope::Cta) && scope::is_trunk_pinned(fal::MemoryScope::Outer));
static_assert(scope::on_host_trunk(fal::MemoryScope::Inner) && scope::on_host_trunk(fal::MemoryScope::Outer));
static_assert(!scope::on_host_trunk(fal::MemoryScope::Cta) && !scope::on_host_trunk(fal::MemoryScope::Gpu));

static_assert(!std::is_same_v<scope::cta, scope::cluster>);

}  // namespace fixy::atom::detail::scope_atom_self_test
