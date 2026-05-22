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
    Empty            = 0,  // Path is empty
    TooLong          = 1,  // Path exceeds MAX_PATH_BYTES
    EmbeddedNul      = 2,  // Path contains embedded NUL byte
    DotDotComponent  = 3,  // Path contains a `..` component
};

// ── MAX_PATH_BYTES ────────────────────────────────────────────────
//
// 16 KiB cap.  POSIX PATH_MAX is typically 4096; 16 KiB gives 4×
// headroom while staying well below int32 overflow concerns.  Cipher
// holds the same cap as MAX_ROOT_PATH_BYTES internally.
inline constexpr std::size_t MAX_PATH_BYTES = 16 * 1024;

// ── sanitize_path(Path<External>&&) → expected<Path<Sanitized>, E> ─
//
// Consume an External-tagged path, apply the four rejection rules,
// and either:
//   * promote provenance to source::Sanitized (success), OR
//   * return a PathTraversalError tagged enum (failure).
//
// `[[nodiscard]]` because dropping the result silently discards both
// the sanitized path AND the error channel — almost certainly a bug.
//
// `noexcept`: std::filesystem::path's string() does NOT throw on
// well-formed paths; we only inspect the held native-encoding bytes.
// The std::expected ctors are noexcept for trivially-copyable error
// enumerators (PathTraversalError is uint8_t).
[[nodiscard]] inline std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path(Path<source::External>&& external_path) noexcept {
    // Peek through the External wrapper to read the underlying path.
    // The rules below are byte/component-level predicates; they do
    // not need to consume the tagged value.  The retag at the bottom
    // is the load-bearing consume.
    const std::filesystem::path& raw = external_path.value();

    // Inspect the native-encoding bytes directly.  We use string()
    // (not native()) to stay portable; on POSIX the two are identical,
    // and Crucible is Linux-only per CLAUDE.md §XIV.
    const std::string s = raw.string();

    // Rule 1: empty path.
    if (s.empty()) {
        return std::unexpected(PathTraversalError::Empty);
    }

    // Rule 2: oversize path.  Bounded above to keep downstream
    // buffer math predictable.
    if (s.size() > MAX_PATH_BYTES) {
        return std::unexpected(PathTraversalError::TooLong);
    }

    // Rule 3: embedded NUL.  std::string can hold embedded NUL bytes
    // but every POSIX path API treats NUL as the C-string terminator,
    // so a path like "good\0/etc/passwd" would silently truncate to
    // "good" at ::open() time.  Reject explicitly.
    if (s.find('\0') != std::string::npos) {
        return std::unexpected(PathTraversalError::EmbeddedNul);
    }

    // Rule 4: any path component == "..".  std::filesystem::path's
    // iterator yields components split on "/" (and the platform's
    // preferred separator).  Reject the dot-dot literal regardless
    // of position — even absolute paths with embedded ".." can
    // escape an intended sandbox.
    for (const auto& component : raw) {
        if (component == "..") {
            return std::unexpected(PathTraversalError::DotDotComponent);
        }
    }

    // All four rules passed.  Promote provenance: External → Sanitized
    // is admitted by retag_policy<source::External, source::Sanitized>
    // (safety/Tagged.h:543).  The raw path bytes are NOT mutated;
    // only the phantom Source tag changes (zero-cost retag).
    return std::move(external_path).template retag<source::Sanitized>();
}

}  // namespace crucible::safety
