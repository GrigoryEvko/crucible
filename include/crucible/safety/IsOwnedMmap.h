// safety/IsOwnedMmap.h — concept + trait companion for OwnedMmap.
// FIXY-V-231 substrate (also covers Agent 9 §4.3).
//
// ── Why ────────────────────────────────────────────────────────────
//
// `safety/OwnedMmap.h` ships the RAII wrapper; this header ships the
// type-trait + concept consumers use to constrain templates that
// accept "any OwnedMmap, regardless of Tag/Prot/Share".  Mirrors the
// `safety/IsOwnedRegion.h` discipline: the trait gives a flat
// `is_owned_mmap_v<T>` predicate, the concept gives a `requires` /
// `IsOwnedMmap auto&` constraint hook for production code.
//
// Production consumers in V-236 (the perf-hub refactor) use the
// concept to write functions like
//
//   template <safety::IsOwnedMmap M>
//   void install_ring(M&& region) noexcept { ... }
//
// without enumerating every (Tag, Prot, Share) combination at the
// call site.
//
// ── Discipline ─────────────────────────────────────────────────────
//
// `is_owned_mmap_v<T>` and `IsOwnedMmap<T>` recognize the literal
// `OwnedMmap<Tag, Prot, Share>` template instantiation only — NOT
// derived types, NOT subclasses, NOT type-erased wrappers.  This is
// the safety-tree convention: trait-based detection is structural,
// not nominal.
//
// To extract the Tag / Prot / Share from a recognized type, use
// `M::tag_type`, `M::prot_type`, `M::share_type` (member typedefs
// in OwnedMmap itself); IsOwnedMmap guarantees those typedefs exist.
//
// ── Axioms ─────────────────────────────────────────────────────────
//
//   InitSafe   — trait is a constexpr bool, no runtime state.
//   TypeSafe   — primary template returns false; only the partial
//                specialization on OwnedMmap<Tag, Prot, Share>
//                returns true.  No conflation.
//   NullSafe   — no pointers.
//   MemSafe    — no allocations.
//   BorrowSafe — pure constexpr.
//   ThreadSafe — pure constexpr.
//   LeakSafe   — no resource ownership.
//   DetSafe    — same T → same bool at every site, every build.

#pragma once

#include <crucible/safety/OwnedMmap.h>

#include <type_traits>

namespace crucible::safety {

// ── Trait: is_owned_mmap_v<T> ────────────────────────────────────────
//
// Primary template returns false; the partial specialization on
// `OwnedMmap<Tag, Prot, Share>` returns true.  cv-qualified inputs
// fold through `std::remove_cv_t`.

namespace detail {
template <typename T>
struct is_owned_mmap_impl : std::false_type {};

template <typename Tag, typename Prot, typename Share>
struct is_owned_mmap_impl<OwnedMmap<Tag, Prot, Share>> : std::true_type {};
}  // namespace detail

template <typename T>
inline constexpr bool is_owned_mmap_v =
    detail::is_owned_mmap_impl<std::remove_cv_t<T>>::value;

// ── Concept: IsOwnedMmap<T> ──────────────────────────────────────────
//
// `requires` / template-constraint hook.  Use in production
// signatures to demand "any OwnedMmap instantiation" without
// enumerating template parameters.

template <typename T>
concept IsOwnedMmap = is_owned_mmap_v<T>;

// ── Self-test — pin the trait at compile time ────────────────────────

namespace self_test {
struct ProbeTag    {};
struct ProbeProt   {};
struct ProbeShare  {};
using ProbeOwnedMmap = OwnedMmap<ProbeTag, ProbeProt, ProbeShare>;

static_assert(is_owned_mmap_v<ProbeOwnedMmap>,
              "OwnedMmap<...> must satisfy is_owned_mmap_v");
static_assert(is_owned_mmap_v<const ProbeOwnedMmap>,
              "const-qualified OwnedMmap<...> must satisfy is_owned_mmap_v");
static_assert(!is_owned_mmap_v<int>,
              "int must NOT satisfy is_owned_mmap_v");
static_assert(!is_owned_mmap_v<void*>,
              "raw void* must NOT satisfy is_owned_mmap_v");

static_assert(IsOwnedMmap<ProbeOwnedMmap>,
              "OwnedMmap<...> must satisfy IsOwnedMmap concept");
static_assert(!IsOwnedMmap<int>,
              "int must NOT satisfy IsOwnedMmap concept");

// Member typedef access — guaranteed by IsOwnedMmap satisfaction.
static_assert(std::is_same_v<ProbeOwnedMmap::tag_type,   ProbeTag>);
static_assert(std::is_same_v<ProbeOwnedMmap::prot_type,  ProbeProt>);
static_assert(std::is_same_v<ProbeOwnedMmap::share_type, ProbeShare>);
}  // namespace self_test

}  // namespace crucible::safety
