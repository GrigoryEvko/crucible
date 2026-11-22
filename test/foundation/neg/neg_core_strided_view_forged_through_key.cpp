// A strided view of raw parts checks nothing.  The null pointer with a size
// of 1000 is a view whose elements lie in no run, and each read through it is
// a read past its run.  The constructor from parts is private, and only the
// two mints and the operations of a view call it.  The key of the public
// constructor from parts, which each unit of the base could reach, is gone.

#include <foundation/core/Strided.h>

#include <cstddef>

namespace core = ::foundation::core;

[[nodiscard]] static std::size_t forged_count() noexcept {
    core::StridedView<int, 1> const forged{core::detail::StridedDoor::key(), nullptr, {{1000}, {1}}};
    return forged.count();
}

int main() { return static_cast<int>(forged_count()); }
