// The mapping surface, run against the kernel.
//
// The negative-compile fixtures of fixy/os/Mmap.h say what the gates
// refuse and nothing about what the mints do, so every case here is a
// mapping that is really made, really written through, and really
// unmapped.
//
// The bit folds and the gate answers stay in the header, where they fire
// wherever the surface is used.  What is here is the behaviour under a
// constructed sequence of calls: that a mapping carries what was written
// to it, that a file mapping shows the file's bytes, that the
// release-aware advice really discards, and that the leak door hands
// back the address the destructor would otherwise have unmapped.  The
// discard runs twice: once with the permission the mapping was minted
// with, and once with the exclusive that a pool hands back after its
// last share ends.

#include <fixy/Core.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/Path.h>
#include <foundation/permissions/SharedPermission.h>

#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {

// A test context that owns IO and Block, in the shape foundation's own
// context self-test uses.  It is handed the capabilities it claims: a
// context is not evidence of a capability, it carries one.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

// The context of a lend and an upgrade of a pool: the schedule decides
// their outcome, so the context holds Nondet.
using PoolOpCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Nondet>>;

// Each mapping is minted with the exclusive permission of its region, and
// takes its tag and its brand from it.  So each region tag declares the
// row a permission needs.
struct AnonRegion final {
    using permission_row = eff::Row<>;
};
struct FileRegion final {
    using permission_row = eff::Row<>;
};
struct DiscardRegion final {
    using permission_row = eff::Row<>;
};
struct PooledRegion final {
    using permission_row = eff::Row<>;
};
struct LeakedRegion final {
    using permission_row = eff::Row<>;
};
struct WordRegion final {
    using permission_row = eff::Row<>;
};

// The rationale a deliberate leak names.  Here the region is unmapped by
// hand right after, which is the one case a test can honestly claim.
struct UnmappedByTheTestItself final {};

using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;

// The files of the test have no name, and only this process holds them and
// changes their size, so no shared map of one faults past its end.
using Faults = fixy::atom::mmap::faults_on_truncate;

inline constexpr std::size_t kPageBytes = 4096;

// The region of a mint, out of its Linear, or the Errno of the map.  The
// region owns the pages, so the unmap happens once, at the end of its
// owner.
template <class Region>
[[nodiscard]] fixy::Result<Region, foundation::os::Errno>
region_of(fixy::Result<fixy::Linear<Region>, foundation::os::Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

[[nodiscard]] int anonymous_mapping_carries_what_was_written() {
    IoBlockCtx ctx{eff::testing::test()};

    const auto owner = perm::mint_permission_root<AnonRegion>();
    auto mapped = region_of(fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, kPageBytes));
    if (mapped.is_err()) {
        std::fprintf(stderr, "an anonymous mapping of one page failed (errno %d)\n",
                     foundation::os::error_number_of(mapped));
        return 1;
    }
    auto region = std::move(mapped).expect("the mint gave a mapping");
    if (!region.is_mapped()) {
        std::fprintf(stderr, "a successful mint handed back a region that reports itself unmapped\n");
        return 1;
    }
    if (region.size() != kPageBytes) {
        std::fprintf(stderr, "the region reported a length the mint was not given\n");
        return 1;
    }

    // A fresh anonymous mapping is zero-filled, which is the one thing
    // mmap(2) promises about its contents.
    auto* const bytes = static_cast<std::uint8_t*>(region.data());
    for (std::size_t index = 0; index < kPageBytes; ++index) {
        if (bytes[index] != 0) {
            std::fprintf(stderr, "a fresh anonymous page was not zero-filled at byte %zu\n", index);
            return 1;
        }
    }

    for (std::size_t index = 0; index < kPageBytes; ++index) {
        bytes[index] = static_cast<std::uint8_t>(index & 0xFFu);
    }
    for (std::size_t index = 0; index < kPageBytes; ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(index & 0xFFu)) {
            std::fprintf(stderr, "the mapping did not carry the byte written at %zu\n", index);
            return 1;
        }
    }

    // Advice the kernel may ignore, so only the call's own report is
    // checked.  Sequential is one of the safe ones: it changes readahead
    // and discards nothing.
    if (auto advised = fixy::mmap::advise<fixy::mmap::advice::Sequential>(ctx, region); advised.is_err()) {
        std::fprintf(stderr, "madvise(MADV_SEQUENTIAL) failed (errno %d)\n", foundation::os::error_number_of(advised));
        return 1;
    }
    return 0;
}

