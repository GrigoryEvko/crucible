// OwnedFile's destructor calls std::fclose on whatever stream it holds.
// A public constructor over a FILE* therefore did two things at once: it
// let a caller claim a stream it never opened, and it let the caller
// name any stream in the process for closing on scope exit.  stdin, or
// a stream another handle already owns, both read as a file.
//
// This fixture is the standing witness on the direct route.  The
// constructor is private and the two static doors perform the open
// themselves, so a handle exists only over a stream libc returned.  Its
// sibling, neg_owned_file_forged_through_mint_linear.cpp, covers the
// route that goes through the generic Linear forwarder.
//
// Verified before the fix: this TU compiled clean against the public
// constructor.  Run, it would have closed standard input.

#include <fixy/OwnedFile.h>

#include <cstdio>

int main() {
    fixy::OwnedFile forged{stdin};
    return forged.is_open() ? 0 : 1;
}
