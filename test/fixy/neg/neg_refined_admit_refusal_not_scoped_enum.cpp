// The refusal of the fallible door is a scoped enum, so an error code
// names its reason and a bare integer does not pass for one.

#include <fixy/Refined.h>

int main() {
    auto refused = fixy::admit_refined<fixy::positive>(0, 7);
    return refused.has_value() ? 0 : 1;
}
