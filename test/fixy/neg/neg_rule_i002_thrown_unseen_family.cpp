// I002: classified x a throw of an exception family the reader cannot see.
//
// The second mismatch class: ctrl::throws<> names ctrl::any_exception,
// and a family the reader cannot see is not Secret.  That is the answer
// that refuses.  The pack trips I002 alone.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ctrl::throws<>> refused{};
    return 0;
}
