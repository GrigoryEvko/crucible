// safety/Path.h — Tagged<std::filesystem::path, Source> typed-path wrapper
// plus `sanitize_path()` trust-boundary promoter.  FIXY-V-031.
//
// ── Why ────────────────────────────────────────────────────────────
//
// Every Crucible component that takes a filesystem root from the
// operator (env var, CLI flag, config file) sits on a real trust
// boundary: the bytes were authored outside Crucible's address space.
// Today Cipher's constructor accepts `const std::string& root` — no
// provenance taint in the type system.  V-031 wires the caller to
// declare the External provenance explicitly:
//
//     auto root = safety::Path<safety::source::External>{user_string};
//     auto cipher = Cipher::open(std::move(root));
//
// Cipher's constructor body then runs `sanitize_path(std::move(root))`
// which returns either `Path<source::Sanitized>` (downstream path is
// guaranteed to not contain `..`, NUL, or be empty / too long) or a
// `PathTraversalError`.  Sanitized paths flow forward into V-030's
// dirfd-anchored ::openat() helpers.
//
// ── What sanitize_path rejects ─────────────────────────────────────
//
//   1. Empty path        — Cipher needs a directory anchor to mint
//                          root_dirfd_; the empty path is meaningless.
//   2. Path > MAX_ROOT_PATH_BYTES (16 KB) — bounded to keep
//                          downstream string handling predictable.
//   3. Embedded NUL byte  — std::string can store an embedded NUL but
//                          POSIX path APIs treat NUL as terminator.
//                          A path "x\0/etc/passwd" would silently
//                          truncate at the open() call.
//   4. Any component == ".." — classical path-traversal vector.
//                          Even an absolute path with embedded `..`
//                          can escape an intended sandbox (e.g.
//                          "/var/cipher/../etc/passwd").
//
// ── What sanitize_path does NOT enforce ────────────────────────────
//
//   * Leading `/`        — absolute paths are LEGITIMATE.  Operators
//                          deliberately choose absolute Cipher roots
//                          (`/var/cipher`, `/mnt/data/cipher`).  The
//                          security boundary is V-030's O_NOFOLLOW
//                          + O_DIRECTORY dirfd discipline AFTER the
//                          path is opened, not the path string itself.
//   * Symlink traversal  — defeated downstream by ::open(... O_NOFOLLOW)
//                          and ::openat(root_dirfd_, ... O_NOFOLLOW)
//                          in V-030's helpers.  A symlinked root
//                          fails with ELOOP at open() time.
//   * Path must exist     — sanitize_path is a string-level predicate;
//                          existence is a runtime concern downstream.
//   * Canonical absolute form — sanitize_path doesn't realpath().
//                          Doing so would dereference symlinks which
//                          we deliberately don't want pre-validation.
//
// ── Authorial intent ───────────────────────────────────────────────
//
// V-031 is the *substrate* scaffold for FIXY-V-232 (safety/source/Path.h
// — FromUserPath / FromEnvPath / FromConfigPath fine-grained taint
// tags) and FIXY-V-233 (safety/sanitize/PathTraversal.h — full
// no_dotdot + absolute_root_locked predicate catalog).  The minimal
// surface today is one wrapper alias + one promoter free function.
//
// ── Axioms ─────────────────────────────────────────────────────────
//
//   InitSafe   — Path<S> wraps std::filesystem::path; sanitize_path
//                returns std::expected — explicit value-or-error,
//                no uninitialized output channel.
//   TypeSafe   — Path<source::External> and Path<source::Sanitized>
//                are DISTINCT types; passing External where Sanitized
//                is expected is a compile error.
//   NullSafe   — std::filesystem::path has a well-defined empty()
//                state; we reject it explicitly.
//   MemSafe    — std::filesystem::path owns its bytes; Tagged wraps
//                by value; sanitize_path consumes by rvalue.
//   BorrowSafe — sanitize_path's signature takes Path<External>&&;
//                caller cannot use it after sanitize_path returns
//                (moved-from).
//   ThreadSafe — sanitize_path is pure / stateless; no shared state.
//   LeakSafe   — std::filesystem::path destructor frees its bytes.
//   DetSafe    — sanitize_path's logic is byte-deterministic; same
//                input → same output / same error on any platform.

