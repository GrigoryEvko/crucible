// A Permission is reachable only through its passkey constructor, and
// only the five mints and the post-join rebuild are friended to build a
// key.  A translation unit that holds neither cannot make a token,
// however it spells the construction.

#include <foundation/permissions/Permission.h>

namespace {

struct ForgedTag {};

// The default constructor is gone: the passkey constructor is the only
// one the class declares.
[[maybe_unused]] ::foundation::permissions::Permission<ForgedTag> forged{};

}  // namespace

int main() { return 0; }
