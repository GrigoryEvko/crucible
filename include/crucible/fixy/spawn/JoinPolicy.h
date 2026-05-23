#pragma once

// ── crucible::fixy::spawn::join — spawn-mechanism phantom tag tree ──
//
// FIXY-V-203 (Agent 7 §fixy::spawn::).  Six phantom tags identifying
// the OS/runtime primitive that creates AND disposes a child task:
//
//     AutoJoin     std::jthread + RAII destructor joins (THE default).
//     ManualJoin   std::jthread + caller explicitly .join()s before
//                  the handle's dtor fires.  Acknowledged opt-in to
//                  manage the join site outside the spawn block.
//     Detached     std::jthread followed by .detach() (or std::thread
//                  with explicit detach).  Banned-by-default; V-204
//                  attaches `grant::detach_with<Rationale>` so the
//                  opt-in is grep-discoverable and audit-trailed.
//     Cloned       Linux raw clone(2).  Banned in production code.
//                  Allowed only in perf/bpf/cog loaders that need
//                  CLONE_VM / CLONE_THREAD / CLONE_FILES / no-libc
//                  semantics, gated by `grant::syscall_only` (V-204).
//     Forked       fork(2).  Banned everywhere except CLI launcher
//                  tools that genuinely fork a child process image.
//                  Requires `grant::subprocess<Rationale>` + the
//                  CRUCIBLE_SPAWN_ALLOW_PROCESS CMake opt-in
//                  (V-210 already shipped the script-side guard).
//     PosixSpawn   posix_spawn(3).  Same audit posture as Forked;
//                  used only by the test-harness fork-then-exec
//                  helpers that drive the integration suite.
//
// ── Why this is a SEPARATE axis from V-078 JoinPolicyLattice ────────
//
// `safety::JoinPolicy` (V-079, on top of V-078 lattice) encodes the
// STRICTNESS of the parent's engagement with the spawned child —
// FORGET ⊑ DETACH ⊑ ABANDON ⊑ CANCEL ⊑ WAIT_DEADLINE ⊑ JOIN_ALL.
// `fixy::spawn::join::*` encodes the SPAWN MECHANISM (which syscall
// or runtime primitive materializes the child).  The two axes are
// orthogonal: an AutoJoin-mechanism task may run under JOIN_ALL
// strictness (standard fork-section), but it also may run under
// WAIT_DEADLINE if the parent imposes a wall-clock budget; a
// Detached-mechanism task is structurally constrained to FORGET or
// DETACH strictness, but the AXIS itself is mechanism — what
// primitive made the handle, not how the parent waits on it.  Keeping
// the two axes distinct lets V-204 attach mechanism-specific grants
// (`grant::detach_with`, `grant::syscall_only`, `grant::subprocess`)
// to the SPAWN site while V-079 stays purely about STRICTNESS at the
// JOIN site.
//
// The unfortunate name collision (`safety::JoinPolicy` vs
// `fixy::spawn::join::JoinMechanism`) is deliberate: the file lives
// in `spawn/JoinPolicy.h` because Agent 7's surface map (file lines
// 4146-4168) calls this header "JoinPolicy.h" — the meaning is "policy
// surrounding the spawn-time join arrangement," NOT "the strictness
// lattice."  The enum itself is spelled `JoinMechanism` to keep the
// two axes from colliding at use sites where both are in scope.
//
// ── §XXI compliance ────────────────────────────────────────────────
//
// V-203 ships NO new `mint_*` factory — the tags are passive type-
// level identifiers; V-204 will ship the rationale-bearing grants
// AND the mint_spawn variant that consumes both axes.  The tags
// themselves are §XXI-pattern-conformant phantom types:
//
//   * Empty struct (sizeof = 1, EBO-collapsible inside Graded /
//     Permission / fork-result tuples).
//   * `final` to prevent inheritance-laundering an imposter tag in.
//   * `mechanism` static constexpr field for reverse lookup.
//
// ── HS14 floor ─────────────────────────────────────────────────────
//
// Per CLAUDE.md HS14: "Every new mint factory MUST ship at least 2
// distinct-mismatch-class neg-compile fixtures."  V-203 ships no new
// mint, but the surface introduces a CONCEPT (`IsJoinMechanismTag`)
// whose purpose is to gate downstream consumers (V-204 mint_spawn).
// Two fixtures witness the gate fires across distinct mismatch axes:
//
//   1. test/fixy_neg/neg_fixy_v_203_join_concept_rejects_non_tag.cpp
//        — IsJoinMechanismTag<int> false → the concept rejects a
//          fundamentally non-tag type (no `mechanism` member at all).
//   2. test/fixy_neg/neg_fixy_v_203_join_imposter_struct.cpp
//        — IsJoinMechanismTag<UserImposter> false → the concept
//          rejects an arbitrary struct that DOES carry `static
//          constexpr JoinMechanism mechanism` (so it satisfies the
//          field-shape check) but is NOT one of the 6 declared tag
//          types.  The `is_same_v` allowlist inside the concept is
//          the load-bearing identity gate; the fixture witnesses it
//          fires.  Distinct from fixture #1 because the rejection
//          axis differs: #1 is "no mechanism member"; #2 is "wrong
//          type identity despite well-formed mechanism member."
//
// ── Sentinel TU ───────────────────────────────────────────────────
//
// test/test_fixy_v_203_spawn_join_policy.cpp forces every header-
// embedded static_assert under project warning flags (per
// feedback_header_only_static_assert_blind_spot) AND adds the
// surface-level positive checks: cardinality, name-coverage,
// concept-admission for every tag, mechanism-of-v round-trip,
// EBO size (each tag is 1 byte).
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — tags are empty types; no state.
//   TypeSafe   — JoinMechanism is `enum class : uint8_t` (strong);
//                IsJoinMechanismTag's `is_same_v` allowlist is the
//                identity gate.
//   NullSafe   — no pointers in the surface.
//   MemSafe    — phantom tags have no resources.
//   BorrowSafe — phantom tags are not transferred between threads;
//                they're TYPE-LEVEL identifiers, not values.
//   ThreadSafe — same.
//   LeakSafe   — same.
//   DetSafe    — every operation in this header is `consteval` or
//                `constexpr`; the surface produces no runtime work.

