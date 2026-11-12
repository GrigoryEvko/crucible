// at() of an rvalue Record does not compile, because a move of one part
// drops the others.  A binding of the moved Record gives out each part.

#include <foundation/core/Index.h>
#include <foundation/core/Record.h>

#include <utility>

int main() {
    ::foundation::core::Record<int, long> pair{1, 2};
    std::move(pair).at(::foundation::core::index<0>());
    return 0;
}
