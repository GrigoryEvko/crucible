// Each predicate is exercised against its success path and against every
// distinct variant of PathTraversalError, so a regression in any single rule
// reddens here rather than hiding behind a neighbouring rule that happens to
// reject the same input.

#include <crucible/safety/sanitize/PathTraversal.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace pt = crucible::safety::sanitize::path_traversal;
namespace fs = std::filesystem;
namespace src = crucible::safety::source;

namespace cs = crucible::safety;
using cs::Path;
using cs::PathTraversalError;

namespace {

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

template <typename From>
void expect_sanitize_ok(std::expected<Path<src::Sanitized>, PathTraversalError>&& r, const char* what) {
    if (!r.has_value()) {
        std::fprintf(stderr, "FAIL: expected sanitize ok for %s; got error %u\n", what,
                     static_cast<unsigned>(r.error()));
        std::abort();
    }
}

template <typename From>
void expect_sanitize_err(std::expected<Path<src::Sanitized>, PathTraversalError>&& r, PathTraversalError want,
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

}  // namespace

int main() {
    {
        Path<src::External> tainted{fs::path{"../etc/passwd"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_err<src::External>(std::move(r), PathTraversalError::DotDotComponent,
                                           "../etc/passwd → DotDotComponent");
    }

    {
        Path<src::External> tainted{fs::path{"/abs/path"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted), fs::path{"/home/user"});
        expect_sanitize_err<src::External>(std::move(r), PathTraversalError::EscapesAnchor,
                                           "/abs/path outside /home/user → EscapesAnchor");
    }

    expect_err(pt::check_no_dotdot(fs::path{""}), PathTraversalError::Empty, "check_no_dotdot empty path");

    {
        std::string huge(cs::MAX_PATH_BYTES + 1, 'a');
        // The leading slash keeps the oversize string a plausible absolute
        // path, so the length rule is the one that fires.
        huge[0] = '/';
        expect_err(pt::check_no_dotdot(fs::path{huge}), PathTraversalError::TooLong, "check_no_dotdot oversize path");
    }

    {
        // Copy-initializing from the literal stops at the first NUL, leaving
        // a five-byte string. Resizing back to the literal's length pads with
        // NUL bytes, which is the shape the embedded-NUL rule must reject.
        std::string nul_path = "/good\0/etc/passwd";
        nul_path.resize(17);
        expect_err(pt::check_no_dotdot(fs::path{nul_path}), PathTraversalError::EmbeddedNul,
                   "check_no_dotdot embedded NUL");
    }

    expect_err(pt::check_no_dotdot(fs::path{"/var/../etc/passwd"}), PathTraversalError::DotDotComponent,
               "check_no_dotdot absolute path with embedded ..");

    expect_ok(pt::check_no_dotdot(fs::path{"/var/cipher/objects"}), "check_no_dotdot well-formed absolute path");

    expect_err(pt::check_absolute_root_locked(fs::path{"relative/path"}, fs::path{"/var/cipher"}),
               PathTraversalError::CandidateNotAbsolute, "absolute_root_locked relative candidate");

    expect_err(pt::check_absolute_root_locked(fs::path{"/var/cipher"}, fs::path{"relative/anchor"}),
               PathTraversalError::AnchorNotAbsolute, "absolute_root_locked relative anchor");

    expect_err(pt::check_absolute_root_locked(fs::path{"/var/cipher"}, fs::path{"/home/user"}),
               PathTraversalError::EscapesAnchor, "absolute_root_locked divergent root");

    expect_err(pt::check_absolute_root_locked(fs::path{"/var"}, fs::path{"/var/cipher"}),
               PathTraversalError::EscapesAnchor, "absolute_root_locked candidate shorter than anchor");

    expect_ok(pt::check_absolute_root_locked(fs::path{"/var/cipher"}, fs::path{"/var/cipher"}),
              "absolute_root_locked candidate equals anchor");

    expect_ok(pt::check_absolute_root_locked(fs::path{"/var/cipher/objects/aa/bb"}, fs::path{"/var/cipher"}),
              "absolute_root_locked candidate is subpath of anchor");

    expect_ok(pt::check_absolute_root_locked(fs::path{"/var/./cipher//objects"}, fs::path{"/var/cipher"}),
              "absolute_root_locked candidate normalizes to anchor + subpath");

    {
        Path<src::External> tainted{fs::path{"/var/cipher/objects"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::External>(std::move(r), "sanitize_path_no_dotdot External → Sanitized happy path");
    }

    {
        Path<src::FromUserPath> tainted{fs::path{"/home/user/data.bin"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromUserPath>(std::move(r),
                                              "sanitize_path_no_dotdot FromUserPath → Sanitized happy path");
    }

    {
        Path<src::FromEnvPath> tainted{fs::path{"/srv/crucible/cipher"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromEnvPath>(std::move(r),
                                             "sanitize_path_no_dotdot FromEnvPath → Sanitized happy path");
    }

    {
        Path<src::FromConfigPath> tainted{fs::path{"/etc/crucible/recipes.json"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromConfigPath>(std::move(r),
                                                "sanitize_path_no_dotdot FromConfigPath → Sanitized happy path");
    }

    {
        Path<src::External> tainted{fs::path{"/home/user/data"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted), fs::path{"/home/user"});
        expect_sanitize_ok<src::External>(std::move(r),
                                          "sanitize_path_root_locked External → Sanitized full happy path");
    }

    // The anchor would reject this input too. The assertion pins which of the
    // two rules reports the failure.
    {
        Path<src::External> tainted{fs::path{"../home/user/data"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted), fs::path{"/home/user"});
        expect_sanitize_err<src::External>(std::move(r), PathTraversalError::DotDotComponent,
                                           "sanitize_path_root_locked no_dotdot fires before root check");
    }

    return 0;
}
