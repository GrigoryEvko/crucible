// mint_masked_array builds each element with its default constructor.  An
// element that has no default constructor fails the gate.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

struct NoDefault {
    int value = 0;
    explicit NoDefault(int initial) noexcept : value{initial} {}
};

int main() {
    auto items = ::foundation::core::mint_masked_array<NoDefault>(::foundation::effects::Alloc{}, 4);
    return static_cast<int>(items.size());
}
