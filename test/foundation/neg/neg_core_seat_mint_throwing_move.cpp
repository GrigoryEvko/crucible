// A step ends the old payload and moves the next one into the storage.  A
// move that can throw would leave the storage with no payload, so the
// constraint of the mint refuses a payload whose move can throw.

#include <foundation/core/Seat.h>

#include <cstdint>

namespace {

struct ThrowingMove final {
    std::uint64_t word = 0;

    explicit ThrowingMove(std::uint64_t value) noexcept : word{value} {}
    ThrowingMove(ThrowingMove&& source) noexcept(false) : word{source.word} {}
};

}  // namespace

int main() {
    static_cast<void>(::foundation::core::mint_seat(ThrowingMove{1}));
    return 0;
}
