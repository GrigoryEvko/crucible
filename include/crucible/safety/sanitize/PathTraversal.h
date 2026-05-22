// safety/sanitize/PathTraversal.h — path-traversal sanitize predicates.
//
// FIXY-V-233 (Agent 9 §4.5).  Lifts V-031's inline no-dot-dot logic
// into a free-standing predicate (`check_no_dotdot`) and adds a
// runtime root-anchor check (`check_absolute_root_locked`).  Together
// they form the canonical RetagWitness for `source::External /
// FromUserPath / FromEnvPath / FromConfigPath → source::Sanitized`
// transitions admitted by the V-023 + V-232 catalog.
//
// ── Why two separate predicates ────────────────────────────────────
//
// Different callers want different sanitize policies:
//
//   * Cipher (compile-time-rooted at the workload's Cipher root)
//     runs `no_dotdot` ONLY.  The root is supplied by the operator
//     once at process start and the dirfd-based ::openat() helpers
//     in V-030 do the actual sandbox-pinning at the kernel level —
//     so the path-string sanitizer does not need to repeat that
//     work at predicate time.
//   * Vessel adapter accepting user-typed paths (e.g. a checkpoint
//     load path supplied via REPL or CLI) runs BOTH: `no_dotdot`
//     first (kills the bulk of attack vectors), then
//     `absolute_root_locked` (anchors the candidate under a
//     declared project root before any disk syscall runs).
//
// Composability is the discipline: each predicate is independently
// usable, side-effect-free, and noexcept; composing them is a
// caller's choice, not the substrate's.
//
// ── Why byte-level (no realpath, no syscalls) ─────────────────────
//
// `check_absolute_root_locked` uses `std::filesystem::path::
// lexically_normal()` for normalization.  This is a STRING-level
// operation (no `realpath`, no `stat`, no symlink traversal); it
// strips `.` components, collapses redundant separators, and
// normalizes the path's internal representation without consulting
// the filesystem.
//
// Real-path / symlink resolution is intentionally NOT done here:
//
//   1. Defense-in-depth.  V-030's dirfd-based ::openat(... O_NOFOLLOW)
//      already defeats symlink traversal at the kernel level.  Doing
//      it again at sanitize time invites TOCTOU races (the symlink
//      could change between sanitize and open).
//   2. DetSafe.  `realpath` consults the live filesystem and
//      changes its answer when symlinks change.  A byte-level
//      sanitize predicate is deterministic across builds, runs,
//      and platforms — properties V-031's substrate doc-block
//      pins (file:80, "byte-deterministic; same input → same output").
//   3. noexcept.  `std::filesystem::weakly_canonical` consults the
//      filesystem and can throw `filesystem_error` on syscall
//      failure.  `lexically_normal` is `noexcept`.
//
// ── What `check_no_dotdot` rejects ─────────────────────────────────
//
//   1. Empty path                — no anchor, meaningless input.
//   2. Path > MAX_PATH_BYTES     — bounded for predictable downstream.
//   3. Embedded NUL byte          — POSIX path APIs would truncate.
//   4. Any component == ".."     — classical path-traversal vector.
//
// These four rules are byte-identical to V-031's inline
// implementation; V-233 lifts them out to share the algorithm.
//
// ── What `check_absolute_root_locked` rejects ──────────────────────
//
//   1. Candidate not absolute    — relative paths cannot anchor.
//   2. Anchor not absolute        — a relative anchor is a programmer
//                                   error; reject loudly.
//   3. Candidate escapes anchor   — after `lexically_normal()`, the
//                                   candidate's component prefix must
//                                   match the anchor's components
//                                   exactly.  Any divergence is an
//                                   escape attempt.
//
// `check_absolute_root_locked` runs AFTER `check_no_dotdot` in the
// composed `sanitize_path_root_locked` entry-point — by then the
// candidate is `..`-free, so the component-walk comparison is
// well-founded (no `..` to resolve).
//
// ── RetagWitness discipline ────────────────────────────────────────
//
// V-022's `RetagAllowed<From, To>` concept gates `Tagged::retag()`
// at compile time.  Each `sanitize_path_*` entry-point here is a
// runtime predicate that DISCHARGES the catalog-admitted
// (External → Sanitized) and (FromUserPath / FromEnvPath /
// FromConfigPath → Sanitized) transitions ON SUCCESSFUL CHECK.
// On predicate failure, no retag happens; the caller gets a
// `PathTraversalError` instead.  The `requires RetagAllowed<From,
// source::Sanitized>` clause on each entry-point ensures only
// catalog-admitted source tags can flow through.
//
// ── Axiom coverage ─────────────────────────────────────────────────
//
//   InitSafe   — All predicates return `std::expected`; no
//                uninitialized output channel.
//   TypeSafe   — `Path<From>` and `Path<Sanitized>` are distinct
//                types.  Returning `Path<Sanitized>` from
//                successful sanitize is the type-level proof.
//   NullSafe   — `std::filesystem::path` has a well-defined empty
//                state; rule 1 rejects it explicitly.
//   MemSafe    — `std::filesystem::path` owns its bytes; `Path<>`
//                wraps by value; predicates inspect by const&.
//   BorrowSafe — Entry-points take rvalue `Path<From>&&`; caller
//                cannot use it after `sanitize_path_*` returns.
//   ThreadSafe — All predicates are pure / stateless; no shared
//                state.
//   LeakSafe   — `std::filesystem::path` and `std::expected` both
//                free their storage on destruction.
//   DetSafe    — All logic is byte-deterministic.  Same input,
//                same output, on any platform, any build.
//
// ── Cost ──────────────────────────────────────────────────────────
//
//   `check_no_dotdot`:           O(path-length).  One `string()`
//                                copy; one `find('\0')`; one
//                                component iteration.
//   `check_absolute_root_locked`: O(path-length + anchor-length).
//                                Two `lexically_normal()` calls
//                                (each O(path-length) string copies);
//                                one component-walk comparison.
//
// Neither predicate is on the hot path — both run once at the
// trust-boundary crossing.  Cost is bounded by MAX_PATH_BYTES (16
// KiB); a worst-case sanitize costs ~tens of microseconds — orders
// of magnitude below `::open()` syscall latency that follows.

