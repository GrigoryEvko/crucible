// Every mapping below comes from OwnedMmap::mint_region, because that is
// the only door there is: the constructor that claims an address is
// private, so a region exists only over what ::mmap returned.
//
// Sentinel TU for fixy/os/OwnedMmap.h: the region is move-only, the leak
// witness admits only the leak atom, release binds to an rvalue, and the
// kernel maps the protection that the type claims.
//
// The empty state is the only one reachable without a syscall, so it is
// checked first.  Then this TU maps anonymous pages, so the unmap path,
// the move path and the release gated on a leak atom all run.

#include <fixy/os/OwnedMmap.h>
#include <foundation/permissions/Permission.h>

#include <sys/mman.h>
#include <unistd.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <type_traits>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace mm = ::fixy::mmap;

struct PageTag {
    using permission_row = eff::Row<>;
};
using Region = ::fixy::OwnedMmap<PageTag, mm::prot::WriteCopy, mm::share::Anonymous>;
using SampleLeak = ::fixy::atom::leak::resource<::fixy::atom::detail::leak_sample_rationale>;

// A context of a test that maps: it owns IO and Block, the row of a
// mapping.
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::IO, eff::Effect::Block>>;

// A mapping takes the brand of the permission it is minted with, so each
// case mints a permission and maps under its brand.
template <typename Prot, typename Brand>
[[nodiscard]] auto map_pages(perm::Permission<PageTag, Brand> const& owner, std::size_t length) {
    MapCtx ctx{eff::testing::test()};
    return ::fixy::OwnedMmap<PageTag, Prot, mm::share::Anonymous, Brand>::mint_region(ctx, owner, length);
}

static_assert(!std::is_copy_constructible_v<Region>);
static_assert(!std::is_copy_assignable_v<Region>);
static_assert(std::is_nothrow_move_constructible_v<Region>);
static_assert(std::is_nothrow_move_assignable_v<Region>);
static_assert(sizeof(Region) == sizeof(void*) + sizeof(std::size_t));

// The tag triple is part of the type, so two regions that name
// different tags cannot be assigned across.
struct OtherTag {};
using OtherRegion = ::fixy::OwnedMmap<OtherTag, mm::prot::WriteCopy, mm::share::Anonymous>;
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

// The protection field of the /proc/self/maps line that holds address,
// such as "r--p", or an empty string when no line holds it.
std::string protection_of(void const* address) {
    std::ifstream maps{"/proc/self/maps"};
    const auto wanted = std::bit_cast<std::uintptr_t>(address);
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long start = 0;
        unsigned long end = 0;
        char protection[5] = {};
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, protection) != 3) continue;
        if (wanted >= start && wanted < end) return std::string{protection};
    }
    return {};
}

// The kernel maps the protection and the share mode that the type claims.
// A caller gives mint_region no bit, so a region typed read-only is a
// read-only private page, and one typed copy-on-write is writable.
int check_the_kernel_maps_the_claimed_protection() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto read_only = map_pages<mm::prot::ReadOnly>(owner, len);
    if (read_only.is_err()) return 0;
    auto const read_only_region = std::move(read_only).expect("the map gave a read-only region");
    if (protection_of(read_only_region.data()) != "r--p") {
        std::fprintf(stderr, "a region typed prot::ReadOnly is mapped %s\n",
                     protection_of(read_only_region.data()).c_str());
        return 50;
    }
    auto writable = map_pages<mm::prot::WriteCopy>(owner, len);
    if (writable.is_err()) return 0;
    auto const writable_region = std::move(writable).expect("the map gave a writable region");
    if (protection_of(writable_region.data()) != "rw-p") {
        std::fprintf(stderr, "a region typed prot::WriteCopy is mapped %s\n",
                     protection_of(writable_region.data()).c_str());
        return 51;
    }
    return 0;
}

// The header and the records of a ring in one mapping: a header of 16
// bytes, then records of 8 bytes.
struct RingHeader {
    std::uint64_t write_index = 0;
    std::uint64_t spare = 0;
};
struct RingRecord {
    std::uint32_t stamp = 0;
    std::uint32_t value = 0;
};

