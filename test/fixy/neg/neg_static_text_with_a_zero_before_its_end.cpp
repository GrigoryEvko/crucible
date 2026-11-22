// Negative compile test.  This file must fail to compile.
//
// A reader of a StaticText stops at the first zero, so a literal with a zero
// before its end would lose the characters after that zero.  The literal
// form is consteval, so such a literal stops the build.
//
// Expected diagnostic: the call of static_text_holds_a_zero_before_its_end.

#include <foundation/core/StaticText.h>

constexpr ::foundation::core::StaticText kText{"ab\0c"};

int main() { return static_cast<int>(kText.view().size()); }