#pragma once

#include <crucible/safety/Tagged.h>           // Tagged + source::External/Sanitized
#include <crucible/safety/source/Path.h>      // FIXY-V-232: FromUserPath / FromEnvPath / FromConfigPath

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>
#include <utility>

namespace crucible::safety {

// ── Path<Source> — Tagged<std::filesystem::path, Source> alias ─────
//
// One-axis-of-provenance typed-path wrapper.  V-232 will introduce
// finer-grained taint tags (FromUserPath, FromEnvPath, FromConfigPath)
// that live alongside source::External as orthogonal narrowings — the
// alias template here is the substrate they'll all flow through.
template <typename Source>
using Path = Tagged<std::filesystem::path, Source>;

// ── PathTraversal — sanitize policy tag ────────────────────────────
//
// Empty marker type identifying the sanitize policy applied.  V-233
// will populate the `sanitize/` directory with one tag per policy;
// PathTraversal is the inaugural entry, covering the four rejection
// rules documented above.
struct PathTraversal {};

// ── PathTraversalError — diagnostic enum ───────────────────────────
//
// Returned in the std::expected error channel.  Each enumerator
// corresponds 1:1 to one of the four rejection rules; the consumer
// can switch on it for logging or fall through to a generic
// `path rejected by sanitize_path` diagnostic.
enum class PathTraversalError : std::uint8_t {
    Empty                = 0,  // Path is empty
    TooLong              = 1,  // Path exceeds MAX_PATH_BYTES
    EmbeddedNul          = 2,  // Path contains embedded NUL byte
    DotDotComponent      = 3,  // Path contains a `..` component
    // ── V-233 extensions — `absolute_root_locked` predicate ────────
    CandidateNotAbsolute = 4,  // Candidate path is not absolute
    AnchorNotAbsolute    = 5,  // Root anchor itself is not absolute
    EscapesAnchor        = 6,  // Candidate lies outside the root anchor
};

// ── MAX_PATH_BYTES ────────────────────────────────────────────────
//
// 16 KiB cap.  POSIX PATH_MAX is typically 4096; 16 KiB gives 4×
// headroom while staying well below int32 overflow concerns.  Cipher
// holds the same cap as MAX_ROOT_PATH_BYTES internally.
inline constexpr std::size_t MAX_PATH_BYTES = 16 * 1024;

// ── sanitize_path() — V-031 entry-point ────────────────────────────
//
// The function body has moved to safety/sanitize/PathTraversal.h
// (FIXY-V-233 audit follow-up).  It now delegates to V-233's
// `check_no_dotdot` predicate so the four-rule no-dot-dot algorithm
// lives in exactly one place — the canonical V-233 predicate — and
// the V-031 entry-point becomes a thin back-compat wrapper around
// `sanitize::path_traversal::sanitize_path_no_dotdot<source::External>`.
//
// Including PathTraversal.h at the bottom of this header (after
// Path<>, PathTraversalError, and MAX_PATH_BYTES are declared) closes
// the back-edge of the cycle: PathTraversal.h needs Path<> visible
// before it can define sanitize_path_no_dotdot; Path.h re-exports
// sanitize_path so existing V-031 callers (Cipher::open(), Vigil)
// don't have to change their includes.

}  // namespace crucible::safety

// V-233 integration: bring `crucible::safety::sanitize_path` and
// the `crucible::safety::sanitize::path_traversal::*` predicates
// into scope for every translation unit that includes safety/Path.h.
// The include lives at the bottom — after the Path<> alias, the
// PathTraversalError enum, and MAX_PATH_BYTES are all declared —
// so PathTraversal.h's body sees the symbols it needs when it
// pulls safety/Path.h via pragma-once short-circuit.
#include <crucible/safety/sanitize/PathTraversal.h>
