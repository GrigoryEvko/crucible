// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-083 fixture #3 - CalendarProducerId<P> is part of the bridge
// type.  A producer row outside [0, NumProducers) must fail before any
// runtime handle exists.

#include <crucible/concurrent/SubstrateSessionBridge.h>

#include <cstdint>

namespace cc = crucible::concurrent;

namespace {
struct Tag {};
struct Key {
    static std::uint64_t key(int value) noexcept {
        return static_cast<std::uint64_t>(value);
    }
};
using Grid = cc::PermissionedCalendarGrid<int, 2, 8, 16, Key, 1ULL, Tag>;
using BadHandle = cc::handle_for_t<Grid, cc::Direction::Producer,
                                   cc::CalendarProducerId<5>>;
}

int main() {
    [[maybe_unused]] BadHandle* impossible = nullptr;
    return 0;
}
