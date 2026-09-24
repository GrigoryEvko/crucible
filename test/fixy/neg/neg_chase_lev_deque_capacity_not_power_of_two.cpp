// The deque maps a position to a cell with a mask, which is the index
// modulo Capacity only when Capacity is a power of two.  Twelve is not,
// so the class refuses it.

#include <fixy/concurrent/ChaseLevDeque.h>

int main() {
    ::fixy::concurrent::ChaseLevDeque<int, 12> deque{};
    return deque.empty_approx() ? 0 : 1;
}