[[nodiscard]] int file_mapping_shows_the_file_bytes() {
    IoBlockCtx ctx{eff::testing::test()};

    // A file with no name under /tmp, which the kernel removes at its last
    // close.
    auto const tmp = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(fixy::TextView{"/tmp"}))
                         .expect("/tmp passes the check");
    fixy::fs::DirectoryHandle const directory = fixy::fs::open_directory(ctx, tmp).expect("/tmp opens");
    fixy::fs::TemporaryFile file =
        fixy::fs::mint_temporary_file_at(ctx, directory.ref())
            .match([](fixy::Linear<fixy::fs::TemporaryFile> opened) { return std::move(opened).consume(); },
                   [](foundation::os::Errno) { return fixy::fs::TemporaryFile{}; });
    if (!file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the file system of /tmp has no file with no name\n");
        return 0;
    }

    // The file holds two pages, and the second page holds a different
    // pattern, so a map at offset zero does not show it.
    std::uint8_t pattern[2 * kPageBytes];
    for (std::size_t index = 0; index < 2 * kPageBytes; ++index) {
        pattern[index] = static_cast<std::uint8_t>((index * 7u + (index / kPageBytes) * 3u) & 0xFFu);
    }
    if (fixy::fs::write_full(ctx, file.ref(), fixy::bytes(pattern)).is_err()) {
        fixy::report(fixy::Sink::Err, "the write of the pattern into the file failed\n");
        return 1;
    }

    const auto owner = perm::mint_permission_root<FileRegion>();
    auto mapped = region_of(fixy::mmap::mint_mmap<ReadOnly, Shared, Faults>(ctx, owner, file.ref(), kPageBytes));
    auto second = region_of(fixy::mmap::mint_mmap<ReadOnly, Shared, Faults>(
        ctx, owner, file.ref(), kPageBytes, fixy::fs::FileOffset{static_cast<std::int64_t>(kPageBytes)}));
    // The file is not needed once the mapping exists: the mapping holds its
    // own reference to the file.
    file = fixy::fs::TemporaryFile{};
    if (mapped.is_err() || second.is_err()) {
        fixy::report(fixy::Sink::Err, "a shared read-only mapping of the file failed (errno {}, {})\n",
                     foundation::os::error_number_of(mapped), foundation::os::error_number_of(second));
        return 1;
    }

    auto const region = std::move(mapped).expect("the mint gave a mapping");
    const auto* const bytes = static_cast<const std::uint8_t*>(region.data());
    if (std::memcmp(bytes, pattern, kPageBytes) != 0) {
        std::fprintf(stderr, "the file mapping did not show the bytes the file holds\n");
        return 1;
    }

    // A map at an offset shows the bytes of the file from that offset.  The
    // mapping is shared, so its view is a SharedView, and each read of a
    // byte is an atomic load.
    auto const second_region = std::move(second).expect("the mint gave a mapping at the offset");
    fixy::mmap::SharedView<std::uint8_t const> const later =
        second_region.view<std::uint8_t const>().expect("a page is a whole count of bytes");
    std::size_t shown = 0;
    for (fixy::mmap::SharedView<std::uint8_t const, kPageBytes> const page : later.window<kPageBytes>(0)) {
        for (fixy::Index<kPageBytes> const index : page.indices()) {
            if (page.load_acquire(index) == pattern[kPageBytes + index.raw()]) ++shown;
        }
    }
    if (shown != kPageBytes) {
        fixy::report(fixy::Sink::Err, "the file mapping at an offset did not show the bytes from that offset\n");
        return 1;
    }
    return 0;
}