#pragma once

#include <crucible/safety/Path.h>            // Path<Source>, PathTraversalError, MAX_PATH_BYTES
#include <crucible/safety/source/Path.h>     // V-232: FromUserPath / FromEnvPath / FromConfigPath
#include <crucible/safety/Tagged.h>          // RetagAllowed concept

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace crucible::safety::sanitize::path_traversal {

// ── Policy tags — pick-a-predicate marker types ───────────────────
//
// Marker types let `sanitize_path<Policy>(...)` overloads dispatch
// without ADL surprises.  The `PathTraversal` tag from V-031 is the
// inaugural family member; V-233's tags refine it.
struct no_dotdot              {};
struct absolute_root_locked   {};

// ── check_no_dotdot(path) ──────────────────────────────────────────
//
// Byte-level predicate: applies V-031's four rejection rules to a
// `std::filesystem::path` const&.  Returns `std::expected<void, …>`
// — caller threads the error into its retag/return path.  Side-
// effect-free; safe to call from any thread.
[[nodiscard]] inline std::expected<void, PathTraversalError>
check_no_dotdot(std::filesystem::path const& candidate) noexcept {
    // Read the native-encoding bytes once.  On POSIX `string()` and
    // `native()` agree; Crucible is Linux-only per CLAUDE.md §XIV.
    const std::string s = candidate.string();

    // Rule 1: empty path.
    if (s.empty()) {
        return std::unexpected(PathTraversalError::Empty);
    }
    // Rule 2: oversize path.
    if (s.size() > MAX_PATH_BYTES) {
        return std::unexpected(PathTraversalError::TooLong);
    }
    // Rule 3: embedded NUL.  POSIX `::open()` would silently truncate.
    if (s.find('\0') != std::string::npos) {
        return std::unexpected(PathTraversalError::EmbeddedNul);
    }
    // Rule 4: any `..` component.  Classical traversal vector.
    for (const auto& component : candidate) {
        if (component == "..") {
            return std::unexpected(PathTraversalError::DotDotComponent);
        }
    }
    return {};
}

// ── check_absolute_root_locked(candidate, anchor) ──────────────────
//
// Byte-level predicate: candidate's lexically-normalized component
// prefix must match the anchor's lexically-normalized components
// exactly.  No filesystem access (no realpath / no symlink resolve).
//
// Pre-conditions enforced as errors (caller's mistake, not user's):
//
//   * Anchor MUST be absolute.  If the anchor itself is relative,
//     the predicate cannot decide containment — programmer error,
//     return AnchorNotAbsolute.
//   * Candidate MUST be absolute.  A relative candidate cannot
//     anchor at any absolute root; return CandidateNotAbsolute.
//
// On equality / proper-prefix match: success.  On any divergence:
// EscapesAnchor.
[[nodiscard]] inline std::expected<void, PathTraversalError>
check_absolute_root_locked(std::filesystem::path const& candidate,
                           std::filesystem::path const& root_anchor) noexcept {
    // Lexically normalize both — strip `.` components and redundant
    // separators.  STRING-level only; no filesystem syscall.
    const auto cand_norm = candidate.lexically_normal();
    const auto root_norm = root_anchor.lexically_normal();

    // Anchor sanity — the predicate is meaningless against a
    // relative anchor.
    if (!root_norm.is_absolute()) {
        return std::unexpected(PathTraversalError::AnchorNotAbsolute);
    }
    // Candidate must be absolute — a relative candidate cannot be
    // pinned to an absolute root without first resolving the
    // candidate's CWD, which would consult the filesystem.
    if (!cand_norm.is_absolute()) {
        return std::unexpected(PathTraversalError::CandidateNotAbsolute);
    }

    // Walk components: every component of root_norm must match the
    // corresponding initial component of cand_norm.  After the walk,
    // cand_norm may extend beyond root_norm (root + subpath) or
    // equal root_norm exactly.
    auto root_it = root_norm.begin();
    const auto root_end = root_norm.end();
    auto cand_it = cand_norm.begin();
    const auto cand_end = cand_norm.end();
    for (; root_it != root_end; ++root_it, ++cand_it) {
        if (cand_it == cand_end) {
            // candidate is shorter than anchor — clearly not under it.
            return std::unexpected(PathTraversalError::EscapesAnchor);
        }
        if (*root_it != *cand_it) {
            return std::unexpected(PathTraversalError::EscapesAnchor);
        }
    }
    return {};
}

// ── sanitize_path_no_dotdot<From>(Path<From>&&) ────────────────────
//
// Compositional entry-point: runs `check_no_dotdot` and, on
// success, RETAGS the path's provenance to `source::Sanitized` via
// V-022's catalog-admitted transition.  On failure: returns
// `unexpected(PathTraversalError)` and the input is consumed
// (rvalue contract).
//
// The `requires RetagAllowed<From, source::Sanitized>` clause is
// the load-bearing soundness gate: only V-023 + V-232 catalog-
// admitted source tags can flow through.  Untagged or
// not-yet-admitted source tags get a compile-time diagnostic
// pointing at the catalog rather than a silent runtime sanitize.
template <typename From>
    requires RetagAllowed<From, source::Sanitized>
[[nodiscard]] inline
std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path_no_dotdot(Path<From>&& tainted) noexcept {
    auto check = check_no_dotdot(tainted.value());
    if (!check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<source::Sanitized>();
}

// ── sanitize_path_root_locked<From>(Path<From>&&, anchor) ──────────
//
// Compositional entry-point: runs `check_no_dotdot` first (kills
// the bulk of attack vectors), then `check_absolute_root_locked`
// (anchors the candidate at the supplied root).  On success: retag
// to `source::Sanitized`.  On any failure: `unexpected` with the
// first-failing predicate's error code.
//
// Same `RetagAllowed` gate as `sanitize_path_no_dotdot` — only
// catalog-admitted source tags can flow through.
template <typename From>
    requires RetagAllowed<From, source::Sanitized>
[[nodiscard]] inline
std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path_root_locked(Path<From>&& tainted,
                          std::filesystem::path const& root_anchor) noexcept {
    // No-dot-dot first — cheapest predicate; rules out most attack
    // vectors before the (slightly more expensive) anchor walk.
    if (auto check = check_no_dotdot(tainted.value()); !check) {
        return std::unexpected(check.error());
    }
    if (auto check = check_absolute_root_locked(tainted.value(), root_anchor);
        !check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<source::Sanitized>();
}

}  // namespace crucible::safety::sanitize::path_traversal

// ── V-031 back-compat entry-point — delegates to V-233 predicate ───
//
// FIXY-V-233 audit follow-up: V-031's `sanitize_path(Path<External>&&)`
// previously inlined a copy of the four-rule no-dot-dot algorithm
// directly in safety/Path.h.  V-233 lifted that algorithm into the
// `check_no_dotdot` predicate, so the V-031 entry-point reduces to a
// one-line delegation through `sanitize_path_no_dotdot<source::External>`.
//
// Why keep the V-031 name?  Two reasons:
//
//   1. Stable surface for V-031's documented callers.  Cipher::open()
//      and Vigil reach for `safety::sanitize_path()` per the V-031
//      contract; renaming would force callsite churn for zero gain.
//   2. Semantic continuity.  The V-031 entry-point and V-233's
//      `sanitize_path_no_dotdot` ARE the same operation today; the
//      back-compat wrapper documents the policy choice — V-031's
//      External admittance composes exactly with V-233's no_dotdot
//      policy.
//
// Future V-2xx tasks can deprecate this wrapper once every V-031
// caller migrates to the policy-explicit form; until then the wrapper
// keeps the migration cost at zero.
//
// `[[nodiscard]]` because dropping the result silently discards both
// the sanitized path AND the error channel.
//
// `noexcept` because every step in the delegation chain
// (`check_no_dotdot`, `retag<source::Sanitized>`) is noexcept.
namespace crucible::safety {

[[nodiscard]] inline std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path(Path<source::External>&& external_path) noexcept {
    return sanitize::path_traversal::sanitize_path_no_dotdot<source::External>(
        std::move(external_path));
}

}  // namespace crucible::safety

// ── V-233 self-test — pin the predicate catalog at sentinel-TU ─────
//
// Static_asserts that the policy tags are distinct types and that
// the `RetagAllowed` gate accepts every V-023 + V-232 catalog-
// admitted source tag.  If any of these flip, the build breaks
// HERE rather than at consumer sites.

namespace crucible::safety::sanitize::path_traversal::detail::v233_self_test {

// ── (1) Policy tags are distinct types ─────────────────────────────
static_assert(!std::is_same_v<no_dotdot, absolute_root_locked>,
    "FIXY-V-233: no_dotdot and absolute_root_locked must be distinct "
    "policy-marker types — different sanitize policies, different "
    "dispatch.");

// ── (2) Catalog-admitted source tags flow through sanitize_path_* ──
//
// Verify the requires-clause accepts every source tag in the V-023
// + V-232 catalog that admits → Sanitized.  Use the runtime-check
// signature via decltype to avoid actually instantiating the body.
static_assert(RetagAllowed<source::External,       source::Sanitized>,
    "FIXY-V-233: source::External must launder to source::Sanitized "
    "through sanitize_path_* (V-023 catalog admittance).");
static_assert(RetagAllowed<source::FromUser,       source::Sanitized>,
    "FIXY-V-233: source::FromUser must launder to source::Sanitized "
    "(V-023 catalog admittance).");
static_assert(RetagAllowed<source::FromUserPath,   source::Sanitized>,
    "FIXY-V-233: V-232 source::FromUserPath must launder to "
    "source::Sanitized via sanitize_path_*.");
static_assert(RetagAllowed<source::FromEnvPath,    source::Sanitized>,
    "FIXY-V-233: V-232 source::FromEnvPath must launder to "
    "source::Sanitized via sanitize_path_*.");
static_assert(RetagAllowed<source::FromConfigPath, source::Sanitized>,
    "FIXY-V-233: V-232 source::FromConfigPath must launder to "
    "source::Sanitized via sanitize_path_*.");

// ── (3) Identity admitted (V-022 identity rule) ─────────────────────
//
// Sanitized → Sanitized identity is admitted by V-022 — pin that
// re-sanitizing an already-Sanitized path compiles (no constraint
// failure) even though it would be a no-op.  This catches a
// regression where the catalog accidentally rejects identity.
static_assert(RetagAllowed<source::Sanitized, source::Sanitized>,
    "FIXY-V-233: source::Sanitized → source::Sanitized identity must "
    "remain admitted by V-022's identity rule.");

// ── (4) Untagged-source rejection ──────────────────────────────────
//
// Sentinel-pair witness: V-022's reserved-forever-unspecialized
// pair must STAY rejected through the V-233 sanitize_path_*
// requires-clause; admitting it would defeat the V-022 fail-closed
// contract.
static_assert(!RetagAllowed<crucible::safety::detail::retag_policy_test::NeverFrom,
                            source::Sanitized>,
    "FIXY-V-233: V-022 sentinel NeverFrom → source::Sanitized MUST "
    "stay rejected; admittance would defeat fail-closed default.");

}  // namespace crucible::safety::sanitize::path_traversal::detail::v233_self_test
