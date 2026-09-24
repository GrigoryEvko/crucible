// Writing a new payload through a mutable reference keeps the current
// grade.  A payload that used more than the one it replaced then carries
// the smaller claim.  There is no mutable accessor.

#include <fixy/Budgeted.h>

int main() {
    fixy::Budgeted<int> measured = fixy::Budgeted<int>::unbounded(1);
    measured.peek_mut() = 2;
    return measured.peek();
}
