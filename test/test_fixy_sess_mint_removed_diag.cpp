// ── test_fixy_sess_mint_removed_diag — FIXY-AUDIT-B6 sentinel TU ──
//
// Witnesses for the `fixy::sess::diag::FixyMintSessionRemoved`
// structured diagnostic that classifies the `= delete`d
// `mint_session<Proto>(...)` overloads in sessions/SessionMint.h:
//
//   1. The tag inherits `safety::diag::tag_base` and satisfies
//      `is_diagnostic_class_v` (the structural concept that admits
//      foundation tags AND user-defined fixy tags).
//   2. `safety::diag::diagnostic_name_v<Tag>` is non-empty and equals
//      the literal "FixyMintSessionRemoved".
//   3. `description` and `remediation` are non-empty.
//   4. The tag is NOT in the closed Catalog (does not participate in
//      `category_of_v`) — it is a user-defined diag, identical model
//      to the 20 `FixyNotEngaged_*` tags in fixy/Reject.h.
//
// HS14: no neg-compile fixture required — this is a tag-class
// registration, not a mint factory.  The TU itself is the positive
// witness that the structured diagnostic is reachable, named, and
// classified.  Deletion behavior of `mint_session` is already
// witnessed by the existing fixy_neg fixtures for the canonical
// `mint_permissioned_session` flow.

#include <crucible/fixy/Sess.h>

#include <string_view>
#include <type_traits>

namespace fd  = crucible::safety::diag;
namespace fs  = crucible::fixy::sess;

// ─── 1. Tag is a diagnostic class ─────────────────────────────────

static_assert(std::is_base_of_v<fd::tag_base,
                                fs::diag::FixyMintSessionRemoved>,
    "FixyMintSessionRemoved must inherit safety::diag::tag_base.");

static_assert(fd::is_diagnostic_class_v<fs::diag::FixyMintSessionRemoved>,
    "FixyMintSessionRemoved must satisfy is_diagnostic_class_v.");

// ─── 2. diagnostic_name_v reachability + value ────────────────────

static_assert(!fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>.empty(),
    "FixyMintSessionRemoved must surface a non-empty name string.");

static_assert(fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>
              == std::string_view{"FixyMintSessionRemoved"},
    "FixyMintSessionRemoved must surface the canonical name literal.");

// ─── 3. description + remediation are non-empty ───────────────────

static_assert(!fd::diagnostic_description_v<
                  fs::diag::FixyMintSessionRemoved>.empty(),
    "FixyMintSessionRemoved description must explain the deletion.");

static_assert(!fd::diagnostic_remediation_v<
                  fs::diag::FixyMintSessionRemoved>.empty(),
    "FixyMintSessionRemoved remediation must point at "
    "mint_permissioned_session.");

// ─── 4. Tag is final (not extensible via subclass) ────────────────

static_assert(std::is_final_v<fs::diag::FixyMintSessionRemoved>,
    "User-defined fixy diag tags inherit the final-class discipline "
    "from FOUND-E01 (matches FixyNotEngaged_* convention).");

// ─── 5. Tag is stateless (zero data members) ──────────────────────

static_assert(std::is_empty_v<fs::diag::FixyMintSessionRemoved>,
    "Diagnostic tags are pure type-level witnesses — no data.");

// ─── 6. Negative witness — int is not a diagnostic class ──────────

static_assert(!fd::is_diagnostic_class_v<int>);

// ─── 7. fixy-A4-014: §XXI grep-discipline structural pin ──────────
//
// `grep "mint_session<"` finds the using-decl in fixy/Sess.h:238.
// The substrate has TWO `= delete`d overloads (SessionMint.h:977
// for the ctx-bound form, :986 for the bare-resource form).  Audit
// fixy-A4-014 found that the §XXI grep-target is structurally
// diluted: the re-export makes `fixy::sess::mint_session<P>(...)`
// appear as a live authorization point in grep output when it is
// actually a deletion canary.
//
// The structural pin runs across THREE surfaces:
//
//   1. The two HS14 neg-compile fixtures
//      (test/fixy_neg/neg_fixy_sess_mint_session_{deleted,bare_deleted}.cpp)
//      witness the deletion diagnostic fires for BOTH overloads
//      through the fixy:: path.  GCC 16's `= delete("...")` is not
//      SFINAE-friendly — a requires-expression `mint_session<P>(c,r)`
//      emits a hard "use of deleted function" error rather than
//      yielding false — so the witness lives in a fail-to-compile
//      fixture, not a static_assert.  This is intentional per C++26
//      semantics: `= delete` is overload-resolution-visible, and the
//      error message IS the structured diagnostic surface.
//
//   2. The positive static_assert below pins that the canonical
//      replacement (`mint_permissioned_session<Proto>(ctx, resource)`)
//      stays REACHABLE.  A future refactor that deletes both the
//      original AND the replacement (e.g. an attempted rename to
//      `mint_typed_session`) reddens here, so call sites are never
//      silently stranded without a construction surface.
//
//   3. The `FixyMintSessionRemoved` diagnostic-class registration
//      (sections 1-5 above) pins the structured diagnostic tag stays
//      classified and named; rename or delete of the tag itself
//      reddens those static_asserts.

#include <utility>            // std::move

#include <crucible/effects/ExecCtx.h>

namespace fixy_a4_014_pin {

struct ProbeResource {};

// `Send<int, End>` is the canonical minimal well-formed session per
// the substrate's own self-tests — guarantees the deletion check is
// reached (rather than failing the prior is_well_formed gate).
using ProbeWellFormedProto = fs::Send<int, fs::End>;

// Pin: the canonical replacement IS callable through fixy::sess::.
// A future move that deletes both the original AND the replacement
// (e.g. an attempted rename to mint_typed_session) reddens here
// rather than silently leaving call sites with no construction
// surface.  The two HS14 fixtures pin the negative side
// (substrate-deleted overloads stay rejected).
static_assert(requires(::crucible::effects::BgCompileCtx ctx,
                       ProbeResource res) {
    fs::mint_permissioned_session<ProbeWellFormedProto>(ctx, std::move(res));
}, "fixy-A4-014: mint_permissioned_session<Proto>(ctx, resource) MUST "
   "remain the canonical replacement for the =deleted mint_session "
   "overloads.  If this pin reddens, audit the remediation field of "
   "FixyMintSessionRemoved (it points to mint_permissioned_session).");

}  // namespace fixy_a4_014_pin

// ─── Runtime entry — touch the diagnostic surface ─────────────────

int main() {
    // Force one runtime touch of the structured name so the tag
    // can never become dead-code-eliminated below the static_asserts.
    auto name = fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>;
    return name.empty() ? 1 : 0;
}
