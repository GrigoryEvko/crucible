// A class that is not the friend of the door of the sort takes the name and
// the shape of the stable sort in its own namespace, and calls the door to
// make a View of four elements at any address.  The friend is
// foundation::core::detail::StableSort, so the member is private here.

#include <foundation/core/Sort.h>

namespace lookalike {

template <class T, class Less>
class StableSort {
public:
    [[nodiscard]] static auto forge() noexcept {
        return ::foundation::core::SortRunDoor::fixed_run_<4>(static_cast<T*>(nullptr));
    }
};

}  // namespace lookalike

int main() {
    static_cast<void>(lookalike::StableSort<int, int>::forge());
    return 0;
}
