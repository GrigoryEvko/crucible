// A round takes a float only.  A double rounded to float and then to a
// storage format rounds two times, so a double does not compile.

#include <fixy/Core.h>

int main() {
    double const value = 0.1;
    return fixy::round_nonsaturating<fixy::bf16>(value).raw() == 0 ? 1 : 0;
}
