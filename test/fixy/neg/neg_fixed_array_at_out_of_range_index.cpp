// The compile-time accessor carries its bound in a requires clause, so
// an index one past the end is a compile error rather than a read of
// the neighbouring object.  This is the tier that needs no proof token,
// because the constraint has already decided.

#include <fixy/FixedArray.h>

int main() {
    fixy::FixedArray<int, 4> a{std::in_place, 1, 2, 3, 4};
    return a.at<4>();
}
