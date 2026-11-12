// == compares two Records of one type only, also when each part of one
// Record compares with the part of the other.

#include <foundation/core/Record.h>

int main() {
    ::foundation::core::Record<int, int> const left{1, 2};
    ::foundation::core::Record<long, long> const right{1, 2};
    return left == right ? 0 : 1;
}
