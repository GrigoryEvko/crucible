// The mapping surface, run against the kernel.
//
// Neither fixy/os/Mmap.h nor the header it was ported from has ever had
// a positive test.  Thirteen negative-compile fixtures stand over the
// old one, which say what the gates refuse and nothing about what the
// mints do, so every case here is a mapping that is really made, really
// written through, and really unmapped.
//
// The bit folds and the gate answers stay in the header, where they fire
// wherever the surface is used.  What is here is the behaviour under a
// constructed sequence of calls: that a mapping carries what was written
// to it, that a file mapping shows the file's bytes, that the
// release-aware advice really discards, and that the leak door hands
// back the address the destructor would otherwise have unmapped.

#include <fixy/os/Mmap.h>

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {

// The named context this surface is meant to take belongs to a
// fixy/Ctx.h the tree does not have yet.  This one stands in, in the
// shape foundation's own context self-test uses.  It is handed the
// capabilities it claims: a context is not evidence of a capability, it
// carries one.
using IoBlockCtx =
    eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO, eff::Effect::Block>>;

struct AnonRegion final {};
struct FileRegion final {};
struct DiscardRegion final {
    using permission_row = eff::Row<>;
};
struct LeakedRegion final {};

// The rationale a deliberate leak names.  Here the region is unmapped by
// hand right after, which is the one case a test can honestly claim.
struct UnmappedByTheTestItself final {};

using WriteAnon = fixy::atom::mmap::with_prot<fixy::mmap::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<fixy::mmap::share::Anonymous>;
using ReadOnly = fixy::atom::mmap::with_prot<fixy::mmap::prot::ReadOnly>;
using Shared = fixy::atom::mmap::with_share<fixy::mmap::share::Shared>;

inline constexpr std::size_t kPageBytes = 4096;

[[nodiscard]] int anonymous_mapping_carries_what_was_written() {
    IoBlockCtx ctx{eff::testing::test()};

    auto mapped = fixy::mmap::mint_mmap_anon<AnonRegion, WriteAnon, Anonymous>(ctx, kPageBytes);
    if (!mapped) {
        std::fprintf(stderr, "an anonymous mapping of one page failed (%s)\n", mapped.error().message().c_str());
        return 1;
    }
    if (!mapped->peek().is_mapped()) {
        std::fprintf(stderr, "a successful mint handed back a region that reports itself unmapped\n");
        return 1;
    }
    if (mapped->peek().size() != kPageBytes) {
        std::fprintf(stderr, "the region reported a length the mint was not given\n");
        return 1;
    }

    // A fresh anonymous mapping is zero-filled, which is the one thing
    // mmap(2) promises about its contents.
    auto* const bytes = static_cast<std::uint8_t*>(mapped->peek().data());
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
    if (auto advised = fixy::mmap::advise<fixy::mmap::advice::Sequential>(ctx, mapped->peek_mut()); !advised) {
        std::fprintf(stderr, "madvise(MADV_SEQUENTIAL) failed (%s)\n", advised.error().message().c_str());
        return 1;
    }

    // Moving the region out of the Linear leaves the wrapper empty and
    // the region owning the pages, so the unmapping happens once, here.
    auto region = std::move(*mapped).consume();
    if (!region.is_mapped()) {
        std::fprintf(stderr, "the region lost its mapping on the way out of the Linear\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int file_mapping_shows_the_file_bytes() {
    IoBlockCtx ctx{eff::testing::test()};

    const int fd = ::memfd_create("fixy-mmap-test", 0);
    if (fd < 0) {
        std::fprintf(stderr, "[skipped] memfd_create is unavailable here (errno %d)\n", errno);
        return 0;
    }
    if (::ftruncate(fd, static_cast<::off_t>(kPageBytes)) != 0) {
        std::fprintf(stderr, "ftruncate on the memfd failed (errno %d)\n", errno);
        ::close(fd);
        return 1;
    }

    std::uint8_t pattern[kPageBytes];
    for (std::size_t index = 0; index < kPageBytes; ++index) {
        pattern[index] = static_cast<std::uint8_t>((index * 7u) & 0xFFu);
    }
    if (::pwrite(fd, pattern, kPageBytes, 0) != static_cast<::ssize_t>(kPageBytes)) {
        std::fprintf(stderr, "pwrite of the pattern failed (errno %d)\n", errno);
        ::close(fd);
        return 1;
    }

    auto mapped = fixy::mmap::mint_mmap<FileRegion, ReadOnly, Shared>(ctx, fd, kPageBytes);
    // The descriptor is not needed once the mapping exists: the mapping
    // holds its own reference to the file.
    ::close(fd);
    if (!mapped) {
        std::fprintf(stderr, "a shared read-only mapping of the memfd failed (%s)\n",
                     mapped.error().message().c_str());
        return 1;
    }

    const auto* const bytes = static_cast<const std::uint8_t*>(mapped->peek().data());
    if (std::memcmp(bytes, pattern, kPageBytes) != 0) {
        std::fprintf(stderr, "the file mapping did not show the bytes the file holds\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int release_aware_advice_discards_the_pages() {
    IoBlockCtx ctx{eff::testing::test()};

    auto mapped = fixy::mmap::mint_mmap_anon<DiscardRegion, WriteAnon, Anonymous>(ctx, kPageBytes);
    if (!mapped) {
        std::fprintf(stderr, "an anonymous mapping for the discard leg failed (%s)\n",
                     mapped.error().message().c_str());
        return 1;
    }

    auto* const bytes = static_cast<std::uint8_t*>(mapped->peek().data());
    std::memset(bytes, 0xA5, kPageBytes);
    if (bytes[0] != 0xA5) {
        std::fprintf(stderr, "the mapping did not take the fill\n");
        return 1;
    }

    // The permission is what admits the dangerous advice.  A root mint
    // is the only way to the first one, and holding it is the claim that
    // no reader of this region is live — which is true here, because
    // this thread is the only one that has ever touched it.
    const auto exclusive = perm::mint_permission_root<DiscardRegion>();

    if (auto discarded =
            fixy::mmap::advise_release_aware<fixy::mmap::advice::DontNeed, DiscardRegion>(ctx, mapped->peek_mut(),
                                                                                          exclusive);
        !discarded) {
        std::fprintf(stderr, "madvise(MADV_DONTNEED) failed (%s)\n", discarded.error().message().c_str());
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

[[nodiscard]] int the_leak_door_hands_back_the_mapping() {
    IoBlockCtx ctx{eff::testing::test()};

    auto mapped = fixy::mmap::mint_mmap_anon<LeakedRegion, WriteAnon, Anonymous>(ctx, kPageBytes);
    if (!mapped) {
        std::fprintf(stderr, "an anonymous mapping for the leak leg failed (%s)\n", mapped.error().message().c_str());
        return 1;
    }

    auto region = std::move(*mapped).consume();
    void* const expected_address = region.data();

    const auto [address, length] =
        std::move(region).release(fixy::atom::leak::resource<UnmappedByTheTestItself>{});
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
    if (const int rc = release_aware_advice_discards_the_pages(); rc != 0) return rc;
    if (const int rc = the_leak_door_hands_back_the_mapping(); rc != 0) return rc;
    return 0;
}
