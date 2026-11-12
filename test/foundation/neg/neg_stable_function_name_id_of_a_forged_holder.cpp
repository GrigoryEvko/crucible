// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The name id of a function reads the identity of the function and the
// name of the pointer from the two template arguments of a holder.  Here a
// translation unit writes a holder by hand: the function has external
// linkage, and the pointer names a static function.  The name of the
// static function is the same in each unit that declares one, so its name
// id would be one id for different functions.  Only function_key makes a
// key, so the id refuses the holder.
//
// Expected diagnostic: the refusal text of the function key.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace forged_holder {
inline int shown(int value) { return value; }
static int hidden(int value) { return value + 1; }
inline constexpr std::uint64_t forged_name_id =
    ::foundation::reflect::stable_function_name_id(^^::foundation::reflect::detail::function_holder<^^shown, &hidden>);
}  // namespace forged_holder

int main() { return static_cast<int>(forged_holder::forged_name_id & 1U) + forged_holder::hidden(0); }
