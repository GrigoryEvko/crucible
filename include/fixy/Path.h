#pragma once

// A filesystem path that carries its provenance, and the two predicates
// that advance that provenance to Sanitized.
//
// A path is a Tagged over std::filesystem::path.  The tag is the whole
// mechanism: a helper that opens a file demands Path<Sanitized>, and
// the only way to hold one is to have run a predicate below, because
// the retag catalog admits the edge and the value constructor of
// Tagged is private.
//
// Old spellings: include/crucible/safety/Path.h and
// include/crucible/safety/sanitize/PathTraversal.h.  The two are one
// header here.  The old pair included each other — Path.h named the
// sanitizer at its foot, and the sanitizer named Path.h at its head —
// and the old header comment apologized for the ordering that made
// that work.  The sanitize directory held that one file.  Declaration
// order inside a single header carries the same dependency without the
// cycle.

#include <fixy/Tagged.h>
#include <fixy/Tags.h>
#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>

namespace fixy {

template <typename Source>
using Path = Tagged<std::filesystem::path, Source>;

struct PathTraversal {};

// An embedded NUL is rejected because POSIX path calls stop at it, so a
// path built from a string that holds one silently opens a shorter
// path.  A `..` component is rejected because it escapes an intended
// root even when the path is absolute.
//
// A leading `/` is not rejected: an absolute root is a legitimate
// choice.  Symlinks are not resolved either, and a check here could not
// settle them anyway — the guarantee comes from opening the root with
// O_NOFOLLOW and O_DIRECTORY and anchoring later opens to that
// descriptor.  Nothing here touches the filesystem, so a path that
// passes need not exist.
enum class PathTraversalError : std::uint8_t {
    Empty = 0,
    TooLong = 1,
    EmbeddedNul = 2,
    DotDotComponent = 3,
    CandidateNotAbsolute = 4,
    AnchorNotAbsolute = 5,
    EscapesAnchor = 6,
};

// POSIX PATH_MAX is typically 4096.  This leaves headroom over that
// while staying far below the width of the arithmetic that carries it.
inline constexpr std::size_t MAX_PATH_BYTES = 16 * 1024;

}  // namespace fixy

