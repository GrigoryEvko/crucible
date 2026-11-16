// The op of write_each captures the ViewMut of the output by reference.
// Through the capture the op would read the run that the restrict loop
// writes beside the restrict pointer, so the door takes only an op with no
// state.

#include <foundation/core/List.h>

#include <utility>

int main() {
    ::foundation::core::FixedList<int, 4> out{};
    ::foundation::core::FixedList<int, 4> input{};
    static_cast<void>(out.push(1).expect("the list has room"));
    static_cast<void>(input.push(2).expect("the list has room"));
    out = ::foundation::core::with_mut(std::move(out), [&input](auto out_view) noexcept {
        auto const& held = out_view;
        static_cast<void>(::foundation::core::write_each(
            std::move(out_view), [&held](int const& value) noexcept { return value + (held.size() == 0 ? 1 : 0); },
            input.view()));
    });
    return 0;
}
