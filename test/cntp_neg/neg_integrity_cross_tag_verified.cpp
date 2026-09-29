// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Externally-tagged bytes are not integrity-verified bytes.  The receiver
// must unwrap a matching hash first.

#include <crucible/cntp/Integrity.h>
#include <fixy/Qtt.h>
#include <fixy/Tagged.h>

#include <array>
#include <cstddef>
#include <utility>

using Payload = std::array<std::byte, 8>;

void requires_verified(crucible::cntp::IntegrityVerifiedPayload<Payload> payload) noexcept { (void)payload; }

int main() {
    auto external = ::fixy::mint_tagged<::fixy::tags::source::External>(::fixy::mint_linear<Payload>(Payload{}));
    requires_verified(std::move(external));
    return 0;
}
