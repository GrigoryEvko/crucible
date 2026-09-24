// Sentinel TU for foundation/Pinned.h.  The mixin deletes copy and
// move on the derived type, carries no bytes of its own, and NonMovable
// is the same class under its second name.

#include <foundation/Pinned.h>

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace {

// The address is the identity: another thread reads the head through
// a pointer it took at construction.
struct PinnedRing : ::foundation::Pinned<PinnedRing> {
    alignas(64) std::atomic<std::uint64_t> head{0};
};

// The handle is the identity: a moved-from shell would read as a live
// descriptor.
struct OwnedHandle : ::foundation::NonMovable<OwnedHandle> {
    std::uint64_t handle_id = 0;
};

static_assert(!std::is_copy_constructible_v<PinnedRing>);
static_assert(!std::is_copy_assignable_v<PinnedRing>);
static_assert(!std::is_move_constructible_v<PinnedRing>);
static_assert(!std::is_move_assignable_v<PinnedRing>);
static_assert(std::is_default_constructible_v<PinnedRing>);

static_assert(!std::is_copy_constructible_v<OwnedHandle>);
static_assert(!std::is_copy_assignable_v<OwnedHandle>);
static_assert(!std::is_move_constructible_v<OwnedHandle>);
static_assert(!std::is_move_assignable_v<OwnedHandle>);
static_assert(std::is_default_constructible_v<OwnedHandle>);

// The base carries nothing, so a derived handle keeps the size of its
// own members.
static_assert(std::is_empty_v<::foundation::Pinned<PinnedRing>>);
static_assert(std::is_empty_v<::foundation::NonMovable<OwnedHandle>>);
static_assert(sizeof(OwnedHandle) == sizeof(std::uint64_t));

// One class, two names.
static_assert(std::is_same_v<::foundation::NonMovable<OwnedHandle>, ::foundation::Pinned<OwnedHandle>>);

// The base is a template on the derived type, so two derived types do
// not share a base.
static_assert(!std::is_same_v<::foundation::Pinned<PinnedRing>, ::foundation::Pinned<OwnedHandle>>);

}  // namespace

int main() {
    PinnedRing ring{};
    ring.head.store(7, std::memory_order_release);
    if (ring.head.load(std::memory_order_acquire) != 7) return 1;

    OwnedHandle handle{};
    handle.handle_id = 42;
    if (handle.handle_id != 42) return 2;

    return 0;
}
