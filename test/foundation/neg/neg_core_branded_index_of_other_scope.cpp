// An index of one scope is no index of a different scope, because each scope
// has its own brand: the outer index can be past the count of the inner view.
// The bodies give their return types, so only the read of the inner view
// fails.

#include <foundation/core/Branded.h>

int main() {
    auto [storage, storage_key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>, ::foundation::core::FixedArray<int, 4>{1, 2, 3, 4});
    return storage.with_mut(storage_key, [](auto values) noexcept -> int {
        return ::foundation::core::mint_branded_scope(values.view(), [&values](auto outer) noexcept -> int {
            auto const last = outer.index(3).expect("3 is below the count");
            return ::foundation::core::mint_branded_scope(
                values.view().window(0, 1).expect("one element fits"),
                [last](auto inner) noexcept -> int { return inner.at(last); });
        });
    });
}
