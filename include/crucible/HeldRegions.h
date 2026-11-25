#pragma once

// The regions that the foreground of a Vigil holds, in cells that one thread
// writes and the publish stage reads.
//
// The cells and their sequence are a sequence lock of one writer.  A write
// makes the sequence odd, stores each cell and makes the sequence even again,
// so each write adds two.  The generation of a write is the half of the
// sequence after it.
//
// A read loads the sequence, then each cell, and then the sequence again.  Two
// equal even values give the cells of one write.  Two other values give no
// regions: a write stood open at the read.  So the read takes no lock, waits
// for nothing and ends after CAP + 2 loads.
//
// Order of the loads.  Each store is a release and each load is an acquire, so
// the read needs no fence:
//   - The first load reads the even value that the write of generation G
//     stored at its end.  That store comes after each cell store of the write,
//     so each cell load of the read gives the value of the write G or of a
//     later write.
//   - A cell load that gives the value of a later write reads a store that
//     comes after the odd value of that write.  The second load of the
//     sequence comes after the cell load, so it gives that odd value or a
//     later value, which is not the value of the first load.
// So a read that gives regions gives the cells of the write G.

#include <crucible/MerkleDag.h>
#include <crucible/RegionCache.h>
#include <fixy/Core.h>

#include <cstdint>
#include <utility>

namespace crucible {

// The cells that the Owner writes.  Only the Owner opens a write, on its one
// producer thread, and each thread can read.
template <class Owner>
class HeldRegions final {
public:
    // The region of the alignment walk, the region of the replay context, the
    // region that the producer took last, and each age of the region cache.
    static constexpr uint32_t CAP = 3 + RegionCache::CAP;
    using Position = ::fixy::Index<CAP>;

    // The regions of one write, and the generation of that write.
    struct Read {
        ::fixy::FixedArray<ConstRegionLink, CAP> regions{};
        uint64_t generation = 0;
    };

    HeldRegions() noexcept = default;
    HeldRegions(HeldRegions const&) = delete("the publish stage reads the cells at one address");
    HeldRegions& operator=(HeldRegions const&) = delete("the publish stage reads the cells at one address");
    HeldRegions(HeldRegions&&) = delete("the publish stage reads the cells at one address");
    HeldRegions& operator=(HeldRegions&&) = delete("the publish stage reads the cells at one address");
    ~HeldRegions() = default;

    // The regions of the last write, or none while a write stands open.  It
    // waits for nothing.  Complexity: O(CAP).
    [[nodiscard]] ::fixy::Option<Read> try_read() const noexcept {
        const uint64_t before = sequence_.load_acquire();
        if ((before & 1u) != 0) return ::fixy::none;
        Read read{.generation = before / 2};
        for (const Position position : cells_.indices()) {
            read.regions.set(position, cells_.at(position).load_acquire());
        }
        if (sequence_.load_acquire() != before) return ::fixy::none;
        return ::fixy::some(read);
    }

    // True when a cell names the region.  Only the producer thread asks: it
    // writes the cells at the end of each cold path that changes a holder, so
    // on that thread the cells name each region that the foreground holds.
    // Complexity: O(CAP).
    [[nodiscard]] bool holds(ConstRegionLink region) const noexcept {
        for (const ::fixy::Atomic<ConstRegionLink>& cell : cells_) {
            if (::fixy::same_target(cell.load_acquire(), region)) return true;
        }
        return false;
    }

private:
    friend Owner;

    // Opens a write, gives the body a call that stores one cell, and ends the
    // write after the body.  Each cell is an Atomic, and the publish stage
    // reads the cells at the same time, so each store goes through the element
    // door in place.
    template <class Body>
    void with_write_(Body&& body) noexcept {
        const uint64_t before = sequence_.load_acquire();
        sequence_.store_release(before + 1);
        std::forward<Body>(body)([this](Position position, ConstRegionLink region) noexcept {
            cells_.with_element(
                position, [region](::fixy::Atomic<ConstRegionLink>& cell) noexcept { cell.store_release(region); });
        });
        sequence_.store_release(before + 2);
    }

    ::fixy::Atomic<uint64_t> sequence_{};
    ::fixy::FixedArray<::fixy::Atomic<ConstRegionLink>, CAP> cells_{};
};

}  // namespace crucible
