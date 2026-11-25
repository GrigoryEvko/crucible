// The object of a temporary Sealed ends with the full expression, so a
// temporary Sealed gives no object.

#include <foundation/core/Sealed.h>
#include <foundation/core/SegmentList.h>

#include <cstddef>

int main() {
    std::size_t const count = (*::foundation::core::Sealed{::foundation::core::SegmentList<int>{}}).size();
    return static_cast<int>(count);
}
