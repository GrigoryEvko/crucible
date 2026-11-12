// A partial specialization of the niche protocol for each pointer type, in a
// file outside the base, gives an empty value to types that no file of the
// test defines.  The argument T* has no head that a file defines, so no file
// owns it, and the rule specialization-home refuses the specialization.

#include <foundation/core/Choice.h>

template <class T>
struct foundation::core::niche<T*> {
    [[nodiscard]] static constexpr T* empty(niche_key<T*> const&) noexcept { return nullptr; }
    [[nodiscard]] static constexpr bool is_empty(niche_key<T*> const&, T* const& value) noexcept {
        return value == nullptr;
    }
};

int main() { return 0; }
