// A word protocol whose from_word takes no key.  Each caller could then make
// a seat from a word, so the gate refuses the protocol, and no cell holds a
// seat.

#include <foundation/core/Atomic.h>

#include <cstdint>

namespace {

class Seat {
    std::uint32_t number_ = 1;
    constexpr explicit Seat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<Seat>;

public:
    constexpr Seat(Seat const&) noexcept = default;
    constexpr Seat& operator=(Seat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
};

}  // namespace

template <>
struct foundation::core::atomic_word<Seat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    [[nodiscard]] static constexpr word_type to_word(Seat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr Seat from_word(word_type word) noexcept { return Seat{word}; }
};

int main() {
    static_cast<void>(::foundation::core::Atomic<Seat>{}.load_acquire());
    return 0;
}
