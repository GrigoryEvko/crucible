// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A registry of the transfer facts of classes that are only declared counts
// its statements in its seal (THE OPAQUE TYPES of foundation/Transfer.h).  A
// statement that the seal does not count stands outside the seal, and the
// first read of the registry stops the build.  So no translation unit reads a
// statement that another one does not see.

#include <foundation/Transfer.h>

namespace sealed_registry {

struct Handle;
struct OtherHandle;

namespace transfer_facts {
// No thread writes a handle after its build.
inline constexpr ::foundation::transfer::opaque_facts handle{
    .type = ^^Handle, .is_sendable = true, .is_shareable = true};
inline constexpr ::foundation::transfer::opaque_seal seal{.count = 1};
// A statement that a different header adds after the seal.
inline constexpr ::foundation::transfer::opaque_facts other_handle{
    .type = ^^OtherHandle, .is_sendable = true, .is_shareable = true};
}  // namespace transfer_facts

struct ReadsHandle {
    Handle const& handle;
};

}  // namespace sealed_registry

static_assert(::foundation::transfer::Sendable<sealed_registry::ReadsHandle>);

int main() { return 0; }
