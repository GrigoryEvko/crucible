// A Ref of an element of a temporary SegmentList would name storage that the
// end of the full expression frees, so get() of an rvalue is refused.

#include <foundation/core/SegmentList.h>

int main() { return ::foundation::core::SegmentList<int>{}.get(0).is_some() ? 1 : 0; }
