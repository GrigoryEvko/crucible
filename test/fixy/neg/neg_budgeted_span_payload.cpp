// A span payload is refused.  The budget measured what producing the
// span used, and the elements it points at can be replaced afterwards by
// a producer that used more.

#include <fixy/Budgeted.h>

#include <span>

int main() {
    static int storage[4] = {1, 2, 3, 4};
    fixy::Budgeted<std::span<int const>> const window{std::span<int const>{storage}, fixy::BitsBudget{8}, fixy::PeakBytes{16}};
    return static_cast<int>(window.peek().size());
}
