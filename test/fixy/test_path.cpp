// Sentinel TU for fixy/Path.h: each predicate answers on its success
// path and on every distinct variant of PathTraversalError, so a
// regression in one rule reddens here rather than hiding behind a
// neighbouring rule that happens to reject the same input.
//
// The header's self-test walks the anchors. This TU is the port of the
// old test/test_path_traversal_predicates.cpp, and it adds the tag
// arithmetic the old test could not state: the value constructor of
// Tagged is private since A10.2, so every tainted path here is minted.

#include <fixy/Path.h>

#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace {

namespace pt = ::fixy::sanitize::path_traversal;
namespace fs_ns = std::filesystem;
namespace src = ::fixy::tags::source;

using ::fixy::mint_tagged;
using ::fixy::Path;
using ::fixy::PathTraversalError;

using SanitizedPath = Path<src::Sanitized>;

// The tag is the whole mechanism, so it has to be part of the type.
static_assert(!std::is_same_v<Path<src::External>, SanitizedPath>);
static_assert(sizeof(Path<src::External>) == sizeof(fs_ns::path));

// A path whose tag has no admitted edge into Sanitized cannot reach
// either predicate. CipherPath is deliberately one of those.
template <typename From>
concept CanSanitizeNoDotdot = requires(Path<From> p) { pt::sanitize_path_no_dotdot(std::move(p)); };
template <typename From>
concept CanSanitizeRootLocked =
    requires(Path<From> p, fs_ns::path anchor) { pt::sanitize_path_root_locked(std::move(p), anchor); };
static_assert(CanSanitizeNoDotdot<src::External>);
static_assert(CanSanitizeNoDotdot<src::FromUserPath>);
static_assert(CanSanitizeNoDotdot<src::FromEnvPath>);
static_assert(CanSanitizeNoDotdot<src::FromConfigPath>);
static_assert(CanSanitizeRootLocked<src::External>);
static_assert(!CanSanitizeNoDotdot<src::CipherPath>, "CipherPath has no edge into Sanitized on purpose");
static_assert(!CanSanitizeRootLocked<src::CipherPath>);

// The one-argument entry point takes External and nothing else, which
// is what keeps the other lanes at the two-argument door.
static_assert(!std::is_invocable_v<decltype(&::fixy::sanitize_path), Path<src::FromUserPath>&&>);

void expect_ok(const std::expected<void, PathTraversalError>& r, const char* what) {
    if (!r.has_value()) {
        std::fprintf(stderr, "FAIL: expected ok for %s; got error code %u\n", what, static_cast<unsigned>(r.error()));
        std::abort();
    }
}

void expect_err(const std::expected<void, PathTraversalError>& r, PathTraversalError want, const char* what) {
    if (r.has_value()) {
        std::fprintf(stderr, "FAIL: expected error %u for %s; got ok\n", static_cast<unsigned>(want), what);
        std::abort();
    }
    if (r.error() != want) {
        std::fprintf(stderr, "FAIL: wrong error for %s — want %u, got %u\n", what, static_cast<unsigned>(want),
                     static_cast<unsigned>(r.error()));
        std::abort();
    }
}

void expect_sanitize_ok(std::expected<SanitizedPath, PathTraversalError>&& r, const char* what) {
    if (!r.has_value()) {
        std::fprintf(stderr, "FAIL: expected sanitize ok for %s; got error %u\n", what,
                     static_cast<unsigned>(r.error()));
        std::abort();
    }
}

void expect_sanitize_err(std::expected<SanitizedPath, PathTraversalError>&& r, PathTraversalError want,
                         const char* what) {
    if (r.has_value()) {
        std::fprintf(stderr, "FAIL: expected sanitize error %u for %s; got ok\n", static_cast<unsigned>(want), what);
        std::abort();
    }
    if (r.error() != want) {
        std::fprintf(stderr, "FAIL: wrong sanitize error for %s — want %u, got %u\n", what, static_cast<unsigned>(want),
                     static_cast<unsigned>(r.error()));
        std::abort();
    }
}