#include <crucible/Platform.h>

#include <cstdint>
#include <meta>
#include <string_view>
#include <type_traits>

namespace crucible::fixy::spawn::join {

// ── JoinMechanism enum ─────────────────────────────────────────────
//
// Strong-typed identifier for the spawn primitive.  Append-only
// universe per CLAUDE.md §XVI canonical-nesting rule: every
// JoinMechanism enumerator's underlying value is FROZEN at first
// ship.  Adding a future mechanism (e.g. `IoUringSqpoll = 6` for
// io_uring kernel-thread-based offload) lands at the next free
// position; existing positions never renumber.  This keeps any
// federation cache slot that mentions a JoinMechanism stable across
// versions.
enum class JoinMechanism : std::uint8_t {
    AutoJoin   = 0,   // std::jthread, RAII destructor joins — DEFAULT.
    ManualJoin = 1,   // std::jthread, caller explicitly .join()s.
    Detached   = 2,   // .detach() on jthread/thread — needs detach_with.
    Cloned     = 3,   // Linux raw clone(2) — needs syscall_only.
    Forked     = 4,   // fork(2) — needs subprocess + CMake opt-in.
    PosixSpawn = 5,   // posix_spawn(3) — needs subprocess + CMake opt-in.
};

// Cardinality via P2996 reflection — auto-bumps on universe extension;
// reflection-based name coverage assertion catches a missed switch arm.
inline constexpr std::size_t join_mechanism_count =
    std::meta::enumerators_of(^^JoinMechanism).size();

// Diagnostic name — switch over the enum.
[[nodiscard]] consteval std::string_view name_of(JoinMechanism m) noexcept {
    switch (m) {
        case JoinMechanism::AutoJoin:   return "AutoJoin";
        case JoinMechanism::ManualJoin: return "ManualJoin";
        case JoinMechanism::Detached:   return "Detached";
        case JoinMechanism::Cloned:     return "Cloned";
        case JoinMechanism::Forked:     return "Forked";
        case JoinMechanism::PosixSpawn: return "PosixSpawn";
        default:                        return std::string_view{"<unknown JoinMechanism>"};
    }
}

// ── Six phantom tags ───────────────────────────────────────────────
//
// Each tag is `final` (prevents inheritance-laundering an imposter)
// and carries a `static constexpr JoinMechanism mechanism` field for
// reverse lookup.  Empty struct → sizeof == 1 → EBO-collapsible
// inside Graded / Permission / fork-result tuples.
struct AutoJoin   final {
    static constexpr JoinMechanism mechanism = JoinMechanism::AutoJoin;
};
struct ManualJoin final {
    static constexpr JoinMechanism mechanism = JoinMechanism::ManualJoin;
};
struct Detached   final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Detached;
};
struct Cloned     final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Cloned;
};
struct Forked     final {
    static constexpr JoinMechanism mechanism = JoinMechanism::Forked;
};
struct PosixSpawn final {
    static constexpr JoinMechanism mechanism = JoinMechanism::PosixSpawn;
};

