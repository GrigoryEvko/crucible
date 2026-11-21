// A copy of a window loan.  The copy and the source would each give the
// window back at their end, so the top would go back two times.  A loan does
// not copy.

#include <foundation/core/WindowStack.h>

int main() {
    ::foundation::core::WindowStack<int, 4> stack{};
    ::foundation::core::WindowLoan<int, 4> const loan{stack, ::foundation::core::WindowCount{2}};
    ::foundation::core::WindowLoan<int, 4> const copy{loan};
    return copy.is_open() ? 0 : 1;
}
