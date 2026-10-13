// The error of a Result can be linear, so a failed operation gives a
// resource back.  The const form of err() copies the error, and a copy of
// a linear value is a second owner.  Only the rvalue form of err() gives a
// linear error, so the caller gets it back once.  GCC names that form, the
// one candidate, in its diagnostic.

#include <fixy/Core.h>
#include <fixy/Qtt.h>

int main() {
    fixy::Result<int, fixy::Linear<int>> result = fixy::err(fixy::mint_linear<int>(3));
    fixy::Result<int, fixy::Linear<int>> const& reader = result;
    static_cast<void>(reader.err());
    return 0;
}
