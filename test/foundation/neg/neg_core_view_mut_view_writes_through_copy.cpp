// A body copies the View of its ViewMut and writes an element through the
// copy.  The View of a ViewMut reads and does not write, so no copy of it is a
// second writer of the run.  get_mut gives the writer of an element.

#include <foundation/core/Array.h>

int main() {
    ::foundation::core::FixedArray<int, 4> array{1, 2, 3, 4};
    array.with_mut([](auto run) noexcept {
        auto const first = run.view();
        auto second = first;
        second.at(::foundation::core::index<0>()) = 5;
    });
    return 0;
}
