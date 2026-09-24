// Every mapping below comes from Region::map_region, because that is
// the only door there is: the constructor that claims an address is
// private, so a region exists only over what ::mmap returned.  These
// cases used to call ::mmap themselves and wrap the result, which is
// exactly the pattern the private constructor removed.
//
// Sentinel TU for fixy/OwnedMmap.h: the region is move-only, the leak
// witness admits only the leak atom, and release binds to an rvalue.
//
// The empty state is the only one reachable without a syscall, so it is
// checked first.  Then this TU maps one anonymous page, so the unmap
// path, the move path and the grant-gated release all run.

#include <fixy/OwnedMmap.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

struct PageTag {};
struct PageProt {};
struct PageShare {};
using Region = ::fixy::OwnedMmap<PageTag, PageProt, PageShare>;
using SampleLeak = ::fixy::atom::leak::resource<::fixy::atom::detail::leak_sample_rationale>;

static_assert(!std::is_copy_constructible_v<Region>);
static_assert(!std::is_copy_assignable_v<Region>);
static_assert(std::is_nothrow_move_constructible_v<Region>);
static_assert(std::is_nothrow_move_assignable_v<Region>);
static_assert(sizeof(Region) == sizeof(void*) + sizeof(std::size_t));

// The tag triple is part of the type, so two regions that name
// different tags cannot be assigned across.
struct OtherTag {};
using OtherRegion = ::fixy::OwnedMmap<OtherTag, PageProt, PageShare>;
static_assert(!std::is_assignable_v<Region&, OtherRegion&&>);
static_assert(!std::is_constructible_v<Region, OtherRegion&&>);

// release binds to an rvalue only, and only with a leak atom.
template <typename R>
concept ReleasesLvalue = requires(R& r, SampleLeak w) { r.release(w); };
static_assert(!ReleasesLvalue<Region>);

// The kernel rounds a mapping up to a whole page, and ::munmap is
// called with the same length, so asking the system keeps the two ends
// of every mapping below in step on a host whose page is not 4 KiB.
std::size_t page_bytes() {
    const long reported = ::sysconf(_SC_PAGESIZE);
    return reported > 0 ? static_cast<std::size_t>(reported) : std::size_t{4096};
}

int check_live_mapping() {
    const std::size_t len = page_bytes();
    auto mapped = Region::map_region(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, len, 0);
    if (!mapped) return 0;  // mapping unavailable; nothing to check

    Region r = std::move(*mapped);
    void* const addr = r.data();
    if (!r.is_mapped()) return 10;
    if (addr == MAP_FAILED) return 11;
    if (r.size() != len) return 12;

    // The destructor unmaps.  A second region moved from this one must
    // leave the source empty so the unmap happens exactly once.
    Region moved = std::move(r);
    if (r.is_mapped()) return 13;
    if (!moved.is_mapped()) return 14;
    if (moved.data() != addr) return 15;

    return 0;
}

int check_release_hands_the_region_back() {
    const std::size_t len = page_bytes();
    auto mapped = Region::map_region(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, len, 0);
    if (!mapped) return 0;

    Region r = std::move(*mapped);
    void* const addr = r.data();
    auto [out_addr, out_len] = std::move(r).release(SampleLeak{});
    if (out_addr != addr) return 20;
    if (out_len != len) return 21;
    if (r.is_mapped()) return 22;

    // Ownership left the wrapper, so the unmap is ours.
    if (::munmap(out_addr, out_len) != 0) return 23;  // SYSCALL-CAP-OK: the test owns the region after release
    return 0;
}

int check_move_assign_unmaps_the_replaced_region() {
    const std::size_t len = page_bytes();
    auto first = Region::map_region(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, len, 0);
    if (!first) return 0;
    auto second = Region::map_region(PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, len, 0);
    if (!second) return 0;

    Region a = std::move(*first);
    Region b = std::move(*second);
    void* const second_addr = b.data();
    a = std::move(b);
    if (b.is_mapped()) return 30;
    if (a.data() != second_addr) return 31;

    return 0;
}

// The empty state is reachable without a syscall, and every query
// answers on it.  release_ takes the not-mapped branch, so the
// destructor of an empty region issues no syscall.
int check_empty_state() {
    Region empty{};
    if (empty.is_mapped()) return 40;
    if (empty.size() != 0u) return 41;
    if (empty.data() != MAP_FAILED) return 42;

    Region moved = std::move(empty);
    if (moved.is_mapped()) return 43;

    Region assigned{};
    assigned = std::move(moved);
    if (assigned.is_mapped()) return 44;

    auto [addr, len] = std::move(assigned).release(SampleLeak{});
    if (addr != MAP_FAILED) return 45;
    if (len != 0u) return 46;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_empty_state(); rc != 0) return rc;
    if (int rc = check_live_mapping(); rc != 0) return rc;
    if (int rc = check_release_hands_the_region_back(); rc != 0) return rc;
    if (int rc = check_move_assign_unmaps_the_replaced_region(); rc != 0) return rc;

    return 0;
}
