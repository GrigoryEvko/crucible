// The length of a classified container is classified.  Secret has no
// size() that gives the length out: the one exit for a length is
// declassify<secret_policy::LengthOnly>(), so a search finds each release.

#include <fixy/Secret.h>

#include <array>
#include <cstdint>

int main() {
    auto key = fixy::mint_secret<std::array<std::uint8_t, 16>>();
    return key.size() == 16 ? 0 : 1;
}
