// == of a Record exists only when each part has == that gives bool.

#include <foundation/core/Record.h>

struct NoEquality {
    int value = 0;
};

int main() {
    ::foundation::core::Record<int, NoEquality> const left{};
    ::foundation::core::Record<int, NoEquality> const right{};
    return left == right ? 0 : 1;
}
