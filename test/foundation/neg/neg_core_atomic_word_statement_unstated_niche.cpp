// A word protocol states that only the empty value of the niche gives the
// empty word, but the niche of the value states nothing, so a door can build
// a value whose word is the empty word.  The gate of the protocol refuses the
// statement (the word of a stated payload of foundation/core/Atomic.h), so
// the cell of an Option of the value does not compile.

#include <foundation/core/Atomic.h>

#include <cstdint>

class UnstatedSeat {
    std::uint32_t number_ = 1;
    constexpr explicit UnstatedSeat(std::uint32_t number) noexcept : number_{number} {}
    friend struct ::foundation::core::atomic_word<UnstatedSeat>;

public:
    constexpr UnstatedSeat(UnstatedSeat const&) noexcept = default;
    constexpr UnstatedSeat& operator=(UnstatedSeat const& other) noexcept {
        number_ = other.number_;
        return *this;
    }
    [[nodiscard]] static constexpr UnstatedSeat of(std::uint32_t number) noexcept { return UnstatedSeat{number}; }
    [[nodiscard]] static constexpr UnstatedSeat
    niche_empty(::foundation::core::niche_key<UnstatedSeat> const&) noexcept {
        return UnstatedSeat{0};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<UnstatedSeat> const&) const noexcept {
        return number_ == 0;
    }
};

template <>
struct foundation::core::atomic_word<UnstatedSeat> {
    using word_type = std::uint32_t;
    static constexpr word_type empty_word = 0;
    static constexpr ::foundation::core::niche_statement is_empty_word_of_niche{};
    [[nodiscard]] static constexpr word_type to_word(UnstatedSeat const& seat) noexcept { return seat.number_; }
    [[nodiscard]] static constexpr UnstatedSeat from_word(atomic_word_key<UnstatedSeat> const&,
                                                          word_type word) noexcept {
        return UnstatedSeat{word};
    }
};

int main() { return sizeof(::foundation::core::Atomic<::foundation::core::Option<UnstatedSeat>>) > 0 ? 0 : 1; }
