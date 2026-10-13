// A linear wrapper built from bytes.  A Linear<int> whose special members
// are all trivial is trivially copyable, and std::bit_cast builds it from
// an int with no call of mint_linear.  The move assignment of the wrapper
// is user-provided, so the wrapper is not trivially copyable, and
// std::bit_cast has no candidate.  The move constructor stays trivial, so
// the ABI still passes the wrapper in a register.

#include <fixy/Qtt.h>

#include <bit>

int main() {
    auto forged = std::bit_cast<::fixy::Linear<int>>(0);
    (void)forged;
    return 0;
}
