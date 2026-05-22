// FIXY-V-233 runtime smoke test — sanitize::path_traversal predicates.
//
// Witnesses the two new predicates introduced by V-233:
//
//   * `check_no_dotdot`         — V-031's four-rule no-dot-dot
//                                  algorithm, lifted into a free
//                                  function with a `std::expected`
//                                  error channel.
//   * `check_absolute_root_locked` — root-anchor containment using
//                                  `std::filesystem::path::
//                                  lexically_normal()` (byte-level,
//                                  no filesystem syscalls, noexcept).
//
// Plus the two compositional entry-points that retag the result to
// `source::Sanitized` on success:
//
//   * `sanitize_path_no_dotdot<From>(Path<From>&&)`
//   * `sanitize_path_root_locked<From>(Path<From>&&, anchor)`
//
// Each predicate is checked against the success path AND each
// distinct failure mode in the `PathTraversalError` enum, so a
// regression in any rule reddens here.

#include <crucible/safety/sanitize/PathTraversal.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace pt   = crucible::safety::sanitize::path_traversal;
namespace fs   = std::filesystem;
namespace src  = crucible::safety::source;

namespace cs = crucible::safety;
using cs::Path;
using cs::PathTraversalError;

namespace {

// ── Tiny assertion helper (no-throw, abort-on-fail) ────────────────
// Reads the result, prints diagnostic, std::abort()s on mismatch.
// Lighter than gtest; matches the harness convention used in other
// V-* runtime smoke tests.

void expect_ok(const std::expected<void, PathTraversalError>& r,
               const char* what) {
    if (!r.has_value()) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: expected ok for %s; got error code %u\n",
            what, static_cast<unsigned>(r.error()));
        std::abort();
    }
}

void expect_err(const std::expected<void, PathTraversalError>& r,
                PathTraversalError want,
                const char* what) {
    if (r.has_value()) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: expected error %u for %s; got ok\n",
            static_cast<unsigned>(want), what);
        std::abort();
    }
    if (r.error() != want) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: wrong error for %s — want %u, got %u\n",
            what, static_cast<unsigned>(want),
            static_cast<unsigned>(r.error()));
        std::abort();
    }
}

template <typename From>
void expect_sanitize_ok(std::expected<Path<src::Sanitized>,
                                       PathTraversalError>&& r,
                        const char* what) {
    if (!r.has_value()) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: expected sanitize ok for %s; got error %u\n",
            what, static_cast<unsigned>(r.error()));
        std::abort();
    }
}

template <typename From>
void expect_sanitize_err(std::expected<Path<src::Sanitized>,
                                        PathTraversalError>&& r,
                         PathTraversalError want,
                         const char* what) {
    if (r.has_value()) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: expected sanitize error %u for %s; got ok\n",
            static_cast<unsigned>(want), what);
        std::abort();
    }
    if (r.error() != want) {
        std::fprintf(stderr,
            "FIXY-V-233 FAIL: wrong sanitize error for %s — want %u, got %u\n",
            what, static_cast<unsigned>(want),
            static_cast<unsigned>(r.error()));
        std::abort();
    }
}

}  // namespace

