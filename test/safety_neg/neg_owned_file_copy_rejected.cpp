// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-032 HS14 fixture #1: OwnedFile copy ctor rejected.
//
// OwnedFile owns a `std::FILE*` exclusively — copying the wrapper
// would alias the underlying FILE* and trigger double-fclose on
// destruction.  The wrapper deletes its copy ctor with a structured
// reason string ("FILE* is unique; copy would double-close on
// destruction"); attempting to copy-construct fails substitution.
//
// Pre-V-032 TraceLoader used a raw `std::FILE*` with explicit
// fclose() on every error-return path; the leak bug class was an
// added early-return that forgot the fclose().  This fixture pins
// the RAII guarantee at the type level: a future hand-rolled copy
// can't be slipped in without first deleting the `delete("reason")`
// declaration in safety/OwnedFile.h.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "use of deleted function" / "deleted" / "copy" / "double-close".

#include <crucible/safety/OwnedFile.h>

#include <cstdio>

int main() {
    // Open /dev/null — guaranteed-available on Linux per CLAUDE.md §XIV.
    ::crucible::safety::OwnedFile original{std::fopen("/dev/null", "rb")};

    // Should FAIL: copy ctor is deleted; OwnedFile is exclusive.
    [[maybe_unused]] ::crucible::safety::OwnedFile alias{original};
    return 0;
}