// ── IsJoinMechanismTag concept ─────────────────────────────────────
//
// Gate template parameters of downstream consumers (V-204
// mint_spawn variant, `grant::detach_with` admission, etc.).
//
// Two-layer gate:
//   (a) Structural — `T::mechanism` exists with the right type.
//   (b) Identity   — `T` is one of the six declared tag types.
//
// (b) is the load-bearing identity allowlist: an imposter struct
// `struct Mine { static constexpr JoinMechanism mechanism =
//   JoinMechanism::AutoJoin; };` passes (a) but fails (b) — it is
// NOT one of the six declared tag identities, so a consumer cannot
// substitute its own private struct for AutoJoin.  This closes the
// "anyone can mint a join-policy tag" hole.
template <typename T>
concept IsJoinMechanismTag =
    std::is_same_v<T, AutoJoin>
    || std::is_same_v<T, ManualJoin>
    || std::is_same_v<T, Detached>
    || std::is_same_v<T, Cloned>
    || std::is_same_v<T, Forked>
    || std::is_same_v<T, PosixSpawn>;

// Reverse lookup metafunction — extract the enum value at compile time.
template <typename T>
    requires IsJoinMechanismTag<T>
inline constexpr JoinMechanism mechanism_of_v = T::mechanism;

// ── Default mechanism ──────────────────────────────────────────────
//
// `Default` is `AutoJoin` — the structured-concurrency-safe choice
// that V-204 mint_spawn picks if the caller does not override.
// Production code SHOULD always go through `Default` (or `AutoJoin`
// explicitly) at fork-section spawn sites; the looser mechanisms are
// reserved for places where (a) the caller has audited the lifetime
// shape AND (b) ships a `grant::*<Rationale>` opt-in.
using Default = AutoJoin;

// ═══════════════════════════════════════════════════════════════════
// ── Self-test (compile-time) ───────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════
namespace detail::join_policy_self_test {

// Cardinality — six mechanism tags today; future additions MUST land
// at the next free enumerator position (append-only).
static_assert(join_mechanism_count == 6,
    "fixy::spawn::join::JoinMechanism universe drifted from six "
    "{AutoJoin, ManualJoin, Detached, Cloned, Forked, PosixSpawn} — "
    "adding a new mechanism is APPEND-ONLY (next free ordinal) per "
    "the federation-cache stability invariant; update the cardinality "
    "sentinel + the per-tag struct + the name_of switch in lockstep.");

// Reflection-driven name coverage — fires if a future enumerator
// addition lands without a name_of switch arm.
[[nodiscard]] consteval bool every_mechanism_has_name() noexcept {
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^JoinMechanism));
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
    template for (constexpr auto en : enumerators) {
        if (name_of([:en:]) == std::string_view{"<unknown JoinMechanism>"}) {
            return false;
        }
    }
#pragma GCC diagnostic pop
    return true;
}
static_assert(every_mechanism_has_name(),
    "fixy::spawn::join::name_of switch is missing an arm for at "
    "least one JoinMechanism enumerator — add the arm or the new "
    "mechanism leaks the '<unknown JoinMechanism>' sentinel into "
    "runtime observer's debug output.");

