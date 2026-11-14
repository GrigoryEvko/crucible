// Sentinel TU for fixy/os/Path.h: each predicate answers on its success
// path and on every distinct variant of PathTraversalError, so a
// regression in one rule reddens here rather than hiding behind a
// neighbouring rule that happens to reject the same input.
//
// This TU also checks the tag arithmetic: the value constructor of Tagged
// is private, so every tainted path here is minted.

#include <fixy/Core.h>
#include <fixy/os/Path.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

namespace {

namespace pt = ::fixy::sanitize::path_traversal;
namespace src = ::fixy::tags::source;

using ::fixy::mint_tagged;
using ::fixy::Path;
using ::fixy::PathTraversalError;
using ::fixy::TextView;

using SanitizedPath = Path<src::Sanitized>;
using Checked = ::fixy::Result<::fixy::Unit, PathTraversalError>;
using Sanitized = ::fixy::Result<SanitizedPath, PathTraversalError>;

// The tag is the whole mechanism, so it has to be part of the type.
static_assert(!std::is_same_v<Path<src::External>, SanitizedPath>);
static_assert(sizeof(Path<src::External>) == sizeof(::foundation::os::PathText<::fixy::kPathBytes>));

// A path whose tag has no admitted edge into Sanitized cannot reach
// either predicate. CipherPath is deliberately one of those.
template <typename From>
concept CanSanitizeNoDotdot = requires(Path<From> p) { pt::sanitize_path_no_dotdot(std::move(p)); };
template <typename From>
concept CanSanitizeRootLocked =
    requires(Path<From> p, TextView anchor) { pt::sanitize_path_root_locked(std::move(p), anchor); };
static_assert(CanSanitizeNoDotdot<src::External>);
static_assert(CanSanitizeNoDotdot<src::FromUserPath>);
static_assert(CanSanitizeNoDotdot<src::FromEnvPath>);
static_assert(CanSanitizeNoDotdot<src::FromConfigPath>);
static_assert(CanSanitizeRootLocked<src::External>);
static_assert(!CanSanitizeNoDotdot<src::CipherPath>, "CipherPath has no edge into Sanitized on purpose");
static_assert(!CanSanitizeRootLocked<src::CipherPath>);

// The text entry takes a text tagged External and nothing else, which
// is what keeps the other lanes at the two-argument door.
template <typename From>
concept CanSanitizeText = requires(::fixy::Tagged<TextView, From> const& text) { ::fixy::sanitize_path(text); };
static_assert(CanSanitizeText<src::External>);
static_assert(!CanSanitizeText<src::FromUserPath>);
static_assert(!CanSanitizeText<src::Sanitized>);

// The path of text under the tag Source.  Each text of these tests fits a
// path and holds no zero byte.
template <typename Source>
[[nodiscard]] Path<Source> path_of(TextView text) {
    ::foundation::os::PathText<::fixy::kPathBytes> characters{};
    characters.append(text).expect("each text of these tests fits a path");
    return mint_tagged<Source>(std::move(characters));
}

// The text tagged External that the text entry takes.
[[nodiscard]] ::fixy::Tagged<TextView, src::External> external(TextView text) {
    return mint_tagged<src::External>(text);
}

[[nodiscard]] unsigned code_of(PathTraversalError error) { return static_cast<unsigned>(error); }

void expect_ok(Checked const& result, const char* what) {
    if (result.is_err()) {
        std::fprintf(stderr, "FAIL: expected ok for %s; got error code %u\n", what,
                     code_of(result.err().expect("the result holds an error")));
        std::abort();
    }
}

void expect_err(Checked const& result, PathTraversalError want, const char* what) {
    if (result.is_ok()) {
        std::fprintf(stderr, "FAIL: expected error %u for %s; got ok\n", code_of(want), what);
        std::abort();
    }
    const PathTraversalError got = result.err().expect("the result holds an error");
    if (got != want) {
        std::fprintf(stderr, "FAIL: wrong error for %s — want %u, got %u\n", what, code_of(want), code_of(got));
        std::abort();
    }
}

void expect_sanitize_ok(Sanitized const& result, const char* what) {
    if (result.is_err()) {
        std::fprintf(stderr, "FAIL: expected sanitize ok for %s; got error %u\n", what,
                     code_of(result.err().expect("the result holds an error")));
        std::abort();
    }
}

void expect_sanitize_err(Sanitized const& result, PathTraversalError want, const char* what) {
    if (result.is_ok()) {
        std::fprintf(stderr, "FAIL: expected sanitize error %u for %s; got ok\n", code_of(want), what);
        std::abort();
    }
    const PathTraversalError got = result.err().expect("the result holds an error");
    if (got != want) {
        std::fprintf(stderr, "FAIL: wrong sanitize error for %s — want %u, got %u\n", what, code_of(want),
                     code_of(got));
        std::abort();
    }
}

void check_every_error_variant() {
    expect_err(pt::check_no_dotdot(TextView{}), PathTraversalError::Empty, "check_no_dotdot empty path");

    {
        // The leading slash keeps the oversize string a plausible
        // absolute path, so the length rule is the one that fires.
        std::string huge(::fixy::kPathBytes + 1, 'a');
        huge[0] = '/';
        expect_err(pt::check_no_dotdot(::fixy::mint_foreign_text(huge.data(), huge.size())),
                   PathTraversalError::TooLong, "check_no_dotdot oversize path");
        // A text of exactly the bound fits.
        expect_ok(pt::check_no_dotdot(::fixy::mint_foreign_text(huge.data(), ::fixy::kPathBytes)),
                  "check_no_dotdot path of the bound");
    }

    {
        // Copy-initializing from the literal stops at the first NUL,
        // leaving a five-byte string. Resizing back to the literal's
        // length pads with NUL bytes, which is the shape the
        // embedded-NUL rule must reject.
        std::string nul_path = "/good\0/etc/passwd";
        nul_path.resize(17);
        expect_err(pt::check_no_dotdot(::fixy::mint_foreign_text(nul_path.data(), nul_path.size())),
                   PathTraversalError::EmbeddedNul, "check_no_dotdot embedded NUL");
    }

    expect_err(pt::check_no_dotdot("/var/../etc/passwd"), PathTraversalError::DotDotComponent,
               "check_no_dotdot absolute path with embedded ..");
    expect_err(pt::check_no_dotdot(".."), PathTraversalError::DotDotComponent, "check_no_dotdot bare ..");
    expect_err(pt::check_no_dotdot("/var/cipher/.."), PathTraversalError::DotDotComponent,
               "check_no_dotdot trailing ..");

    expect_ok(pt::check_no_dotdot("/var/cipher/objects"), "check_no_dotdot well-formed absolute path");
    expect_ok(pt::check_no_dotdot("/var/..cipher/.../objects.."), "check_no_dotdot dots inside a name");
    expect_ok(pt::check_no_dotdot("./var//cipher/"), "check_no_dotdot dot and empty parts");

    expect_err(pt::check_absolute_root_locked("relative/path", "/var/cipher"), PathTraversalError::CandidateNotAbsolute,
               "absolute_root_locked relative candidate");

    expect_err(pt::check_absolute_root_locked("/var/cipher", "relative/anchor"), PathTraversalError::AnchorNotAbsolute,
               "absolute_root_locked relative anchor");

    expect_err(pt::check_absolute_root_locked("/var/cipher", "/home/user"), PathTraversalError::EscapesAnchor,
               "absolute_root_locked divergent root");

    expect_err(pt::check_absolute_root_locked("/var", "/var/cipher"), PathTraversalError::EscapesAnchor,
               "absolute_root_locked candidate shorter than anchor");

    // The compare is of whole parts, not of characters.
    expect_err(pt::check_absolute_root_locked("/var/cipher2/objects", "/var/cipher"), PathTraversalError::EscapesAnchor,
               "absolute_root_locked sibling that shares a prefix of characters");

    // A ".." part after the anchor parts can climb out of the anchor.
    expect_err(pt::check_absolute_root_locked("/var/cipher/../etc", "/var/cipher"), PathTraversalError::DotDotComponent,
               "absolute_root_locked candidate with ..");

    // A ".." part of the anchor is a name that no candidate part has.
    expect_err(pt::check_absolute_root_locked("/var/cipher/objects", "/var/x/../cipher"),
               PathTraversalError::EscapesAnchor, "absolute_root_locked anchor with ..");

    expect_ok(pt::check_absolute_root_locked("/var/cipher", "/var/cipher"),
              "absolute_root_locked candidate equals anchor");

    expect_ok(pt::check_absolute_root_locked("/var/cipher/objects/aa/bb", "/var/cipher"),
              "absolute_root_locked candidate is subpath of anchor");

    expect_ok(pt::check_absolute_root_locked("/var/./cipher//objects", "/var/cipher"),
              "absolute_root_locked candidate with dot and empty parts");

    expect_ok(pt::check_absolute_root_locked("/var/cipher/objects", "/var/cipher/"),
              "absolute_root_locked anchor with a trailing slash");

    expect_ok(pt::check_absolute_root_locked("/etc/passwd", "/"), "absolute_root_locked anchor of the root");
}

void check_every_admitted_lane() {
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(path_of<src::External>("/var/cipher/objects")),
                       "sanitize_path_no_dotdot External happy path");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(path_of<src::FromUserPath>("/home/user/data.bin")),
                       "sanitize_path_no_dotdot FromUserPath happy path");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(path_of<src::FromEnvPath>("/srv/crucible/cipher")),
                       "sanitize_path_no_dotdot FromEnvPath happy path");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(path_of<src::FromConfigPath>("/etc/crucible/recipes.json")),
                       "sanitize_path_no_dotdot FromConfigPath happy path");

    expect_sanitize_err(pt::sanitize_path_no_dotdot(path_of<src::External>("../etc/passwd")),
                        PathTraversalError::DotDotComponent, "../etc/passwd rejected as DotDotComponent");
    expect_sanitize_err(pt::sanitize_path_no_dotdot(Path<src::External>{}), PathTraversalError::Empty,
                        "the empty path rejected as Empty");

    expect_sanitize_ok(pt::sanitize_path_root_locked(path_of<src::External>("/home/user/data"), "/home/user"),
                       "sanitize_path_root_locked External full happy path");

    expect_sanitize_err(pt::sanitize_path_root_locked(path_of<src::External>("/abs/path"), "/home/user"),
                        PathTraversalError::EscapesAnchor, "/abs/path outside /home/user rejected as EscapesAnchor");

    // The anchor would reject this input too. The assertion pins which
    // of the two rules reports the failure.
    expect_sanitize_err(pt::sanitize_path_root_locked(path_of<src::External>("../home/user/data"), "/home/user"),
                        PathTraversalError::DotDotComponent, "no_dotdot fires before the root check");

    // The text entry carries the characters through unchanged.  The retag
    // is the whole of the transformation, so a sanitizer that rewrote the
    // path would still report success here.
    const Sanitized clean = ::fixy::sanitize_path(external("/var/./cipher//objects"));
    expect_sanitize_ok(clean, "sanitize_path entry point");
    clean.match(
        [](SanitizedPath const& path) {
            if (!(path.value().c_text().view() == TextView{"/var/./cipher//objects"})) {
                std::fprintf(stderr, "FAIL: sanitize_path rewrote the value it carried\n");
                std::abort();
            }
        },
        [](PathTraversalError const&) { std::abort(); });

    // The text entry runs each check of check_no_dotdot on the text, before
    // the copy into the path.
    expect_sanitize_err(::fixy::sanitize_path(external(TextView{})), PathTraversalError::Empty,
                        "sanitize_path of the empty text");
    expect_sanitize_err(::fixy::sanitize_path(external("/var/../etc")), PathTraversalError::DotDotComponent,
                        "sanitize_path of a text with ..");
    {
        const std::string huge(::fixy::kPathBytes + 1, 'a');
        expect_sanitize_err(::fixy::sanitize_path(external(::fixy::mint_foreign_text(huge.data(), huge.size()))),
                            PathTraversalError::TooLong, "sanitize_path of a text past the bound");
        expect_sanitize_ok(::fixy::sanitize_path(external(::fixy::mint_foreign_text(huge.data(), ::fixy::kPathBytes))),
                           "sanitize_path of a text of the bound");
    }
    {
        std::string nul_path = "/good\0/etc/passwd";
        nul_path.resize(17);
        expect_sanitize_err(
            ::fixy::sanitize_path(external(::fixy::mint_foreign_text(nul_path.data(), nul_path.size()))),
            PathTraversalError::EmbeddedNul, "sanitize_path of a text with a zero byte");
    }

    // A sanitized path re-enters the sanitizer through the identity
    // edge, which is what lets a helper re-check a value it was handed.
    SanitizedPath again = ::fixy::sanitize_path(external("/var/cipher")).expect("the path is clean");
    expect_sanitize_ok(pt::sanitize_path_no_dotdot(std::move(again)), "re-sanitize an already-sanitized path");
}

}  // namespace

int main() {
    check_every_error_variant();
    check_every_admitted_lane();

    return 0;
}
