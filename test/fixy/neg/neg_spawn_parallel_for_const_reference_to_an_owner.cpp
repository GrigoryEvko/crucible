// The body of a parallel loop captures a const reference to the keyed owner
// whose frame lends the run of the region (rule 9 of foundation/Closure.h).
// The owner gives the View of the run inside the shard, so a shard would read
// an element that a second shard writes.  The owner is an input of reads(),
// which takes the compare of bytes.

#include "../parallel_loop_rig.h"

#include <foundation/core/KeyedMint.h>

namespace rig = ::parallel_loop_rig;

int main() {
    auto [array, array_key] = ::fixy::mint_keyed_owner(::fixy::fixed_array_owner<rig::Word, rig::kCount>);
    array.with_mut(array_key, [&owner = std::as_const(array)](auto lent) noexcept {
        static_cast<void>(::fixy::with_region<rig::Written>(std::move(lent), [&owner](auto region) noexcept {
            return rig::loop_over(std::move(region), [&owner](auto shard, auto) noexcept {
                for (rig::Word& element : shard) {
                    element = owner.at(::fixy::index<0>());
                }
            });
        }));
    });
    return 0;
}
