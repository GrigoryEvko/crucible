#pragma once

// The call-shape atoms: how a binding reaches the code it calls.
// Every atom here engages Axis::CallShape.  Each atom here has a rule
// that reads it.
//
// Old spelling: include/crucible/fixy/grant/Dispatch.h.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <cstddef>
#include <tuple>
#include <type_traits>

namespace fixy::atom::dispatch {

inline constexpr atom_seal atom_namespace_seal{};

// Rule D001 of fixy/Collision.h reads the family: a call through a
// pointer whose signature is not noexcept can throw.
template <class FnPtrFamily>
struct indirect_call final : atom_of<Axis::CallShape> {};

// MaxDepth is a proven worst-case self-recursion depth, not an estimate.
// Rule D002 reads it together with the cost of the binding.
template <std::size_t MaxDepth>
struct recurses final : atom_of<Axis::CallShape> {};

}  // namespace fixy::atom::dispatch

namespace fixy::atom::detail {

struct dispatch_sample_family final {};

using dispatch_atom_roster =
    std::tuple<dispatch::indirect_call<dispatch_sample_family>, dispatch::recurses<32>, dispatch::recurses<0>>;

}  // namespace fixy::atom::detail

namespace fixy::atom::detail::dispatch_atom_self_test {

// The roster is a hand list, so this reads the family namespace and
// asks the list about each plain atom it finds.  A parametric atom is
// not covered; fixy/Atom.h says why beside the walk.
static_assert(every_atom_in_is_rostered_<^^::fixy::atom::dispatch, dispatch_atom_roster>(),
              "fixy/atoms/Dispatch.h: an atom declared in fixy::atom::dispatch is missing from "
              "dispatch_atom_roster.");

static_assert(every_roster_member_is_atom_<dispatch_atom_roster>(),
              "fixy/atoms/Dispatch.h: a member of dispatch_atom_roster is not an atom.");
static_assert(every_roster_member_on_axis_<dispatch_atom_roster, Axis::CallShape>(),
              "fixy/atoms/Dispatch.h: every call-shape atom engages Axis::CallShape.");

// The parameter is part of the type.
struct other_family final {};
static_assert(!std::is_same_v<dispatch::indirect_call<dispatch_sample_family>, dispatch::indirect_call<other_family>>);
static_assert(!std::is_same_v<dispatch::recurses<32>, dispatch::recurses<16>>);
static_assert(std::is_same_v<dispatch::recurses<32>, dispatch::recurses<32>>);

}  // namespace fixy::atom::detail::dispatch_atom_self_test
