// The set after a step is perm_set_union_t of the set that stays and the
// set that the step gains.  This file tries to give a union a tag that no
// operand holds: it writes a specialization of the alias.  An alias
// template has no specialization, so the file does not compile.

#include <foundation/permissions/PermSet.h>

namespace user_code {
struct Alpha {};
struct Beta {};
}  // namespace user_code

namespace foundation::permissions {
template <>
struct perm_set_union_t<PermSet<user_code::Alpha>, PermSet<user_code::Beta>> {};
}  // namespace foundation::permissions

int main() { return 0; }