// The words of a shared mapping of a file, each read with an acquire load.
// The words at an offset are the words of the file from that offset.  Words
// past the end of the mapping, and an offset that is not a multiple of the
// alignment of a word, give no value.  The mapping takes the permission of
// its region, which the caller holds and ends after the mapping ends.
template <class Brand>
[[nodiscard]] int check_shared_words_of_the_file(perm::Permission<WordRegion, Brand> const& owner) {
    IoBlockCtx ctx{eff::testing::test()};
    auto const tmp = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(fixy::TextView{"/tmp"}))
                         .expect("/tmp passes the check");
    fixy::fs::DirectoryHandle const directory = fixy::fs::open_directory(ctx, tmp).expect("/tmp opens");
    fixy::fs::TemporaryFile file =
        fixy::fs::mint_temporary_file_at(ctx, directory.ref())
            .match([](fixy::Linear<fixy::fs::TemporaryFile> opened) { return std::move(opened).consume(); },
                   [](foundation::os::Errno) { return fixy::fs::TemporaryFile{}; });
    if (!file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the file system of /tmp has no file with no name\n");
        return 0;
    }

    // One page of words: the word at index i holds i + 1 times an odd
    // constant, so no two words are equal.
    constexpr std::size_t kWords = kPageBytes / sizeof(std::uint64_t);
    fixy::FixedArray<std::uint64_t, kWords> words{};
    for (fixy::Index<kWords> const index : words.indices()) {
        std::uint64_t const word = (index.raw() + 1) * 0x9E3779B97F4A7C15ULL;
        words.set(index, word);
    }
    if (fixy::fs::write_full(ctx, file.ref(), fixy::bytes(words)).is_err()) {
        fixy::report(fixy::Sink::Err, "the write of the words into the file failed\n");
        return 1;
    }
    auto mapped = region_of(fixy::mmap::mint_mmap<ReadOnly, Shared, Faults>(ctx, owner, file.ref(), kPageBytes));
    file = fixy::fs::TemporaryFile{};
    if (mapped.is_err()) {
        fixy::report(fixy::Sink::Err, "a shared read-only mapping of the file failed (errno {})\n",
                     foundation::os::error_number_of(mapped));
        return 1;
    }
    auto const region = std::move(mapped).expect("the mint gave a mapping");

    std::size_t loaded = 0;
    for (fixy::mmap::SharedView<std::uint64_t const, 4> const shared :
         region.template view_at<std::uint64_t const, 4>(2 * sizeof(std::uint64_t))) {
        for (fixy::Index<4> const index : shared.indices()) {
            if (shared.load_acquire(index) != (index.raw() + 3) * 0x9E3779B97F4A7C15ULL) {
                fixy::report(fixy::Sink::Err, "the shared word {} is not the word of the file\n", index.raw());
                return 1;
            }
            ++loaded;
        }
    }
    if (loaded != 4) {
        fixy::report(fixy::Sink::Err, "the shared words at an offset inside the mapping gave no value\n");
        return 1;
    }
    bool const is_last_word_given = region.template view_at<std::uint64_t const, 1>(kPageBytes - 8).is_some();
    bool const is_past_end_given = region.template view_at<std::uint64_t const, 2>(kPageBytes - 8).is_some();
    bool const is_misaligned_given = region.template view_at<std::uint64_t const, 1>(4).is_some();
    bool const is_narrow_word_given = region.template view_at<std::uint32_t const, 1>(4).is_some();
    if (!is_last_word_given || is_past_end_given || is_misaligned_given || !is_narrow_word_given) {
        fixy::report(fixy::Sink::Err, "the checks of the shared words gave last {} past {} misaligned {} narrow {}\n",
                     is_last_word_given, is_past_end_given, is_misaligned_given, is_narrow_word_given);
        return 1;
    }
    return 0;
}

// The root of the region of the words ends at the one exit of this
// function, after the mapping of the check ended.
[[nodiscard]] int shared_words_load_the_file_words() {
    auto owner = perm::mint_permission_root<WordRegion>();
    int const result = check_shared_words_of_the_file(owner);
    foundation::linear::drop(std::move(owner));
    return result;
}

[[nodiscard]] int release_aware_advice_discards_the_pages() {
    IoBlockCtx ctx{eff::testing::test()};

    // The permission is what admits the dangerous advice.  A root mint
    // is the only way to the first one, and holding it is the claim that
    // no reader of this region is live — which is true here, because
    // this thread is the only one that has ever touched it.  The mapping
    // is minted with it, so the two name one identity.
    const auto exclusive = perm::mint_permission_root<DiscardRegion>();

    auto mapped = region_of(fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, exclusive, kPageBytes));
    if (mapped.is_err()) {
        std::fprintf(stderr, "an anonymous mapping for the discard leg failed (errno %d)\n",
                     foundation::os::error_number_of(mapped));
        return 1;
    }
    auto region = std::move(mapped).expect("the mint gave a mapping");
    static_assert(std::is_same_v<decltype(region)::brand_type, foundation::brand::brand_of_t<decltype(exclusive)>>,
                  "a mapping takes the brand of the permission it was minted with");

    auto* const bytes = static_cast<std::uint8_t*>(region.data());
    std::memset(bytes, 0xA5, kPageBytes);
    if (bytes[0] != 0xA5) {
        std::fprintf(stderr, "the mapping did not take the fill\n");
        return 1;
    }

    if (auto discarded = fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed>(ctx, region, exclusive);
        discarded.is_err()) {
        std::fprintf(stderr, "madvise(MADV_DONTNEED) failed (errno %d)\n", foundation::os::error_number_of(discarded));
        return 1;
    }

    // On a private anonymous mapping the discard is observable: the
    // pages come back zero-filled on the next touch.  This is what
    // separates the release-aware door from the plain one — it is the
    // only advice here that destroys what the caller wrote.
    for (std::size_t index = 0; index < kPageBytes; ++index) {
        if (bytes[index] != 0) {
            std::fprintf(stderr, "MADV_DONTNEED left byte %zu at 0x%02X rather than discarding the page\n", index,
                         static_cast<unsigned>(bytes[index]));
            return 1;
        }
    }
    return 0;
}

