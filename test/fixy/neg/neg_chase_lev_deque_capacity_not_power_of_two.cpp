// The deque maps a position to a cell with a mask, which is the index
// modulo Capacity only when Capacity is a power of two.  Twelve is not,
// so the class refuses it.

#include <fixy/concurrent/ChaseLevDeque.h>

int main() {
    [[maybe_unused]] ::fixy::concurrent::ChaseLevDeque<int, 12> deque{};
    return 0;
}
