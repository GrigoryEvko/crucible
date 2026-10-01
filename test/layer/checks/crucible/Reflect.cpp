// The compile-time checks of crucible/Reflect.h.

#include <crucible/Reflect.h>

namespace crucible {

namespace detail_reflect {

static_assert(IsReflectFieldSupported<int>, "integral types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<double>, "floating-point types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<void*>, "pointer types must satisfy IsReflectFieldSupported");
static_assert(IsReflectFieldSupported<int[4]>, "C array types must satisfy IsReflectFieldSupported");
struct ReflectFieldSentinel {
    int x;
};
static_assert(IsReflectFieldSupported<ReflectFieldSentinel>,
              "class types must satisfy IsReflectFieldSupported, which is what closes the recursion");

static_assert(!IsReflectFieldSupported<void>, "void is not a reflectable type category");
static_assert(!IsReflectFieldSupported<int(int)>, "function types are not reflectable, but function pointers are");

}  // namespace detail_reflect

}  // namespace crucible
