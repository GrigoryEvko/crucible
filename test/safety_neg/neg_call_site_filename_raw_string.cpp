// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for #881 WRAP-CallSite-3
// (CallSiteTable::Entry::filename / funcname std::string →
// safety::Tagged<std::string, source::Sanitized>).
//
// Premise: with filename / funcname migrated to
// SanitizedName = Tagged<std::string, source::Sanitized>, direct
// aggregate-init of Entry with a raw std::string is rejected
// because Tagged's only ctor is `explicit Tagged(T)` — std::string
// does not implicitly convert to Tagged<std::string, Sanitized>.
//
// Pre-migration the field was bare std::string and any caller could
// build an Entry with raw strings, blurring the post-validation /
// pre-validation distinction at storage.  With the type-system
// gate, every Entry that flows through public insert() goes
// through SanitizedName{...} explicitly, and direct-Entry
// construction outside that path is forced to acknowledge the
// trust level.
//
// Distinct mismatch class from companion fixture
// neg_call_site_filename_external_passback.cpp:
//   * This fixture: WRITE-side gate (raw std::string rejected
//     because Tagged ctor is explicit).
//   * Companion:    READ-side gate (extracted SanitizedName cannot
//     pass to a function expecting source::External — the trust
//     elevation is one-way).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/ir001/CallSiteTable.h>

#include <cstdint>
#include <string>

int main() {
    // Tagged<std::string, source::Sanitized> has only `explicit
    // Tagged(T)` and no implicit conversion from std::string.  The
    // aggregate-init below tries to bind `std::string{"file.py"}`
    // to the SanitizedName field — must fail at the brace.
    crucible::CallSiteTable::Entry e{
        crucible::CallsiteHash{uint64_t{0xC0FFEE}},
        std::string{"file.py"},   // ← MUST fail: no viable ctor
        std::string{"f"},
        crucible::CallSiteTable::Lineno{int32_t{42}}
    };
    (void)e;
    return 0;
}
