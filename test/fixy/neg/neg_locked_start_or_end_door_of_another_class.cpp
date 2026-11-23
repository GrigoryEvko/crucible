// A class that the tag of a Locked does not name calls the door of the lock
// with no context.  That door blocks with no Block in the type of its
// caller, so only the owner that the tag names in start_or_end_owner calls
// it.  The door is private, and only that owner is its friend.

#include <fixy/os/SpinLock.h>

#include <cstdint>

namespace {

class Registry;

struct RegistryTag final {
    using start_or_end_owner = Registry;
};

class Registry final {
public:
    void add() noexcept {
        locked_.with_at_owner_start_or_end([](std::uint64_t& count) noexcept { ++count; });
    }
    ::fixy::spin::BlockingLocked<std::uint64_t, RegistryTag> locked_{};
};

// Another class calls the door of the lock of the registry.
struct Intruder final {
    static void add(Registry& registry) noexcept {
        registry.locked_.with_at_owner_start_or_end([](std::uint64_t& count) noexcept { ++count; });
    }
};

}  // namespace

int main() {
    Registry registry{};
    registry.add();
    Intruder::add(registry);
    return 0;
}
