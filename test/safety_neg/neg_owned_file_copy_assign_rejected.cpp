// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-032 HS14 fixture #2: OwnedFile copy-assignment rejected.
//
// Companion to neg_owned_file_copy_rejected.cpp.  The copy-ctor
// fixture pins construction-time exclusivity; this fixture pins the
// assignment boundary.  A caller that wrote
//
//     OwnedFile a{std::fopen(p, "rb")};
//     OwnedFile b{std::fopen(q, "rb")};
//     b = a;   // FORBIDDEN — would alias *a*'s FILE* into *b*,
//              // leaking *b*'s prior fp and double-closing *a*'s.
//
// must fail at substitution time.  The copy-assignment operator is
// deleted with the same structured reason string as the copy-ctor.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" / "deleted" / "copy" / "double-close".

#include <crucible/safety/OwnedFile.h>

#include <cstdio>

int main() {
    ::crucible::safety::OwnedFile a{std::fopen("/dev/null", "rb")};
    ::crucible::safety::OwnedFile b{std::fopen("/dev/null", "rb")};

    // Should FAIL: copy-assignment is deleted; ownership is unique.
    b = a;
    return 0;
}
