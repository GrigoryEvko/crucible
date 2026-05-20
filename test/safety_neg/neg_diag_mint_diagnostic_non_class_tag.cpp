// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-U-109 fixture #1 for safety::diag::mint_diagnostic
// (Diagnostic.h:1476).  The factory's sole gate is
// `requires is_diagnostic_class_v<Tag>` where
//   is_diagnostic_class_v<T> =
//       std::is_base_of_v<tag_base, T> && !std::is_same_v<T, tag_base>.
// A FUNDAMENTAL type such as `int` is not a class, so
// `is_base_of_v<tag_base, int>` is false and the requires-clause
// removes the factory from the candidate set.
//
// Distinct from neg_diag_non_tag_in_diagnostic_wrapper.cpp (which
// instantiates the WRAPPER template `Diagnostic<int, ...>`); this
// fixture calls the MINT FACTORY, exercising the §XXI authorization
// point rather than the type's own constraint.
//
// Distinct mismatch class from
// neg_diag_mint_diagnostic_non_tag_base_class.cpp (#2): there the
// Tag IS a class but does not derive from tag_base; here the Tag is
// not a class type at all.
//
// Expected diagnostic: "constraints not satisfied" /
// "is_diagnostic_class_v" / "no matching function" / "mint_diagnostic".

#include <crucible/safety/Diagnostic.h>

int main() {
    // `int` is not a class → is_diagnostic_class_v<int> is false →
    // the requires-clause on mint_diagnostic fails.
    auto d = crucible::safety::diag::mint_diagnostic<int>();
    (void)d;
    return 0;
}
