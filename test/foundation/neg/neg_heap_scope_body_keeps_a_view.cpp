// The body of a heap scope captures by reference an Option of a View, which the
// caller reads after the scope.  The scope ends with the body, so a borrow that
// the body keeps there would outlive it.  The mint refuses a body that can
// store a borrow outside the scope (StoresNoBorrowOutside of
// foundation/core/CaptureStore.h).

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/effects/Effect.h>
#include <foundation/heap/HeapScope.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    int const kept_count = ::foundation::heap::mint_heap_scope(::foundation::effects::testing::test().alloc,
                                                               [&kept](auto& scope) noexcept {
                                                                   static_cast<void>(scope);
                                                                   return kept.is_some() ? 1 : 0;
                                                               });
    return kept_count;
}
