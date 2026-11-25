// erase_brand consumes the one writer of a run.  A named ViewMut stays with
// its holder, so the holder would keep a writer of the run beside the erased
// view.  The door takes only an rvalue.

#include <foundation/core/Array.h>

#include <cstddef>
#include <utility>

int main() {
    auto [values, values_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    auto const count =
        values.with_mut(values_key, [](auto run) noexcept -> std::size_t { return ::foundation::core::erase_brand(run).size(); });
    return static_cast<int>(count);
}
