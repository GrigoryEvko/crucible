// A unique strided view is the one writer of its elements, so no two of its
// indexes name one element.  mint_strided_view_mut refuses a view with an
// overlap, and the constructor from a plain view is private.  The key of the
// public constructor from a plain view, which each unit of the base could
// reach, made a unique view of any plain view, such as a view with a stride
// of zero.  That key is gone.

#include <foundation/core/Strided.h>

#include <cstddef>

namespace forge {
// The stand-in for the plain view that a forger gives.  The compiler refuses
// the construction, so the fixture does not link, and the stand-in has no
// body.
template <class T>
T&& rvalue() noexcept;

// The brand of the forged unique view.  It is empty, which is all that
// foundation::brand::IsBrand asks of a brand.
struct brand {};
}  // namespace forge

namespace core = ::foundation::core;
using Plain = core::StridedView<int, 2>;
using Unique = core::StridedViewMut<int, 2, forge::brand>;

[[nodiscard]] static std::size_t forged_count() noexcept {
    Unique const forged{core::detail::StridedDoor::key(), forge::rvalue<Plain>()};
    return forged.count();
}

int main() { return static_cast<int>(forged_count()); }
