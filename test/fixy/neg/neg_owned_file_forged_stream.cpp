// OwnedFile's destructor calls std::fclose on whatever stream it holds.
// A public constructor over a FILE* would therefore do two things at
// once: it would let a caller claim a stream it never opened, and it
// would let the caller name any stream in the process for closing on
// scope exit.  stdin, or a stream another handle already owns, would
// both read as a file.  If this TU compiled and ran, it would close
// standard input.
//
// This fixture is the standing witness on the direct route.  The
// constructor is private, and OwnedFileDoor performs the open for the two
// mints, so a handle exists only over a stream libc returned.  Its
// sibling, neg_owned_file_forged_through_mint_linear.cpp, covers the
// route that goes through the generic Linear forwarder.

#include <fixy/OwnedFile.h>

#include <cstdio>

int main() {
    fixy::OwnedFile forged{stdin};
    return forged.is_open() ? 0 : 1;
}
