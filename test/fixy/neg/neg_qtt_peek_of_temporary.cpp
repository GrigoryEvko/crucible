// A borrow of the value of a temporary Linear dangles at the end of the full
// expression.  The rvalue overload of peek() is deleted, for each grade.

#include <fixy/Qtt.h>

int main() {
    int const& dangling = fixy::mint_linear<int>(7).peek();
    return dangling;
}
