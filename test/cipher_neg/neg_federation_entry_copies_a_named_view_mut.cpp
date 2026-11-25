// Negative compile test.  This file must fail to compile.
//
// Fixture of serialize_federation_entry of
// crucible/cipher/FederationProtocol.h.
//
// Premise: serialize_federation_entry takes the ViewMut of a frame by value,
// so the call is the one writer of the run.  A call with the named ViewMut of
// the body copies it, and a copy of a ViewMut is a second writer of the run.
// The copy constructor of ViewMut is deleted, so the call does not compile.
// The call with std::move(run) compiles (test_federation_protocol).

#include <crucible/cipher/FederationProtocol.h>

#include <cstddef>

int main() {
    auto [buffer, buffer_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<std::byte, 64>);
    buffer.with_mut(buffer_key, [](auto run) noexcept {
        namespace fed = ::crucible::cipher::federation;
        // Must fail: the call copies the named ViewMut.
        auto written =
            fed::serialize_federation_entry(run, fed::FederationKey::sentinel(), ::fixy::View<std::byte const>{});
        static_cast<void>(written);
    });
    return 0;
}
