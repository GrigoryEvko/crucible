// A word protocol states the word of a stated payload of
// foundation/core/Atomic.h with a constant that is not a niche_statement.
// The gate of the protocol refuses the statement, so the cell of the value
// does not compile.

#include <foundation/core/Atomic.h>

#include <cstdint>

class CountedSeat {
    std::uint32_t number_ = 1;
    constexpr explicit CountedSeat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<CountedSeat>;

public:
    constexpr CountedSeat(CountedSeat const&) noexcept = default;
    constexpr CountedSeat& operator=(CountedSeat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] static constexpr CountedSeat of(std::uint32_t number) noexcept { return CountedSeat{number}; }
};

template <>
struct foundation::core::atomic_word<CountedSeat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    static constexpr int is_empty_word_of_niche = 1;
    [[nodiscard]] static constexpr word_type to_word(CountedSeat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr CountedSeat from_word(atomic_word_key<CountedSeat> const&, word_type word) noexcept {
        return CountedSeat{word};
    }
};

int main() { return sizeof(::foundation::core::Atomic<CountedSeat>) > 0 ? 0 : 1; }
