// A move assignment of a refined value whose payload clears its source on a
// move.
//
// The payload holds a word, and its move assignment writes zero into the
// source.  The assignment would leave the source with a refinement of
// non_zero_word that holds zero.  The move assignment of such a refinement is
// deleted, as its move constructor is.

#include <fixy/Refined.h>

#include <cstdint>
#include <utility>

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

}  // namespace

int main() {
    NonZeroWord target = ::fixy::mint_refined<non_zero_word>(Word{3});
    NonZeroWord source = ::fixy::mint_refined<non_zero_word>(Word{4});
    target = std::move(source);
    return static_cast<int>(target.value().raw());
}
