// The compile-time checks of fixy/handle/Once.h.

#include <fixy/handle/Once.h>

namespace fixy::handle {

static_assert(sizeof(SetOnce<int>) == sizeof(int*));
static_assert(sizeof(SetOnce<void>) == sizeof(void*));

}  // namespace fixy::handle
