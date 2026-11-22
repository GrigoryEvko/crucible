// A move of a refined member whose payload clears its source on a move.
//
// The payload holds a word, and its move constructor writes zero into the
// source, as an owner does.  A move of the refined member would leave the
// holder with a refinement of non_zero_word that holds zero, and value() of
// the holder would give it.  The move of such a refinement is deleted.

#include <fixy/Refined.h>

#include <cstdint>

namespace {

class Word {
    std::uint32_t value_ = 0;

public:
    constexpr explicit Word(std::uint32_t value) noexcept : value_{value} {}
    constexpr Word(Word const& other) noexcept = default;
    constexpr Word(Word&& other) noexcept : value_{other.value_} { other.value_ = 0; }
    constexpr Word& operator=(Word const& other) noexcept = default;
    constexpr Word& operator=(Word&& other) noexcept {
        value_ = other.value_;
        other.value_ = 0;
        return *this;
    }
    [[nodiscard]] constexpr std::uint32_t raw() const noexcept { return value_; }
};

struct IsNonZeroWord {
    constexpr bool operator()(Word const& word) const noexcept { return word.raw() != 0; }
};

inline constexpr IsNonZeroWord non_zero_word{};

using NonZeroWord = ::fixy::Refined<non_zero_word, Word>;

struct Holder {
    NonZeroWord word;
};

}  // namespace

int main() {
    Holder holder{::fixy::mint_refined<non_zero_word>(Word{5})};
    NonZeroWord const taken = static_cast<NonZeroWord&&>(holder.word);
    return static_cast<int>(holder.word.value().raw() + taken.value().raw());
}
