// bool is not a number: compare of a bool and an integer does not compile.

#include <foundation/core/Compare.h>

int main() { return ::foundation::core::compare(true, 1) == 0 ? 0 : 1; }