// The exclusive that a pool hands back is the permission the pool was
// built from, so it has the brand that the mapping took at the mint.
// While a share is out, the pool refuses the upgrade, and the caller has
// no proof to present.  When the share ends, the upgrade gives the proof
// back and the discard runs.
[[nodiscard]] int the_pool_hands_back_the_exclusive_that_releases() {
    IoBlockCtx ctx{eff::testing::test()};

    auto root = perm::mint_permission_root<PooledRegion>();
    auto mapped = region_of(fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, root, kPageBytes));
    if (mapped.is_err()) {
        std::fprintf(stderr, "an anonymous mapping for the pooled leg failed (errno %d)\n",
                     foundation::os::error_number_of(mapped));
        return 1;
    }
    auto region = std::move(mapped).expect("the mint gave a mapping");
    auto* const bytes = static_cast<std::uint8_t*>(region.data());
    std::memset(bytes, 0x3C, kPageBytes);

    perm::SharedPermissionPool pool{std::move(root)};
    static_assert(std::is_same_v<decltype(pool)::brand_type, decltype(region)::brand_type>,
                  "the pool keeps the brand of the permission the mapping was minted with");
    const PoolOpCtx pool_ctx{eff::testing::test()};
    {
        auto share = pool.lend(pool_ctx);
        if (share.is_none()) {
            std::fprintf(stderr, "the pool refused a share with no exclusive out\n");
            return 1;
        }
        // An exclusive that the pool gives here goes back to the pool at once.
        const bool is_upgraded_under_a_share = pool.try_upgrade(pool_ctx).match(
            [&pool](auto exclusive) noexcept {
                pool.deposit_exclusive(std::move(exclusive));
                return true;
            },
            []() noexcept { return false; });
        if (is_upgraded_under_a_share) {
            std::fprintf(stderr, "the pool gave the exclusive out while a share was live\n");
            return 1;
        }
    }

    auto exclusive = pool.try_upgrade(pool_ctx);
    int failures = 0;
    if (exclusive.is_none()) {
        std::fprintf(stderr, "the pool kept the exclusive after the last share ended\n");
        ++failures;
    }
    exclusive.match(
        [&](auto const& held) noexcept {
            if (auto discarded =
                    fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed>(ctx, region, held.peek());
                discarded.is_err()) {
                std::fprintf(stderr, "madvise(MADV_DONTNEED) with the pooled exclusive failed (errno %d)\n",
                             foundation::os::error_number_of(discarded));
                ++failures;
            } else if (bytes[0] != 0 || bytes[kPageBytes - 1] != 0) {
                std::fprintf(stderr, "MADV_DONTNEED with the pooled exclusive did not discard the page\n");
                ++failures;
            }
        },
        []() noexcept {});
    // The one exit gives the exclusive back to the pool, also when a check failed.
    std::move(exclusive).match([&pool](auto held) noexcept { pool.deposit_exclusive(std::move(held)); },
                               []() noexcept {});
    return failures;
}

[[nodiscard]] int the_leak_door_hands_back_the_mapping() {
    IoBlockCtx ctx{eff::testing::test()};

    const auto owner = perm::mint_permission_root<LeakedRegion>();
    auto mapped = region_of(fixy::mmap::mint_mmap_anon<WriteAnon, Anonymous>(ctx, owner, kPageBytes));
    if (mapped.is_err()) {
        std::fprintf(stderr, "an anonymous mapping for the leak leg failed (errno %d)\n",
                     foundation::os::error_number_of(mapped));
        return 1;
    }

    auto region = std::move(mapped).expect("the mint gave a mapping");
    void* const expected_address = region.data();

    const auto [address, length] = std::move(region).release(fixy::atom::leak::resource<UnmappedByTheTestItself>{});
    if (address != expected_address || length != kPageBytes) {
        std::fprintf(stderr, "release handed back an address or length the region did not hold\n");
        return 1;
    }
    if (region.is_mapped()) {
        std::fprintf(stderr, "release left the region still claiming the pages, so they would be unmapped twice\n");
        return 1;
    }

    // The rationale tag names this line.  Without it the pages would
    // leak for the life of the process.
    if (::munmap(address, length) != 0) {
        std::fprintf(stderr, "unmapping the released region by hand failed (errno %d)\n", errno);
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = anonymous_mapping_carries_what_was_written(); rc != 0) return rc;
    if (const int rc = file_mapping_shows_the_file_bytes(); rc != 0) return rc;
    if (const int rc = shared_words_load_the_file_words(); rc != 0) return rc;
    if (const int rc = release_aware_advice_discards_the_pages(); rc != 0) return rc;
    if (const int rc = the_pool_hands_back_the_exclusive_that_releases(); rc != 0) return rc;
    if (const int rc = the_leak_door_hands_back_the_mapping(); rc != 0) return rc;
    return 0;
}