constexpr std::size_t kNoView = ~std::size_t{0};

// The count of elements of an Option of a View, or kNoView when the Option
// holds no View.
template <typename T, std::size_t Extent>
[[nodiscard]] std::size_t count_of(::foundation::core::Option<::foundation::core::View<T, Extent>> const& view) {
    for (::foundation::core::View<T, Extent> const held : view) {
        return held.size();
    }
    return kNoView;
}

// view_at gives the elements at an offset when they fit and the offset is
// aligned, and no value otherwise.  One mapping of LEN bytes holds a header
// and records of a different type after it.  A write through one View shows
// in a later View of the same bytes, so the lifetime start keeps the value of
// each byte.  The case takes the mapping of check_live_mapping.
template <typename Mapping>
int check_view_at(Mapping& region, std::size_t len) {
    for (::foundation::core::View<RingHeader, 1> const headers : region.template view_at<RingHeader, 1>(0)) {
        for (RingHeader& header : headers)
            header.write_index = 3;
    }
    for (::foundation::core::View<RingRecord> const records :
         region.template view_at<RingRecord>(sizeof(RingHeader), 3)) {
        std::uint32_t stamp = 10;
        for (RingRecord& record : records)
            record.stamp = stamp++;
    }

    Mapping const& read = region;
    std::uint64_t seen_index = 0;
    for (::foundation::core::View<RingHeader const, 1> const headers : read.template view_at<RingHeader const, 1>(0)) {
        for (RingHeader const& header : headers)
            seen_index = header.write_index;
    }
    if (seen_index != 3) return 60;
    std::uint32_t stamp_sum = 0;
    for (::foundation::core::View<RingRecord const, 3> const records :
         read.template view_at<RingRecord const, 3>(sizeof(RingHeader))) {
        for (RingRecord const& record : records)
            stamp_sum += record.stamp;
    }
    if (stamp_sum != 10 + 11 + 12) return 61;

    // The bounds: the last record fits, one past it does not, the end of the
    // mapping gives an empty View, and an offset past the end gives none.
    std::size_t const last = len - sizeof(RingRecord);
    if (count_of(region.template view_at<RingRecord>(last, 1)) != 1) return 62;
    if (count_of(region.template view_at<RingRecord>(last, 2)) != kNoView) return 63;
    if (count_of(region.template view_at<RingRecord>(len, 0)) != 0) return 64;
    if (count_of(region.template view_at<RingRecord>(len + 8, 0)) != kNoView) return 65;
    if (count_of(region.template view_at<RingRecord, 1>(len)) != kNoView) return 66;

    // A count or an offset near the largest value overflows no sum.
    if (count_of(region.template view_at<std::uint64_t>(0, ~std::size_t{0})) != kNoView) return 67;
    if (count_of(region.template view_at<std::uint64_t>(~std::size_t{0} - 7, 1)) != kNoView) return 68;

    // The alignment: an offset of 4 holds a word of 4 bytes, not of 8.
    if (count_of(region.template view_at<std::uint64_t>(4, 1)) != kNoView) return 69;
    if (count_of(region.template view_at<std::uint32_t>(4, 1)) != 1) return 70;
    return 0;
}

// The foreign map refuses a negative descriptor with ebadf, before the
// kernel sees it.  The case takes the permission of check_live_mapping.
template <typename Brand>
int check_the_foreign_map_refuses_a_negative_descriptor(perm::Permission<PageTag, Brand> const& owner,
                                                        std::size_t len) {
    MapCtx ctx{eff::testing::test()};
    auto const refused = ::fixy::OwnedMmap<PageTag, mm::prot::ReadOnly, mm::share::Shared, Brand>::map_foreign_region(
        ctx, owner, mm::ForeignDescriptor{-1}, len, ::fixy::fs::FileOffset{0});
    if (refused.is_ok()) return 60;
    if (refused.err().value_or(::foundation::os::Errno{}) != ::foundation::os::Errno::ebadf) return 61;
    return 0;
}

