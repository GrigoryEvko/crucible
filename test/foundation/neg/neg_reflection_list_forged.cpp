// A ReflectionList built from a pointer and a count of the caller.  The
// count could pass the array, and the pointer could name storage that is
// not static.  The constructor is private, and reflection_list is its one
// door.

#include <foundation/reflect/ReflectionList.h>

int main() {
    constexpr ::foundation::reflect::ReflectionList forged{nullptr, 3};
    return forged.size() == 3 ? 0 : 1;
}
