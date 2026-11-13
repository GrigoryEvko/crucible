// A Hole owns its storage alone, so a copy is refused: two Holes would give
// the storage back two times.

#include <foundation/core/Credit.h>
#include <foundation/effects/Effect.h>

#include <utility>

int main() {
    auto hole = foundation::core::mint_hole<8, 8>(foundation::effects::Alloc{});
    auto copy = hole;
    std::move(copy).consume();
    std::move(hole).consume();
    return 0;
}
