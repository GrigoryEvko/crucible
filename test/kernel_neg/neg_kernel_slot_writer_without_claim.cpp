// Negative-compile test.  This file must fail to compile.
//
// A slot of the kernel cache gives its one writer only with the claim that won
// the slot.  A door that took any token for a writer would give one slot two
// writers, and a reader would find the row of one insert with the kernel of
// the other.  So the slot has no such door.
//
// Expected diagnostic: the slot has no member writer.

#include <crucible/KernelCache.h>

#include <foundation/Linear.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace {

struct CompileTag {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    crucible::KernelCache::KernelCacheSlot slot{};
    auto writer = slot.writer(::foundation::permissions::mint_permission_root<CompileTag>());
    ::foundation::linear::drop(std::move(writer));
    return 0;
}
