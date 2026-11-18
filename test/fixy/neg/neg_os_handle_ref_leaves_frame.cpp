// An arm of a frame door gives the borrow of a handle as its result.  A borrow
// holds the address of the descriptor of its handle, so the frame door refuses
// it as a result that leaves the frame: the borrow could outlive its handle.

#include <fixy/os/Handle.h>
#include <foundation/core/Choice.h>

namespace os = fixy::os;

int main() {
    os::Handle<os::kind::File, os::RightSet::read> const file{};
    ::foundation::core::Option<int> option = ::foundation::core::some(1);
    [[maybe_unused]] auto kept =
        option.with_mut([&file](auto) noexcept { return file.ref(); }, [&file]() noexcept { return file.ref(); });
    return 0;
}
