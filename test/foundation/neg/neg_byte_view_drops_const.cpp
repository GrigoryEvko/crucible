// The bytes of a const run as bytes that can be written.  The const of the
// run stays on the bytes, so no write reaches a run that only reads.

#include <foundation/core/Region.h>

#include <cstddef>
#include <cstdint>

namespace {

void write_through(::foundation::core::View<std::uint8_t const> run) {
    ::foundation::core::View<std::byte> const bytes = ::foundation::core::byte_view(run);
    static_cast<void>(bytes);
}

}  // namespace

int main() {
    write_through(::foundation::core::View<std::uint8_t const>{});
    return 0;
}
