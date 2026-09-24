// declassify<Policy>() is the only way out of a Secret, so that every
// escape names a reviewed policy.  A trivially copyable Secret<int> had a
// second way out: std::bit_cast<int>(secret) read the value with no policy
// and no audit entry.  The move assignment is user-provided now, so the
// class is not trivially copyable and bit_cast refuses it as the source.

#include <fixy/Secret.h>

#include <bit>

int main() {
    auto secret = fixy::mint_secret<int>(42);
    return std::bit_cast<int>(secret);
}
