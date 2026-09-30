// A class that a function body declares prints the name of the function
// and its own name.  Two local classes of one name in one function print
// one name, so a stable id refuses a local class.

#include <foundation/reflect/Hash.h>

#include <cstdint>

int main() {
    struct Local {};
    constexpr std::uint64_t id = ::foundation::reflect::stable_type_id<Local>;
    return id == 0 ? 1 : 0;
}
