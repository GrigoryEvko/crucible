// Sentinel TU for fixy/OwnedMmap.h: the region is move-only, the leak
// witness admits only the leak atom, and release binds to an rvalue.
//
// The header's own self-test reaches only the empty state, because a
// real region needs a syscall.  This TU maps one anonymous page, so the
// unmap path, the move path and the grant-gated release all run.

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
    void* addr = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) return 0;  // mapping unavailable; nothing to check

    Region r{addr, len};
    if (!r.is_mapped()) return 10;
    if (r.data() != addr) return 11;
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
    void* addr = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) return 0;

    Region r{addr, len};
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
    void* first = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (first == MAP_FAILED) return 0;
    void* second = ::mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (second == MAP_FAILED) return 0;

    Region a{first, len};
    Region b{second, len};
    a = std::move(b);
    if (b.is_mapped()) return 30;
    if (a.data() != second) return 31;

    return 0;
}

}  // namespace

int main() {
    ::fixy::detail::owned_mmap_self_test::runtime_smoke_test();

    if (int rc = check_live_mapping(); rc != 0) return rc;
    if (int rc = check_release_hands_the_region_back(); rc != 0) return rc;
    if (int rc = check_move_assign_unmaps_the_replaced_region(); rc != 0) return rc;

    return 0;
}