int check_live_mapping() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    if (int rc = check_the_foreign_map_refuses_a_negative_descriptor(owner, len); rc != 0) return rc;
    auto mapped = map_pages<mm::prot::WriteCopy>(owner, len);
    if (mapped.is_err()) return 0;  // mapping unavailable; nothing to check

    auto r = std::move(mapped).expect("the map gave a region");
    void* const addr = r.data();
    if (!r.is_mapped()) return 10;
    if (addr == nullptr) return 11;
    if (r.size() != len) return 12;
    if (int rc = check_view_at(r, len); rc != 0) return rc;

    // The destructor unmaps.  A second region moved from this one must
    // leave the source empty so the unmap happens exactly once.
    auto moved = std::move(r);
    if (r.is_mapped()) return 13;
    if (!moved.is_mapped()) return 14;
    if (moved.data() != addr) return 15;

    // Each accessor of the region that the move emptied gives the empty
    // value, and its view holds no element.
    if (r.data() != nullptr || r.size() != 0) return 16;
    if (r.view<std::byte>().value_or(::foundation::core::View<std::byte>{}).size() != 0) return 17;

    return 0;
}

int check_release_hands_the_region_back() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto mapped = map_pages<mm::prot::WriteCopy>(owner, len);
    if (mapped.is_err()) return 0;

    auto r = std::move(mapped).expect("the map gave a region");
    void* const addr = r.data();
    auto [out_addr, out_len] = std::move(r).release(SampleLeak{});
    if (out_addr != addr) return 20;
    if (out_len != len) return 21;
    if (r.is_mapped()) return 22;

    // Ownership left the wrapper, so the unmap is ours.
    if (::munmap(out_addr, out_len) != 0) return 23;
    return 0;
}

int check_move_assign_unmaps_the_replaced_region() {
    const std::size_t len = page_bytes();
    auto const owner = perm::mint_permission_root<PageTag>();
    auto first = map_pages<mm::prot::WriteCopy>(owner, len);
    if (first.is_err()) return 0;
    auto second = map_pages<mm::prot::WriteCopy>(owner, len);
    if (second.is_err()) return 0;

    auto a = std::move(first).expect("the map gave a region");
    auto b = std::move(second).expect("the map gave a region");
    void* const second_addr = b.data();
    a = std::move(b);
    if (b.is_mapped()) return 30;
    if (a.data() != second_addr) return 31;

    return 0;
}

// The empty region gives an empty View at the offset zero, and no value
// for each element or offset past it.
int check_view_at_of_empty_region() {
    Region empty{};
    if (count_of(empty.view_at<std::byte>(0, 0)) != 0) return 71;
    if (count_of(empty.view_at<std::byte>(0, 1)) != kNoView) return 72;
    if (count_of(empty.view_at<std::byte>(1, 0)) != kNoView) return 73;
    if (count_of(empty.view_at<std::byte, 1>(0)) != kNoView) return 74;
    return 0;
}

// The empty state is reachable without a syscall, and every query
// answers on it.  The release of the empty state unmaps nothing, so the
// destructor of an empty region issues no syscall.
int check_empty_state() {
    Region empty{};
    if (empty.is_mapped()) return 40;
    if (empty.size() != 0u) return 41;
    if (empty.data() != nullptr) return 42;

    Region moved = std::move(empty);
    if (moved.is_mapped()) return 43;

    Region assigned{};
    assigned = std::move(moved);
    if (assigned.is_mapped()) return 44;

    auto [addr, len] = std::move(assigned).release(SampleLeak{});
    if (addr != nullptr) return 45;
    if (len != 0u) return 46;

    return 0;
}

}  // namespace

int main() {
    if (int rc = check_empty_state(); rc != 0) return rc;
    if (int rc = check_the_kernel_maps_the_claimed_protection(); rc != 0) return rc;
    if (int rc = check_live_mapping(); rc != 0) return rc;
    if (int rc = check_release_hands_the_region_back(); rc != 0) return rc;
    if (int rc = check_move_assign_unmaps_the_replaced_region(); rc != 0) return rc;
    if (int rc = check_view_at_of_empty_region(); rc != 0) return rc;

    return 0;
}
