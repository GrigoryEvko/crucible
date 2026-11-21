// A move of a window loan out of its scope.  The loan would end after a loan
// that opened later, so the order of the ends would not be the reverse of the
// order of the opens.  A loan does not move.

#include <foundation/core/WindowStack.h>

#include <utility>

int main() {
    ::foundation::core::WindowStack<int, 4> stack{};
    ::foundation::core::WindowLoan<int, 4> loan{stack, ::foundation::core::WindowCount{2}};
    ::foundation::core::WindowLoan<int, 4> const taken{std::move(loan)};
    return taken.is_open() ? 0 : 1;
}
