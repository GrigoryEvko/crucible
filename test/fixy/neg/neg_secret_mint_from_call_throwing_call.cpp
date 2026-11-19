// The mint of a Secret from a call does not throw, so the call that builds
// the payload must not throw either.  A call with no noexcept can throw, so
// the mint refuses it.

#include <fixy/Secret.h>

#include <cstdint>

int main() {
    auto const key = ::fixy::mint_secret_from_call([]() { return std::uint64_t{7}; });
    static_cast<void>(key);
    return 0;
}
