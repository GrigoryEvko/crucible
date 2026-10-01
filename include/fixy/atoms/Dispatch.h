#pragma once

// The call-shape atoms: how a binding reaches the code it calls.
// Every atom here engages Axis::CallShape.  Each atom here has a rule
// that reads it.

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