void check_every_error_variant() {
    expect_err(pt::check_no_dotdot(fs_ns::path{""}), PathTraversalError::Empty, "check_no_dotdot empty path");

    {
        std::string huge(::fixy::MAX_PATH_BYTES + 1, 'a');
        // The leading slash keeps the oversize string a plausible
        // absolute path, so the length rule is the one that fires.
        huge[0] = '/';
        expect_err(pt::check_no_dotdot(fs_ns::path{huge}), PathTraversalError::TooLong,
                   "check_no_dotdot oversize path");
    }

    {
        // Copy-initializing from the literal stops at the first NUL,
        // leaving a five-byte string. Resizing back to the literal's
        // length pads with NUL bytes, which is the shape the
        // embedded-NUL rule must reject.
        std::string nul_path = "/good\0/etc/passwd";
        nul_path.resize(17);
        expect_err(pt::check_no_dotdot(fs_ns::path{nul_path}), PathTraversalError::EmbeddedNul,
                   "check_no_dotdot embedded NUL");
    }

    expect_err(pt::check_no_dotdot(fs_ns::path{"/var/../etc/passwd"}), PathTraversalError::DotDotComponent,
               "check_no_dotdot absolute path with embedded ..");

    expect_ok(pt::check_no_dotdot(fs_ns::path{"/var/cipher/objects"}), "check_no_dotdot well-formed absolute path");

    expect_err(pt::check_absolute_root_locked(fs_ns::path{"relative/path"}, fs_ns::path{"/var/cipher"}),
               PathTraversalError::CandidateNotAbsolute, "absolute_root_locked relative candidate");

    expect_err(pt::check_absolute_root_locked(fs_ns::path{"/var/cipher"}, fs_ns::path{"relative/anchor"}),
               PathTraversalError::AnchorNotAbsolute, "absolute_root_locked relative anchor");

    expect_err(pt::check_absolute_root_locked(fs_ns::path{"/var/cipher"}, fs_ns::path{"/home/user"}),
               PathTraversalError::EscapesAnchor, "absolute_root_locked divergent root");

    expect_err(pt::check_absolute_root_locked(fs_ns::path{"/var"}, fs_ns::path{"/var/cipher"}),
               PathTraversalError::EscapesAnchor, "absolute_root_locked candidate shorter than anchor");

    expect_ok(pt::check_absolute_root_locked(fs_ns::path{"/var/cipher"}, fs_ns::path{"/var/cipher"}),
              "absolute_root_locked candidate equals anchor");

    expect_ok(pt::check_absolute_root_locked(fs_ns::path{"/var/cipher/objects/aa/bb"}, fs_ns::path{"/var/cipher"}),
              "absolute_root_locked candidate is subpath of anchor");

    expect_ok(pt::check_absolute_root_locked(fs_ns::path{"/var/./cipher//objects"}, fs_ns::path{"/var/cipher"}),
              "absolute_root_locked candidate normalizes to anchor + subpath");
}

void check_every_admitted_lane() {
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(mint_tagged<src::External>(fs_ns::path{"/var/cipher/objects"})),
                       "sanitize_path_no_dotdot External happy path");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(mint_tagged<src::FromUserPath>(fs_ns::path{"/home/user/data.bin"})),
                       "sanitize_path_no_dotdot FromUserPath happy path");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(mint_tagged<src::FromEnvPath>(fs_ns::path{"/srv/crucible/cipher"})),
                       "sanitize_path_no_dotdot FromEnvPath happy path");
    expect_sanitize_ok(
        pt::sanitize_path_no_dotdot(mint_tagged<src::FromConfigPath>(fs_ns::path{"/etc/crucible/recipes.json"})),
        "sanitize_path_no_dotdot FromConfigPath happy path");

    expect_sanitize_err(pt::sanitize_path_no_dotdot(mint_tagged<src::External>(fs_ns::path{"../etc/passwd"})),
                        PathTraversalError::DotDotComponent, "../etc/passwd rejected as DotDotComponent");

    expect_sanitize_ok(pt::sanitize_path_root_locked(mint_tagged<src::External>(fs_ns::path{"/home/user/data"}),
                                                     fs_ns::path{"/home/user"}),
                       "sanitize_path_root_locked External full happy path");

    expect_sanitize_err(
        pt::sanitize_path_root_locked(mint_tagged<src::External>(fs_ns::path{"/abs/path"}), fs_ns::path{"/home/user"}),
        PathTraversalError::EscapesAnchor, "/abs/path outside /home/user rejected as EscapesAnchor");

    // The anchor would reject this input too. The assertion pins which
    // of the two rules reports the failure.
    expect_sanitize_err(pt::sanitize_path_root_locked(mint_tagged<src::External>(fs_ns::path{"../home/user/data"}),
                                                      fs_ns::path{"/home/user"}),
                        PathTraversalError::DotDotComponent, "no_dotdot fires before the root check");

    // The one-argument entry point carries the value through unchanged.
    auto clean = ::fixy::sanitize_path(mint_tagged<src::External>(fs_ns::path{"/var/cipher/objects"}));
    expect_sanitize_ok(std::move(clean), "sanitize_path entry point");

    // A sanitized path re-enters the sanitizer through the identity
    // edge, which is what lets a helper re-check a value it was handed.
    auto again = ::fixy::sanitize_path(mint_tagged<src::External>(fs_ns::path{"/var/cipher"}));
    if (!again.has_value()) std::abort();
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(std::move(*again)), "re-sanitize an already-sanitized path");
}

}  // namespace

int main() {
    ::fixy::detail::path_self_test::runtime_smoke_test();

    check_every_error_variant();
    check_every_admitted_lane();

    return 0;
}
