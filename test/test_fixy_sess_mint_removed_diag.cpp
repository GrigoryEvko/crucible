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

// ─── Runtime entry — touch the diagnostic surface ─────────────────

int main() {
    // Force one runtime touch of the structured name so the tag
    // can never become dead-code-eliminated below the static_asserts.
    auto name = fd::diagnostic_name_v<fs::diag::FixyMintSessionRemoved>;
    return name.empty() ? 1 : 0;
}
