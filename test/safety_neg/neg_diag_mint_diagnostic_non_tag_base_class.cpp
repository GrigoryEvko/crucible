// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #2 for safety::diag::mint_diagnostic
// (Diagnostic.h:1476).  The factory's sole gate is
// `requires is_diagnostic_class_v<Tag>` where
//   is_diagnostic_class_v<T> =
//       std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>.
// An arbitrary CLASS that does NOT derive from tag_base fails
// `is_base_of_v<tag_base, NotADiagTag>` → the requires-clause
// removes the factory from the candidate set.  This proves the gate
// is provenance-based (must descend from tag_base), NOT merely
// "any class type."
//
// Distinct mismatch class from
// neg_diag_mint_diagnostic_non_class_tag.cpp (#1): there the Tag was
// a fundamental type; here it is a well-formed class that simply
// lacks the tag_base lineage.
//
// Expected diagnostic: "constraints not satisfied" /
// "is_diagnostic_class_v" / "no matching function" / "mint_diagnostic".

#include <crucible/safety/Diagnostic.h>

namespace {
// A plausible-looking but unrelated class — the kind of copy-paste
// slip the gate must catch (someone names a struct "FooDiag" without
// deriving from tag_base).
struct NotADiagTag {};
}  // namespace

int main() {
    auto d = crucible::safety::diag::mint_diagnostic<NotADiagTag>();
    (void)d;
    return 0;
}
