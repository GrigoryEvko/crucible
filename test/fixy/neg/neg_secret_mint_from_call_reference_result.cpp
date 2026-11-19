// The door of a call builds the payload of a Secret as the prvalue of the
// call.  A call that gives a reference would copy an object that stays
// outside the Secret, and nothing zeroizes that object.  So the mint
// refuses the call.

#include <fixy/Secret.h>

#include <cstdint>

int main() {
    std::uint64_t outer_key = 7;
    auto const key = ::fixy::mint_secret_from_call([&outer_key]() noexcept -> std::uint64_t& { return outer_key; });
    static_cast<void>(key);
    return 0;
}
