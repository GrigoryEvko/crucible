#pragma once

// The global-state atoms: the process-wide state a binding touches.
// Every atom here engages Axis::GlobalState.
//
// Each distinct global needs its own tag type. Two globals that share one
// tag collapse to a single atom and become indistinguishable to any
// consumer that walks the atoms of a binding.

#include <fixy/Atom.h>
#include <fixy/Axis.h>

#include <tuple>
#include <type_traits>

namespace fixy::atom::global {

inline constexpr atom_seal atom_namespace_seal{};

// The singleton init graph reads the tag, and walks the graph of the
// tags for an init cycle.
template <class GlobalTag>
struct singleton final : atom_of<Axis::GlobalState> {};

// Rule G002 reads a thread-local slot together with its representation.
template <class TLSTag>
struct thread_local_ final : atom_of<Axis::GlobalState> {};

}  // namespace fixy::atom::global

namespace fixy::atom::detail {

struct global_sample_tag final {};

using global_atom_roster = std::tuple<global::singleton<global_sample_tag>, global::thread_local_<global_sample_tag>>;

}  // namespace fixy::atom::detail
