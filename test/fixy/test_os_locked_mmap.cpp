// A mapping and its lock in one object (LockedMmap of fixy/os/LockedMmap.h).
//   - A discard zeroes the pages of a private mapping, and the next write goes
//     through.
//   - A discard gives ReadersLive while a read body or a write body runs, and
//     the bytes stay.  The kernel is not asked.
//   - Two objects of one mint site have one type.  The discard of one leaves
//     the bytes of the other, and a reader of the other does not refuse it.
//   - A map that fails gives the exclusive back, and a second mint takes it.
//   - An advice that discards no page is a call of the region in a body.
//   - A write body and a read body get a View of a range of the pages, of a
//     counted extent and of a fixed extent.  A range past the end gives none.
//   - A write body sets the memory policy of the pages, and the policy stays
//     over a discard.  It refuses the masks that the policy mint refuses.
//   - A shared mapping of sealed memory keeps its bytes over a discard,
//     because the kernel reads them again from the memory.

#include <fixy/os/LockedMmap.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/NumaPlacement.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace perm = ::foundation::permissions;
namespace mmap = ::fixy::mmap;

constexpr ::fixy::TestRunnerCtx kTest{eff::testing::test()};

// One context for each door: the rows of a map and of an advice, and the row
// of a pool.
using MapCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                eff::Effect::Block, eff::Effect::Nondet>>;
constexpr MapCtx kCtx{eff::testing::test()};
constexpr eff::Alloc kAlloc = eff::testing::test().alloc;

// The region of each mapping of the test.  Each root is a fresh mint, so the
// regions differ in their brands.
struct PageRegion final {
    using permission_row = eff::Row<>;
};

using WriteCopy = ::fixy::atom::mmap::with_prot<mmap::prot::WriteCopy>;
using Anonymous = ::fixy::atom::mmap::with_share<mmap::share::Anonymous>;
using ReadWrite = ::fixy::atom::mmap::with_prot<mmap::prot::ReadWrite>;
using Shared = ::fixy::atom::mmap::with_share<mmap::share::Shared>;
using DontNeed = mmap::advice::DontNeed;

constexpr std::size_t kPageBytes = 4096;
constexpr ::fixy::ByteCount kPage{kPageBytes};

// Writes value to each byte of the mapping of the object.  False when a
// reader or a writer is in the region.
template <class Locked>
[[nodiscard]] bool fill_pages(Locked& locked, std::uint8_t value) {
    return locked
        .with_write(kCtx,
                    [value](auto& region) noexcept {
                        for (auto const bytes : region.template view<std::uint8_t>())
                            for (std::uint8_t& byte : bytes)
                                byte = value;
                    })
        .is_some();
}

// The count of the bytes of the mapping that hold value, or no value when a
// writer is in the region.
template <class Locked>
[[nodiscard]] ::fixy::Option<std::size_t> count_bytes(Locked& locked, std::uint8_t value) {
    return locked.with_read(kCtx, [value](auto const& region) noexcept {
        std::size_t count = 0;
        for (auto const bytes : region.template view<std::uint8_t const>())
            for (std::uint8_t const& byte : bytes)
                if (byte == value) ++count;
        return count;
    });
}

[[nodiscard]] bool is_readers_live(::fixy::Result<::fixy::Unit, mmap::ReleaseRefusal> const& discarded) {
    return discarded.match([](::fixy::Unit const&) noexcept { return false; },
                           [](mmap::ReleaseRefusal const& refusal) noexcept {
                               return refusal.match([](mmap::ReadersLive const&) noexcept { return true; },
                                                    [](::foundation::os::Errno const&) noexcept { return false; });
                           });
}

void a_discard_zeroes_private_pages() {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(*locked, std::uint8_t{0xA5}));
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0xA5}) == ::fixy::some(kPageBytes));
    CRUCIBLE_TEST_ASSERT(kTest, locked->template advise_release_aware<DontNeed>(kCtx).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0}) == ::fixy::some(kPageBytes));
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(*locked, std::uint8_t{0x3C}));
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0x3C}) == ::fixy::some(kPageBytes));
}

