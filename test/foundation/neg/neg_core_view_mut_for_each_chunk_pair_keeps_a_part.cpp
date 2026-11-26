// The body of for_each_chunk_pair captures by reference an Option of a View.
// The door gives the rest of the first run back to the caller as its one
// writer, so a View of a part that the body kept there would read a run that
// another name writes after the walk.  The door refuses a body that can store
// a borrow outside the walk (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

#include <utility>

int main() {
    ::foundation::core::FixedArray<int, 8> const source{};
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 8>);
    array.with_mut(key, [&source](auto out) noexcept {
        ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
        auto rests = ::foundation::core::for_each_chunk_pair<4>(std::move(out),
                                                                ::foundation::core::View<int const>{source.view()},
                                                                [&kept](auto part, auto other) noexcept {
                                                                    static_cast<void>(part);
                                                                    static_cast<void>(other);
                                                                    static_cast<void>(kept.is_some());
                                                                });
        static_cast<void>(rests);
    });
    return 0;
}
