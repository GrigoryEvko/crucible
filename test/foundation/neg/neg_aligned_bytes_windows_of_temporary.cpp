// A temporary block gives its bytes back at the end of the full expression,
// so a ViewMut of a window of it would point into bytes that no longer
// exist.  with_mut_windows takes only a named block.

#include <foundation/AlignedBytes.h>

int main() {
    namespace core = ::foundation::core;
    core::FixedList<::foundation::ByteWindow, 1> windows{};
    static_cast<void>(windows
                          .append(::foundation::ByteWindow{core::side_as<core::side::WindowStart>(core::ByteCount{0}),
                                                           core::side_as<core::side::WindowLength>(core::ByteCount{0})})
                          .expect("the list holds one window"));
    auto const given =
        ::foundation::with_mut_windows<float>(::foundation::AlignedBytes{}, windows, [](auto) noexcept {});
    return given.is_ok() ? 0 : 1;
}
