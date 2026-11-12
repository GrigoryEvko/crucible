// bit_cast takes only a To of which each bit pattern is a value: the empty
// value of a niche is no value, so bit_cast to a class that declares a niche
// does not compile.

#include <foundation/core/Scalar.h>

#include <cstdint>

struct NicheWord {
    std::uint32_t value = 0;

    [[nodiscard]] static constexpr NicheWord niche_empty(::foundation::core::niche_key<NicheWord> const&) noexcept {
        return NicheWord{0xFFFFFFFFu};
    }
    [[nodiscard]] constexpr bool is_niche_empty(::foundation::core::niche_key<NicheWord> const&) const noexcept {
        return value == 0xFFFFFFFFu;
    }
};

int main() { return ::foundation::core::bit_cast<NicheWord>(std::uint32_t{0xFFFFFFFFu}).value == 0 ? 0 : 1; }