int main() {
    // ── HS14 fixture #1 (per V-233 task description) ───────────────
    // `Path<External>{"../etc/passwd"}` passed to no_dotdot sanitize
    // returns unexpected (DotDotComponent).
    {
        Path<src::External> tainted{fs::path{"../etc/passwd"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_err<src::External>(std::move(r),
            PathTraversalError::DotDotComponent,
            "HS14#1: ../etc/passwd → DotDotComponent");
    }

    // ── HS14 fixture #2 (per V-233 task description) ───────────────
    // `Path<External>{"/abs/path"}` passed to absolute_root_locked
    // with anchor "/home/user" returns unexpected (EscapesAnchor) —
    // the path resolves outside the anchor.
    {
        Path<src::External> tainted{fs::path{"/abs/path"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted),
                                                fs::path{"/home/user"});
        expect_sanitize_err<src::External>(std::move(r),
            PathTraversalError::EscapesAnchor,
            "HS14#2: /abs/path outside /home/user → EscapesAnchor");
    }

    // ─────────────────────────────────────────────────────────────
    // Comprehensive predicate-rule witnessing — each enum variant
    // gets a positive failure case.
    // ─────────────────────────────────────────────────────────────

    // check_no_dotdot — rule 1 — Empty
    expect_err(pt::check_no_dotdot(fs::path{""}),
               PathTraversalError::Empty,
               "check_no_dotdot empty path");

    // check_no_dotdot — rule 2 — TooLong (16 KiB + 1 byte)
    {
        std::string huge(cs::MAX_PATH_BYTES + 1, 'a');
        huge[0] = '/';  // make it absolute-ish so it doesn't fail rule 1
        expect_err(pt::check_no_dotdot(fs::path{huge}),
                   PathTraversalError::TooLong,
                   "check_no_dotdot oversize path");
    }

    // check_no_dotdot — rule 3 — EmbeddedNul
    {
        std::string nul_path = "/good\0/etc/passwd";
        nul_path.resize(17);  // restore embedded NUL into std::string
        expect_err(pt::check_no_dotdot(fs::path{nul_path}),
                   PathTraversalError::EmbeddedNul,
                   "check_no_dotdot embedded NUL");
    }

    // check_no_dotdot — rule 4 — DotDotComponent (already tested
    // via HS14 #1 above; pin a different shape here)
    expect_err(pt::check_no_dotdot(fs::path{"/var/../etc/passwd"}),
               PathTraversalError::DotDotComponent,
               "check_no_dotdot absolute path with embedded ..");

    // check_no_dotdot — positive — well-formed absolute path
    expect_ok(pt::check_no_dotdot(fs::path{"/var/cipher/objects"}),
              "check_no_dotdot well-formed absolute path");

    // ─────────────────────────────────────────────────────────────
    // check_absolute_root_locked — every failure mode + positive.
    // ─────────────────────────────────────────────────────────────

    // CandidateNotAbsolute — relative candidate
    expect_err(pt::check_absolute_root_locked(fs::path{"relative/path"},
                                              fs::path{"/var/cipher"}),
               PathTraversalError::CandidateNotAbsolute,
               "absolute_root_locked relative candidate");

    // AnchorNotAbsolute — relative anchor
    expect_err(pt::check_absolute_root_locked(fs::path{"/var/cipher"},
                                              fs::path{"relative/anchor"}),
               PathTraversalError::AnchorNotAbsolute,
               "absolute_root_locked relative anchor");

    // EscapesAnchor — divergent first component
    expect_err(pt::check_absolute_root_locked(fs::path{"/var/cipher"},
                                              fs::path{"/home/user"}),
               PathTraversalError::EscapesAnchor,
               "absolute_root_locked divergent root");

    // EscapesAnchor — candidate strictly shorter than anchor
    expect_err(pt::check_absolute_root_locked(fs::path{"/var"},
                                              fs::path{"/var/cipher"}),
               PathTraversalError::EscapesAnchor,
               "absolute_root_locked candidate shorter than anchor");

    // Positive — candidate equal to anchor
    expect_ok(pt::check_absolute_root_locked(fs::path{"/var/cipher"},
                                              fs::path{"/var/cipher"}),
              "absolute_root_locked candidate equals anchor");

    // Positive — candidate is a proper subpath of anchor
    expect_ok(pt::check_absolute_root_locked(
                  fs::path{"/var/cipher/objects/aa/bb"},
                  fs::path{"/var/cipher"}),
              "absolute_root_locked candidate is subpath of anchor");

    // Positive — candidate normalizes equal to anchor (./ components)
    expect_ok(pt::check_absolute_root_locked(
                  fs::path{"/var/./cipher//objects"},
                  fs::path{"/var/cipher"}),
              "absolute_root_locked candidate normalizes to anchor + subpath");

    // ─────────────────────────────────────────────────────────────
    // Compositional sanitize entry-points — positive paths through
    // each V-232 source tag → Sanitized.
    // ─────────────────────────────────────────────────────────────

    // sanitize_path_no_dotdot from External (V-031 admittance)
    {
        Path<src::External> tainted{fs::path{"/var/cipher/objects"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::External>(std::move(r),
            "sanitize_path_no_dotdot External → Sanitized happy path");
    }

    // sanitize_path_no_dotdot from FromUserPath (V-232 admittance)
    {
        Path<src::FromUserPath> tainted{fs::path{"/home/user/data.bin"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromUserPath>(std::move(r),
            "sanitize_path_no_dotdot FromUserPath → Sanitized happy path");
    }

    // sanitize_path_no_dotdot from FromEnvPath (V-232 admittance)
    {
        Path<src::FromEnvPath> tainted{fs::path{"/srv/crucible/cipher"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromEnvPath>(std::move(r),
            "sanitize_path_no_dotdot FromEnvPath → Sanitized happy path");
    }

    // sanitize_path_no_dotdot from FromConfigPath (V-232 admittance)
    {
        Path<src::FromConfigPath> tainted{fs::path{"/etc/crucible/recipes.json"}};
        auto r = pt::sanitize_path_no_dotdot(std::move(tainted));
        expect_sanitize_ok<src::FromConfigPath>(std::move(r),
            "sanitize_path_no_dotdot FromConfigPath → Sanitized happy path");
    }

    // sanitize_path_root_locked — full composed path: External →
    // anchor "/home/user", candidate "/home/user/data" → ok.
    {
        Path<src::External> tainted{fs::path{"/home/user/data"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted),
                                                 fs::path{"/home/user"});
        expect_sanitize_ok<src::External>(std::move(r),
            "sanitize_path_root_locked External → Sanitized full happy path");
    }

    // sanitize_path_root_locked — no_dotdot fires FIRST on a path
    // containing `..` even though the anchor would also reject it.
    // Witness the predicate ordering.
    {
        Path<src::External> tainted{fs::path{"../home/user/data"}};
        auto r = pt::sanitize_path_root_locked(std::move(tainted),
                                                 fs::path{"/home/user"});
        expect_sanitize_err<src::External>(std::move(r),
            PathTraversalError::DotDotComponent,
            "sanitize_path_root_locked no_dotdot fires before root check");
    }

    return 0;
}
