// IsJoinMechanismTag is an identity allowlist, not a structural check.  A
// struct that declares the same mechanism member is still not one of the
// six declared tags, so a caller cannot substitute its own type.

#include <fixy/os/Spawn.h>

namespace join = fixy::spawn::join;

namespace {
struct MechanismImposter {
    // Marked used so the only diagnostic this fixture produces is the
    // refusal it is about.  The harness runs in strict single-error mode.
    [[maybe_unused]] static constexpr join::JoinMechanism mechanism = join::JoinMechanism::Detached;
};
}  // namespace

static_assert(join::IsJoinMechanismTag<MechanismImposter>,
              "a struct that merely declares a mechanism member must not pass for a declared tag");

int main() { return 0; }