// Per-tag mechanism field — reverse-lookup correctness.
static_assert(AutoJoin::mechanism   == JoinMechanism::AutoJoin);
static_assert(ManualJoin::mechanism == JoinMechanism::ManualJoin);
static_assert(Detached::mechanism   == JoinMechanism::Detached);
static_assert(Cloned::mechanism     == JoinMechanism::Cloned);
static_assert(Forked::mechanism     == JoinMechanism::Forked);
static_assert(PosixSpawn::mechanism == JoinMechanism::PosixSpawn);

// mechanism_of_v round-trips.
static_assert(mechanism_of_v<AutoJoin>   == JoinMechanism::AutoJoin);
static_assert(mechanism_of_v<ManualJoin> == JoinMechanism::ManualJoin);
static_assert(mechanism_of_v<Detached>   == JoinMechanism::Detached);
static_assert(mechanism_of_v<Cloned>     == JoinMechanism::Cloned);
static_assert(mechanism_of_v<Forked>     == JoinMechanism::Forked);
static_assert(mechanism_of_v<PosixSpawn> == JoinMechanism::PosixSpawn);

// IsJoinMechanismTag admits exactly the six tags.
static_assert(IsJoinMechanismTag<AutoJoin>);
static_assert(IsJoinMechanismTag<ManualJoin>);
static_assert(IsJoinMechanismTag<Detached>);
static_assert(IsJoinMechanismTag<Cloned>);
static_assert(IsJoinMechanismTag<Forked>);
static_assert(IsJoinMechanismTag<PosixSpawn>);

// Concept rejects non-tag types.
static_assert(!IsJoinMechanismTag<int>);
static_assert(!IsJoinMechanismTag<void>);
static_assert(!IsJoinMechanismTag<JoinMechanism>);  // the enum itself isn't a tag

// EBO-collapsible — each phantom tag is empty (sizeof == 1).
static_assert(std::is_empty_v<AutoJoin>);
static_assert(std::is_empty_v<ManualJoin>);
static_assert(std::is_empty_v<Detached>);
static_assert(std::is_empty_v<Cloned>);
static_assert(std::is_empty_v<Forked>);
static_assert(std::is_empty_v<PosixSpawn>);
static_assert(sizeof(AutoJoin)   == 1);
static_assert(sizeof(ManualJoin) == 1);
static_assert(sizeof(Detached)   == 1);
static_assert(sizeof(Cloned)     == 1);
static_assert(sizeof(Forked)     == 1);
static_assert(sizeof(PosixSpawn) == 1);

// Tags are distinct types — caller cannot substitute one for another.
static_assert(!std::is_same_v<AutoJoin,   ManualJoin>);
static_assert(!std::is_same_v<AutoJoin,   Detached>);
static_assert(!std::is_same_v<ManualJoin, Detached>);
static_assert(!std::is_same_v<Cloned,     Forked>);
static_assert(!std::is_same_v<Forked,     PosixSpawn>);
static_assert(!std::is_same_v<Detached,   PosixSpawn>);

// `final` prevents inheritance-laundering an imposter through a
// derived class that satisfies T::mechanism but fails the identity
// allowlist.  (Compile-time: std::is_final witnesses.)
static_assert(std::is_final_v<AutoJoin>);
static_assert(std::is_final_v<ManualJoin>);
static_assert(std::is_final_v<Detached>);
static_assert(std::is_final_v<Cloned>);
static_assert(std::is_final_v<Forked>);
static_assert(std::is_final_v<PosixSpawn>);

// `Default` resolves to AutoJoin — the structured-concurrency-safe
// choice mint_spawn picks if the caller does not override.
static_assert(std::is_same_v<Default, AutoJoin>);

// Diagnostic names — full per-mechanism coverage.
static_assert(name_of(JoinMechanism::AutoJoin)   == "AutoJoin");
static_assert(name_of(JoinMechanism::ManualJoin) == "ManualJoin");
static_assert(name_of(JoinMechanism::Detached)   == "Detached");
static_assert(name_of(JoinMechanism::Cloned)     == "Cloned");
static_assert(name_of(JoinMechanism::Forked)     == "Forked");
static_assert(name_of(JoinMechanism::PosixSpawn) == "PosixSpawn");

}  // namespace detail::join_policy_self_test

}  // namespace crucible::fixy::spawn::join
