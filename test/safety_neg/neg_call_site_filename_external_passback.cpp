// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for #881 WRAP-CallSite-3
// (CallSiteTable::Entry::filename / funcname std::string →
// safety::Tagged<std::string, source::Sanitized>).
//
// Premise: with filename / funcname migrated to
// SanitizedName = Tagged<std::string, source::Sanitized>, reading
// the stored filename and passing it to a function that demands
// Tagged<std::string, source::External> is rejected because two
// distinct Tag instantiations of Tagged<T,...> do not implicitly
// convert.  Trust elevation through insert() is one-way:
// External / FromInternal go IN, Sanitized comes OUT, and a caller
// who tries to feed a stored filename back into an External-only
// API is caught at the call site.
//
// Pre-migration the field was bare std::string and the caller
// could pass it to any sanitized-or-not API freely; the table's
// post-validation provenance died at the boundary.  With the
// type-system gate, the source::Sanitized tag survives reads.
//
// Distinct mismatch class from companion fixture
// neg_call_site_filename_raw_string.cpp:
//   * Companion:    WRITE-side gate (no viable Tagged ctor from
//     raw std::string at Entry aggregate-init).
//   * This fixture: READ-side gate (Tagged<...,Sanitized> not
//     implicitly convertible to Tagged<...,External>).
//
// Two fixtures cover both directions of the new soundness gate per HS14.

#include <crucible/ir001/CallSiteTable.h>
#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <string>
#include <utility>

// Sanitized-only API surface.  Demands an External-tagged input;
// post-validation Sanitized values are NOT acceptable here (the
// ExternalName lane is reserved for raw FFI / network arrivals).
using ExternalName = crucible::safety::Tagged<
    std::string, crucible::safety::source::External>;

void wants_external_only(ExternalName const&);

int main() {
    crucible::CallSiteTable t;
    t.insert(
        crucible::CallSiteTable::NonZeroHash{
            crucible::CallsiteHash{uint64_t{0xC0FFEE}}},
        std::string{"file.py"},
        std::string{"f"},
        int32_t{42});

    // Reading the stored filename yields
    // `Tagged<std::string, source::Sanitized> const&`.  Passing it
    // to wants_external_only(...) requires an implicit conversion
    // from `Tagged<T, Sanitized>` to `Tagged<T, External>` — which
    // does not exist.  Must fail at the call site.
    wants_external_only(t.entries[0].filename);  // ← MUST fail
    return 0;
}
