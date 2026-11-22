// A RunAddress is never null, so an Option of a RunAddress reads the null
// pointer as its empty value.  A RunAddress of the null pointer through the
// key of the constructor would read as an empty Option, and foreign_pointer
// would give the null pointer to a foreign reader.  The key comes only from
// the door of the two mints, whose key() is private, and each mint refuses
// the null pointer.

#include <foundation/core/DataAddress.h>

[[nodiscard]] static bool is_null_address() noexcept {
    ::foundation::core::RunAddress const forged{nullptr, ::foundation::core::detail::RunAddressDoor::key()};
    return ::foundation::core::foreign_pointer(forged) == nullptr;
}

int main() { return is_null_address() ? 1 : 0; }