namespace fixy::sanitize::path_traversal {

struct no_dotdot {};
struct absolute_root_locked {};

[[nodiscard]] inline std::expected<void, PathTraversalError>
check_no_dotdot(std::filesystem::path const& candidate) noexcept {
    // On POSIX, string() returns the native bytes unchanged. On a
    // platform whose native encoding is wide, it would transcode and
    // could lose bytes.
    const std::string s = candidate.string();

    if (s.empty()) {
        return std::unexpected(PathTraversalError::Empty);
    }
    if (s.size() > MAX_PATH_BYTES) {
        return std::unexpected(PathTraversalError::TooLong);
    }
    // POSIX path arguments end at the first NUL, so an embedded NUL
    // makes the kernel see a shorter path than the one that was
    // checked.
    if (s.find('\0') != std::string::npos) {
        return std::unexpected(PathTraversalError::EmbeddedNul);
    }
    for (const auto& component : candidate) {
        if (component == "..") {
            return std::unexpected(PathTraversalError::DotDotComponent);
        }
    }
    return {};
}

// Containment is decided on the normalized byte strings alone.
// Resolving symlinks here is rejected for three reasons. The kernel
// already refuses to follow them at open time, so repeating the work
// only opens a window in which a link can change between the check and
// the open. A predicate that consults the live filesystem gives
// different answers for the same input. And the filesystem-consulting
// normalizers throw, which this predicate cannot.
[[nodiscard]] inline std::expected<void, PathTraversalError>
check_absolute_root_locked(std::filesystem::path const& candidate, std::filesystem::path const& root_anchor) noexcept {
    const auto cand_norm = candidate.lexically_normal();
    const auto root_norm = root_anchor.lexically_normal();

    if (!root_norm.is_absolute()) {
        return std::unexpected(PathTraversalError::AnchorNotAbsolute);
    }
    // A relative candidate is rejected rather than resolved: resolving
    // it would mean reading the working directory, which this predicate
    // does not do.
    if (!cand_norm.is_absolute()) {
        return std::unexpected(PathTraversalError::CandidateNotAbsolute);
    }

    auto root_it = root_norm.begin();
    const auto root_end = root_norm.end();
    auto cand_it = cand_norm.begin();
    const auto cand_end = cand_norm.end();
    for (; root_it != root_end; ++root_it, ++cand_it) {
        if (cand_it == cand_end) {
            return std::unexpected(PathTraversalError::EscapesAnchor);
        }
        if (*root_it != *cand_it) {
            return std::unexpected(PathTraversalError::EscapesAnchor);
        }
    }
    return {};
}

template <typename From>
    requires RetagAllowed<From, tags::source::Sanitized>
[[nodiscard]] inline std::expected<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path_no_dotdot(Path<From>&& tainted) noexcept {
    auto check = check_no_dotdot(tainted.value());
    if (!check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<tags::source::Sanitized>();
}

template <typename From>
    requires RetagAllowed<From, tags::source::Sanitized>
[[nodiscard]] inline std::expected<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path_root_locked(Path<From>&& tainted, std::filesystem::path const& root_anchor) noexcept {
    // The dot-dot check runs first. The anchor walk compares components
    // without resolving any, so it is only well founded once the
    // candidate is known to carry no `..`.
    if (auto check = check_no_dotdot(tainted.value()); !check) {
        return std::unexpected(check.error());
    }
    if (auto check = check_absolute_root_locked(tainted.value(), root_anchor); !check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<tags::source::Sanitized>();
}

}  // namespace fixy::sanitize::path_traversal

namespace fixy {

[[nodiscard]] inline std::expected<Path<tags::source::Sanitized>, PathTraversalError>
sanitize_path(Path<tags::source::External>&& external_path) noexcept {
    return sanitize::path_traversal::sanitize_path_no_dotdot<tags::source::External>(std::move(external_path));
}

}  // namespace fixy

namespace fixy::detail::path_self_test {

static_assert(sizeof(Path<tags::source::External>) == sizeof(std::filesystem::path));

static_assert(!std::is_same_v<sanitize::path_traversal::no_dotdot, sanitize::path_traversal::absolute_root_locked>,
              "no_dotdot and absolute_root_locked must be distinct policy-marker types: separate "
              "sanitize policies need separate dispatch.");

static_assert(RetagAllowed<tags::source::External, tags::source::Sanitized>,
              "tags::source::External must launder into tags::source::Sanitized through a sanitize "
              "entry-point.");
static_assert(RetagAllowed<tags::source::FromUser, tags::source::Sanitized>,
              "tags::source::FromUser must launder into tags::source::Sanitized.");
static_assert(RetagAllowed<tags::source::FromUserPath, tags::source::Sanitized>,
              "tags::source::FromUserPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");
static_assert(RetagAllowed<tags::source::FromEnvPath, tags::source::Sanitized>,
              "tags::source::FromEnvPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");
static_assert(RetagAllowed<tags::source::FromConfigPath, tags::source::Sanitized>,
              "tags::source::FromConfigPath must launder into tags::source::Sanitized through a "
              "sanitize entry-point.");

static_assert(RetagAllowed<tags::source::Sanitized, tags::source::Sanitized>,
              "Identity retag from tags::source::Sanitized to tags::source::Sanitized must stay "
              "admitted, so re-sanitizing an already-sanitized path still compiles.");

static_assert(!RetagAllowed<::fixy::detail::retag_policy_test::NeverFrom, tags::source::Sanitized>,
              "The reserved never-admitted source tag must stay rejected into "
              "tags::source::Sanitized. Admitting it would defeat the fail-closed retag default.");

// CipherPath has no edge into Sanitized on purpose.  Its bytes never
// crossed an untrusted boundary, and keeping it out of the three
// external lanes is what stops operator-supplied bytes from reaching
// the directory-anchored open helpers.
template <typename From>
concept CanSanitize = requires(Path<From> p) { sanitize::path_traversal::sanitize_path_no_dotdot(std::move(p)); };
static_assert(CanSanitize<tags::source::External>);
static_assert(CanSanitize<tags::source::FromUserPath>);
static_assert(!CanSanitize<::fixy::detail::retag_policy_test::NeverFrom>);

// The header is made of templates and inline functions, so a
// static_assert inside it proves nothing about a body that no
// translation unit instantiates.  This walks the rules the predicates
// carry, and a sentinel translation unit calls it.
inline void runtime_smoke_test() {
    namespace pt = sanitize::path_traversal;
    using fs_path = std::filesystem::path;

    if (!pt::check_no_dotdot(fs_path{"/var/cipher/objects"})) std::abort();
    if (pt::check_no_dotdot(fs_path{""}).error() != PathTraversalError::Empty) std::abort();
    if (pt::check_no_dotdot(fs_path{"/var/../etc/passwd"}).error() != PathTraversalError::DotDotComponent) std::abort();

    if (pt::check_absolute_root_locked(fs_path{"/var/cipher"}, fs_path{"/home/user"}).error()
        != PathTraversalError::EscapesAnchor)
        std::abort();
    if (!pt::check_absolute_root_locked(fs_path{"/var/cipher/objects"}, fs_path{"/var/cipher"})) std::abort();

    auto tainted = mint_tagged<tags::source::External>(fs_path{"/var/cipher/objects"});
    auto clean = sanitize_path(std::move(tainted));
    if (!clean.has_value()) std::abort();
    if (clean->value() != fs_path{"/var/cipher/objects"}) std::abort();

    auto escaping = mint_tagged<tags::source::External>(fs_path{"../etc/passwd"});
    auto refused = sanitize_path(std::move(escaping));
    if (refused.has_value()) std::abort();
    if (refused.error() != PathTraversalError::DotDotComponent) std::abort();
}

}  // namespace fixy::detail::path_self_test
