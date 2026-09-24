// I002: classified x a returned failure whose error type is not Secret.
//
// The binding names no Security atom, so it is classified by the strict
// pole, and the error value leaves it in the clear.  The pack trips I002
// alone.

#include <fixy/Fn.h>
#include <fixy/Secret.h>

#include <expected>

struct fault final {};

int main() {
    [[maybe_unused]] ::fixy::fn<std::expected<int, fault>> refused{};
    return 0;
}
