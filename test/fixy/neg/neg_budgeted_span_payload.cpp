// A span payload is refused.  The budget measured what producing the
// span used, and the elements it points at can be replaced afterwards by
// a producer that used more.

#include <fixy/Budgeted.h>

#include <span>

int main() {
    static int storage[4] = {1, 2, 3, 4};
    auto const window = fixy::Budgeted<std::span<int const>>::unbounded(std::span<int const>{storage});
    return static_cast<int>(window.peek().size());
}
