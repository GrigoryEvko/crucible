// A move of a Sealed would write the object that a reader on another thread
// reads, so a Sealed does not move.

#include <foundation/core/Sealed.h>
#include <foundation/core/SegmentList.h>

#include <utility>

int main() {
    ::foundation::core::Sealed sealed{::foundation::core::SegmentList<int>{}};
    ::foundation::core::Sealed<::foundation::core::SegmentList<int>> const moved{std::move(sealed)};
    return static_cast<int>(moved->size());
}
