#pragma once

#include <crucible/safety/Path.h>
#include <crucible/safety/source/Path.h>
#include <crucible/safety/Tagged.h>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace crucible::safety::sanitize::path_traversal {

struct no_dotdot {};
struct absolute_root_locked {};

[[nodiscard]] inline std::expected<void, PathTraversalError>
check_no_dotdot(std::filesystem::path const& candidate) noexcept {
    // On POSIX, string() returns the native bytes unchanged. On a platform
    // whose native encoding is wide, it would transcode and could lose bytes.
    const std::string s = candidate.string();

    if (s.empty()) {
        return std::unexpected(PathTraversalError::Empty);
    }
    if (s.size() > MAX_PATH_BYTES) {
        return std::unexpected(PathTraversalError::TooLong);
    }
    // POSIX path arguments end at the first NUL, so an embedded NUL makes the
    // kernel see a shorter path than the one that was checked.
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

// Containment is decided on the normalized byte strings alone. Resolving
// symlinks here is rejected for three reasons. The kernel already refuses to
// follow them at open time, so repeating the work only opens a window in which
// a link can change between the check and the open. A predicate that consults
// the live filesystem gives different answers for the same input. And the
// filesystem-consulting normalizers throw, which this predicate cannot.
[[nodiscard]] inline std::expected<void, PathTraversalError>
check_absolute_root_locked(std::filesystem::path const& candidate, std::filesystem::path const& root_anchor) noexcept {
    const auto cand_norm = candidate.lexically_normal();
    const auto root_norm = root_anchor.lexically_normal();

    if (!root_norm.is_absolute()) {
        return std::unexpected(PathTraversalError::AnchorNotAbsolute);
    }
    // A relative candidate is rejected rather than resolved: resolving it would
    // mean reading the working directory, which this predicate does not do.
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
    requires RetagAllowed<From, source::Sanitized>
[[nodiscard]] inline std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path_no_dotdot(Path<From>&& tainted) noexcept {
    auto check = check_no_dotdot(tainted.value());
    if (!check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<source::Sanitized>();
}

template <typename From>
    requires RetagAllowed<From, source::Sanitized>
[[nodiscard]] inline std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path_root_locked(Path<From>&& tainted, std::filesystem::path const& root_anchor) noexcept {
    // The dot-dot check runs first. The anchor walk compares components without
    // resolving any, so it is only well founded once the candidate is known to
    // carry no `..`.
    if (auto check = check_no_dotdot(tainted.value()); !check) {
        return std::unexpected(check.error());
    }
    if (auto check = check_absolute_root_locked(tainted.value(), root_anchor); !check) {
        return std::unexpected(check.error());
    }
    return std::move(tainted).template retag<source::Sanitized>();
}

}  // namespace crucible::safety::sanitize::path_traversal

namespace crucible::safety {

[[nodiscard]] inline std::expected<Path<source::Sanitized>, PathTraversalError>
sanitize_path(Path<source::External>&& external_path) noexcept {
    return sanitize::path_traversal::sanitize_path_no_dotdot<source::External>(std::move(external_path));
}

}  // namespace crucible::safety

namespace crucible::safety::sanitize::path_traversal::detail::v233_self_test {

static_assert(!std::is_same_v<no_dotdot, absolute_root_locked>,
              "no_dotdot and absolute_root_locked must be distinct policy-marker types: separate "
              "sanitize policies need separate dispatch.");

static_assert(RetagAllowed<source::External, source::Sanitized>,
              "source::External must launder into source::Sanitized through a sanitize "
              "entry-point.");
static_assert(RetagAllowed<source::FromUser, source::Sanitized>,
              "source::FromUser must launder into source::Sanitized.");
static_assert(RetagAllowed<source::FromUserPath, source::Sanitized>,
              "source::FromUserPath must launder into source::Sanitized through a sanitize "
              "entry-point.");
static_assert(RetagAllowed<source::FromEnvPath, source::Sanitized>,
              "source::FromEnvPath must launder into source::Sanitized through a sanitize "
              "entry-point.");
static_assert(RetagAllowed<source::FromConfigPath, source::Sanitized>,
              "source::FromConfigPath must launder into source::Sanitized through a sanitize "
              "entry-point.");

static_assert(RetagAllowed<source::Sanitized, source::Sanitized>,
              "Identity retag from source::Sanitized to source::Sanitized must stay admitted, so "
              "re-sanitizing an already-sanitized path still compiles.");

static_assert(!RetagAllowed<crucible::safety::detail::retag_policy_test::NeverFrom, source::Sanitized>,
              "The reserved never-admitted source tag must stay rejected into source::Sanitized. "
              "Admitting it would defeat the fail-closed retag default.");

}  // namespace crucible::safety::sanitize::path_traversal::detail::v233_self_test