void a_reader_or_a_writer_refuses_the_discard() {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    auto& object = *locked;
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(object, std::uint8_t{0x3C}));

    ::fixy::Option<bool> const is_read_kept = object.with_read(kCtx, [&object](auto const& region) noexcept {
        bool const is_refused = is_readers_live(object.template advise_release_aware<DontNeed>(kCtx));
        return is_refused && region.template view<std::uint8_t const>().is_some_and([](auto const& bytes) noexcept {
            return bytes.walk().all_of([](std::uint8_t const& byte) noexcept { return byte == 0x3C; });
        });
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_read_kept == ::fixy::some(true));

    ::fixy::Option<bool> const is_write_kept = object.with_write(kCtx, [&object](auto& /*region*/) noexcept {
        return is_readers_live(object.template advise_release_aware<DontNeed>(kCtx));
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_write_kept == ::fixy::some(true));

    // Both bodies ended, so the pool holds the exclusive again.
    CRUCIBLE_TEST_ASSERT(kTest, object.template advise_release_aware<DontNeed>(kCtx).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(object, std::uint8_t{0}) == ::fixy::some(kPageBytes));
}

// The two runs of this template mint their objects at one site, so the two
// objects have one type, and the first lives while the second runs.
template <class... First>
void objects_of_one_mint_site_have_their_own_pool(First&... first) {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(*locked, std::uint8_t{0x3C}));
    if constexpr (sizeof...(First) == 0) {
        objects_of_one_mint_site_have_their_own_pool(*locked);
    } else {
        static_assert((std::is_same_v<First, std::remove_cvref_t<decltype(*locked)>> && ...));
        // A reader of the second object refuses its own discard, and it does
        // not stop the discard of the first object.
        ::fixy::Option<bool> const is_second_kept = locked->with_read(kCtx, [&](auto const& /*region*/) noexcept {
            bool const is_first_discarded = (first.template advise_release_aware<DontNeed>(kCtx).is_ok() && ...);
            return is_first_discarded && is_readers_live(locked->template advise_release_aware<DontNeed>(kCtx));
        });
        CRUCIBLE_TEST_ASSERT(kTest, is_second_kept == ::fixy::some(true));
        CRUCIBLE_TEST_ASSERT(kTest, ((count_bytes(first, std::uint8_t{0}) == ::fixy::some(kPageBytes)) && ...));
        CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0x3C}) == ::fixy::some(kPageBytes));
    }
}

// A map of no byte fails with einval.  The refusal gives the exclusive back,
// and a second mint with it succeeds.
void a_failed_map_gives_the_exclusive_back() {
    auto refused = mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(
        kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), ::fixy::ByteCount{0});
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    auto refusal = std::move(refused).err().expect("a map of no byte fails");
    CRUCIBLE_TEST_ASSERT(kTest, refusal.error() == ::foundation::os::Errno::einval);
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, std::move(refusal).take_exclusive(), kPage)
            .expect("an anonymous mapping of one page");
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(*locked, std::uint8_t{0x5A}));
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0x5A}) == ::fixy::some(kPageBytes));
}

void an_advice_that_discards_nothing_runs_in_a_body() {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(*locked, std::uint8_t{0x3C}));
    ::fixy::Option<bool> const is_write_advised = locked->with_write(
        kCtx, [](auto& region) noexcept { return region.template advise<mmap::advice::Sequential>(kCtx).is_ok(); });
    ::fixy::Option<bool> const is_read_advised = locked->with_read(
        kCtx, [](auto const& region) noexcept { return region.template advise<mmap::advice::WillNeed>(kCtx).is_ok(); });
    CRUCIBLE_TEST_ASSERT(kTest, is_write_advised == ::fixy::some(true));
    CRUCIBLE_TEST_ASSERT(kTest, is_read_advised == ::fixy::some(true));
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0x3C}) == ::fixy::some(kPageBytes));
    ::fixy::Option<bool> const is_one_page =
        locked->with_read(kCtx, [](auto const& region) noexcept { return region.size() == kPage; });
    CRUCIBLE_TEST_ASSERT(kTest, is_one_page == ::fixy::some(true));
}

