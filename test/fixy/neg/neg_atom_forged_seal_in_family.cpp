// A seal and an atom forged in a shipped family from another file.
//
// This unit does not include fixy/concurrent/Spawn.h, so the spawn family
// holds no seal here.  The unit declares the seal and an atom in one file,
// so the namespace holds one seal and the atom is in the file of that seal.
// The list of fixy/Atom.h names concurrent/Spawn.h as the header of the
// family, and this file is not that header, so tier 2 refuses the pack.

#include <fixy/Fn.h>

namespace fixy::atom::spawn {
inline constexpr atom_seal atom_namespace_seal{};
struct forged_spawn final : atom_of<Axis::Protocol> {};
}  // namespace fixy::atom::spawn

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::spawn::forged_spawn> refused{};
    return 0;
}
