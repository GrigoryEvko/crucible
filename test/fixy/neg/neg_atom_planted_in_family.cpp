// An atom planted in a shipped family from another file.
//
// The namespace is a family of the catalog and it holds its seal, so the
// first two reads of IsAtom admit the type.  The third read compares the
// file of the declaration with the file of the seal.  This file is not
// fixy/atoms/Sync.h, so tier 2 refuses the pack.  The plant comes after
// the family header, so the roster walk of that header does not see it,
// and the refusal is the gate itself.

#include <fixy/Fn.h>
#include <fixy/atoms/Sync.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture plants a declaration in a base namespace, and a check refuses it")
namespace fixy::atom::sync {
struct planted_wait final : atom_of<Axis::Synchronization> {};
}  // namespace fixy::atom::sync
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::sync::planted_wait> refused{};
    return 0;
}