void a_body_gets_a_view_of_a_range() {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    constexpr ::fixy::ByteCount kRangeStart{16};
    constexpr ::fixy::ByteCount kWordStart{64};
    constexpr ::fixy::ElementCount kRange{4};
    ::fixy::Option<std::size_t> const written = locked->with_write(kCtx, [](auto& region) noexcept {
        std::size_t count = 0;
        for (auto const bytes : region.template view_at<std::uint8_t>(kRangeStart, kRange))
            for (std::uint8_t& byte : bytes) {
                byte = 0x7E;
                ++count;
            }
        for (auto const bytes : region.template view_at<std::uint8_t, 8>(kWordStart))
            for (std::uint8_t& byte : bytes) {
                byte = 0x7E;
                ++count;
            }
        return count;
    });
    CRUCIBLE_TEST_ASSERT(kTest, written == ::fixy::some(std::size_t{12}));
    CRUCIBLE_TEST_ASSERT(kTest, count_bytes(*locked, std::uint8_t{0x7E}) == ::fixy::some(std::size_t{12}));

    ::fixy::Option<std::size_t> const read = locked->with_read(kCtx, [](auto const& region) noexcept {
        std::size_t count = 0;
        for (auto const bytes : region.template view_at<std::uint8_t const>(kRangeStart, kRange))
            for (std::uint8_t const& byte : bytes)
                if (byte == 0x7E) ++count;
        for (auto const bytes : region.template view_at<std::uint8_t const, 8>(kWordStart))
            for (std::uint8_t const& byte : bytes)
                if (byte == 0x7E) ++count;
        return count;
    });
    CRUCIBLE_TEST_ASSERT(kTest, read == ::fixy::some(std::size_t{12}));

    ::fixy::Option<bool> const is_past_the_end_refused = locked->with_read(kCtx, [](auto const& region) noexcept {
        return region.template view_at<std::uint8_t const>(kPage, kRange).is_none()
            && region.template view_at<std::uint8_t const, 8>(kPage).is_none();
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_past_the_end_refused == ::fixy::some(true));
}

// Sets the policy over nodes in a write body.  No reader and no writer is in
// the region of the object.
template <::fixy::NumaPolicy Policy, class Locked>
[[nodiscard]] ::fixy::Result<::fixy::Unit, ::foundation::os::Errno> set_policy(Locked& locked,
                                                                               ::fixy::NumaNodeMask const& nodes) {
    return locked
        .with_write(kCtx,
                    [nodes](auto& region) noexcept { return region.template set_memory_policy<Policy>(kCtx, nodes); })
        .expect("no reader and no writer is in the region");
}

// True when the one page of the object is on no node other than node.  No
// value when the kernel refuses the query.
template <class Locked>
[[nodiscard]] ::fixy::Option<bool> is_page_on_node(Locked& locked, ::fixy::NumaNodeId node) {
    return locked
        .with_read(kCtx,
                   [node](auto const& region) noexcept {
                       ::fixy::Option<bool> is_on_node = ::fixy::none;
                       for (auto const bytes : region.template view<std::uint8_t const>())
                           for (auto const& census : ::fixy::numa::census_page_nodes(kCtx, bytes).ok())
                               is_on_node = ::fixy::some(census.pages() == 1 && census.pages_off(node) == 0);
                       return is_on_node;
                   })
        .expect("no writer is in the region");
}

// Binds the one page of the object to node, and writes it before and after a
// discard.  False when the kernel of the host sets no policy.  The object has
// no page at the call of the policy, so the call moves none.
template <class Locked>
[[nodiscard]] bool bind_page_to_node(Locked& locked, ::fixy::NumaNodeId node) {
    CRUCIBLE_TEST_ASSERT(kTest, locked.template advise_release_aware<DontNeed>(kCtx).is_ok());
    auto const set = set_policy<::fixy::NumaPolicy::bind>(
        locked, ::fixy::make_numa_node_mask(node).expect("an allowed node is concrete"));
    if (set.err() == ::fixy::some(::foundation::os::Errno::enosys)
        || set.err() == ::fixy::some(::foundation::os::Errno::eperm)) {
        return false;
    }
    CRUCIBLE_TEST_ASSERT(kTest, set.is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(locked, std::uint8_t{0x3C}));
    CRUCIBLE_TEST_ASSERT(kTest, is_page_on_node(locked, node) == ::fixy::some(true));
    CRUCIBLE_TEST_ASSERT(kTest, locked.template advise_release_aware<DontNeed>(kCtx).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, fill_pages(locked, std::uint8_t{0xA5}));
    CRUCIBLE_TEST_ASSERT(kTest, is_page_on_node(locked, node) == ::fixy::some(true));
    return true;
}

void a_write_body_sets_a_memory_policy() {
    auto locked =
        mmap::mint_locked_mmap_anon<WriteCopy, Anonymous>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(), kPage)
            .expect("an anonymous mapping of one page");
    ::fixy::NumaNodeMask two_nodes{};
    CRUCIBLE_TEST_ASSERT(kTest, two_nodes.insert(::fixy::NumaNodeId{0}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, two_nodes.insert(::fixy::NumaNodeId{1}).is_ok());
    auto const empty_mask = set_policy<::fixy::NumaPolicy::bind>(*locked, ::fixy::NumaNodeMask{});
    auto const two_preferred = set_policy<::fixy::NumaPolicy::preferred>(*locked, two_nodes);
    CRUCIBLE_TEST_ASSERT(kTest, empty_mask.err() == ::fixy::some(::foundation::os::Errno::einval));
    CRUCIBLE_TEST_ASSERT(kTest, two_preferred.err() == ::fixy::some(::foundation::os::Errno::einval));

    ::fixy::NumaNodeMask const allowed =
        ::fixy::numa::read_allowed_numa_nodes(kCtx).expect("the status of the process holds its allowed nodes");
    auto const slots = allowed.slots();
    CRUCIBLE_TEST_ASSERT(kTest, slots.size() > 0);
    bool const is_each_policy_set = slots.with_keys([&locked](auto const& keys) noexcept {
        for (::fixy::NumaNodeMask::Slot const slot : keys) {
            if (!bind_page_to_node(*locked, ::fixy::numa_node_of_slot(slot))) return false;
        }
        return true;
    });
    if (!is_each_policy_set) {
        ::fixy::report(::fixy::Sink::Err, "[skipped] the kernel of this host sets no memory policy\n");
    }
}

// The bytes of a shared mapping of sealed memory live in the memory.  A
// discard drops the pages of the mapping alone, and the next read loads the
// bytes again.
void a_shared_mapping_keeps_its_bytes() {
    auto memory = ::fixy::fs::mint_sealed_memory(kCtx, ::fixy::fs::FileOffset{std::int64_t{kPageBytes}})
                      .expect("sealed memory of one page")
                      .consume();
    auto locked = mmap::mint_locked_mmap<ReadWrite, Shared>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(),
                                                            memory.ref(), kPage)
                      .expect("a shared mapping of the sealed memory");
    ::fixy::Option<bool> const is_written = locked->with_write(kCtx, [](auto& region) noexcept {
        bool is_stored = false;
        for (auto const words : region.template view_at<std::uint64_t, 1>(::fixy::ByteCount{0})) {
            words.store_release(::fixy::index<0>(), std::uint64_t{0x9E3779B97F4A7C15});
            is_stored = true;
        }
        return is_stored;
    });
    CRUCIBLE_TEST_ASSERT(kTest, is_written == ::fixy::some(true));
    CRUCIBLE_TEST_ASSERT(kTest, locked->template advise_release_aware<DontNeed>(kCtx).is_ok());
    ::fixy::Option<std::uint64_t> const read = locked->with_read(kCtx, [](auto const& region) noexcept {
        std::uint64_t value = 0;
        for (auto const words : region.template view_at<std::uint64_t const, 1>(::fixy::ByteCount{0}))
            value = words.load_acquire(::fixy::index<0>());
        return value;
    });
    CRUCIBLE_TEST_ASSERT(kTest, read == ::fixy::some(std::uint64_t{0x9E3779B97F4A7C15}));

    // A map past the end of the sealed memory gives enxio and the exclusive.
    auto refused = mmap::mint_locked_mmap<ReadWrite, Shared>(kCtx, kAlloc, perm::mint_permission_root<PageRegion>(),
                                                             memory.ref(), ::fixy::ByteCount{2 * kPageBytes});
    CRUCIBLE_TEST_ASSERT(kTest, refused.is_err());
    auto refusal = std::move(refused).err().expect("the range is past the end of the memory");
    CRUCIBLE_TEST_ASSERT(kTest, refusal.error() == ::foundation::os::Errno::enxio);
    ::foundation::linear::drop(std::move(refusal).take_exclusive());
}

}  // namespace

int main() {
    a_discard_zeroes_private_pages();
    a_reader_or_a_writer_refuses_the_discard();
    objects_of_one_mint_site_have_their_own_pool();
    a_failed_map_gives_the_exclusive_back();
    an_advice_that_discards_nothing_runs_in_a_body();
    a_body_gets_a_view_of_a_range();
    a_write_body_sets_a_memory_policy();
    a_shared_mapping_keeps_its_bytes();
    ::fixy::check::pass(kTest, "test_os_locked_mmap: PASSED\n");
    return 0;
}
