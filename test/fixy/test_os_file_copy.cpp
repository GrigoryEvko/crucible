// The copy of a file and the private map of a file (fixy/os/OwnedMmap.h), run
// against the kernel.
//
// mint_file_copy reads a range of a file into memory of its own:
//   - the copy holds the bytes of the range, also at an offset, and the reads
//     leave the position of the file
//   - a range that is not inside the file gives enxio, a negative offset gives
//     einval, a length of zero gives einval, and an empty handle gives ebadf
//   - after the copy, a write to the file and a decrease of its size change no
//     byte of it, and a read of it does not fault
//   - the memory is read-only: the kernel faults in no page of it for a write,
//     where it does for writable memory with no file
//   - an error of the mint unmaps the memory that it made, so the pages that the
//     process maps do not grow.
//
// The control shows the two risks that the licence unchanged_file names.  A
// private map of a regular file shows a write to the file that came after the
// map, and a decrease of the size of the file ends the process with SIGBUS at
// the next read of the lost page, also in a page that this process wrote.
//
// The file is a file with no name under /tmp, so the test leaves no file.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/Pages.h>
#include <fixy/os/Path.h>
#include <fixy/os/Signal.h>
#include <foundation/Linear.h>
#include <foundation/core/Parse.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/File.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace mm = fixy::mmap;
namespace perm = foundation::permissions;

namespace {

using foundation::core::ByteCount;
using foundation::os::Errno;

// The context of the test, which each check of the Check family takes.
constexpr fixy::TestRunnerCtx kTest{eff::testing::test()};

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using WriteCopy = fixy::atom::mmap::with_prot<mm::prot::WriteCopy>;
using Anonymous = fixy::atom::mmap::with_share<mm::share::Anonymous>;
using Private = fixy::atom::mmap::with_share<mm::share::Private>;
using Unchanged = fixy::atom::mmap::unchanged_file;

struct FileRegion {
    using permission_row = eff::Row<>;
};

constexpr std::size_t kPageBytes = 4096;
constexpr std::size_t kWordsOfPage = kPageBytes / sizeof(std::uint64_t);
constexpr std::int64_t kFileBytes = 2 * static_cast<std::int64_t>(kPageBytes);

// The words of one page of the file.
using PageWords = fixy::FixedArray<std::uint64_t, kWordsOfPage>;

// The region of a mint, or the error of the mint.
template <class Region>
[[nodiscard]] fixy::Result<Region, Errno> region_of(fixy::Result<fixy::Linear<Region>, Errno> minted) {
    CRUCIBLE_TRY(made, std::move(minted));
    return std::move(made).consume();
}

// The error of a mint of a mapping, or no error.  A mapping that the mint gave
// unmaps at once.
template <class Region>
[[nodiscard]] Errno error_of(fixy::Result<fixy::Linear<Region>, Errno> minted) {
    return region_of(std::move(minted)).err().value_or(Errno{});
}

// The word at index of the page of index page of the file.
[[nodiscard]] std::uint64_t word_of_file(std::size_t page, std::size_t index) {
    return (page + 1) * 1000003 + index * 7;
}

// Writes the words of the two pages at the position of the file.
[[nodiscard]] bool fill(IoBlockCtx const& ctx, fs::TemporaryFile const& file) {
    for (std::size_t page = 0; page < 2; ++page) {
        PageWords words{};
        for (auto const index : PageWords::indices())
            words.set(index, word_of_file(page, index.raw()));
        if (fs::write_full(ctx, file.ref(), fixy::bytes(words)).is_err()) return false;
    }
    return true;
}

// True when the words of the page of a region are the words of the page of the
// file.
template <class Region>
[[nodiscard]] bool shows_page(Region const& region, std::size_t region_page, std::size_t file_page) {
    for (fixy::View<std::uint64_t const, kWordsOfPage> const words :
         region.template view_at<std::uint64_t const, kWordsOfPage>(region_page * kPageBytes)) {
        for (auto const index : words.indices()) {
            if (words.at(index) != word_of_file(file_page, index.raw())) return false;
        }
        return true;
    }
    return false;
}

// The first word of the page of a region, or zero when the region has no such
// page.
template <class Region>
[[nodiscard]] std::uint64_t first_word(Region const& region, std::size_t page) {
    for (fixy::View<std::uint64_t const, 1> const word :
         region.template view_at<std::uint64_t const, 1>(page * kPageBytes)) {
        return word.at(fixy::index<0>());
    }
    return 0;
}

// A file with no name under /tmp, or an empty handle when the open failed.
[[nodiscard]] fs::TemporaryFile temporary_file(IoBlockCtx const& ctx) {
    auto const tmp = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(fixy::TextView{"/tmp"}))
                         .expect("/tmp passes the check");
    fs::DirectoryHandle const directory = fs::open_directory(ctx, tmp).expect("/tmp opens");
    return fs::mint_temporary_file_at(ctx, directory.ref())
        .match([](fixy::Linear<fs::TemporaryFile> opened) { return std::move(opened).consume(); },
               [](Errno) { return fs::TemporaryFile{}; });
}

// The copy holds the bytes of its range, and the reads leave the position of
// the file.
template <class Brand>
void test_a_copy_holds_its_range(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner,
                                 fs::TemporaryFile const& file) {
    auto const whole =
        region_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{2 * kPageBytes})).expect("the copy of the file");
    CRUCIBLE_TEST_ASSERT(kTest, whole.size() == 2 * kPageBytes);
    CRUCIBLE_TEST_ASSERT(kTest, shows_page(whole, 0, 0) && shows_page(whole, 1, 1));
    auto const second = region_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kPageBytes},
                                                     fs::FileOffset{static_cast<std::int64_t>(kPageBytes)}))
                            .expect("the copy of the second page");
    CRUCIBLE_TEST_ASSERT(kTest, shows_page(second, 0, 1));
    // The position of the file is at its end, where the writes left it, so a
    // read at the position gives no byte.
    auto [read, buffer] = fixy::build_fixed_array<std::byte, 8>([&ctx, &file](auto run) noexcept -> std::size_t {
        return fs::read_full(ctx, file.ref(), std::move(run)).value_or(std::size_t{99});
    });
    CRUCIBLE_TEST_ASSERT(kTest, read == 0);
    fixy::check::pass(kTest, "  test_a_copy_holds_its_range: PASSED\n");
}

// The refusals of the mint.
template <class Brand>
void test_a_copy_refuses_a_range_outside_the_file(IoBlockCtx const& ctx,
                                                  perm::Permission<FileRegion, Brand> const& owner,
                                                  fs::TemporaryFile const& file) {
    CRUCIBLE_TEST_ASSERT(kTest, error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{2 * kPageBytes + 1}))
                                    == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kPageBytes},
                                                            fs::FileOffset{kFileBytes - 1}))
                                    == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(
        kTest, error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kPageBytes}, fs::FileOffset{kFileBytes}))
                   == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kPageBytes}, fs::FileOffset{-1}))
                             == Errno::einval);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{0})) == Errno::einval);
    fs::TemporaryFile const empty{};
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(mm::mint_file_copy(ctx, owner, empty.ref(), ByteCount{kPageBytes})) == Errno::ebadf);
    fixy::check::pass(kTest, "  test_a_copy_refuses_a_range_outside_the_file: PASSED\n");
}

// The pages that the process maps, the first field of /proc/self/statm, or zero
// when the file does not read.
[[nodiscard]] std::uint64_t mapped_pages() {
    fixy::FixedText<128> text{};
    if (foundation::os::read_file(foundation::core::CText{"/proc/self/statm"}, text).is_err()) return 0;
    fixy::TextView const all = text.view();
    std::size_t const space = foundation::core::find(all, ' ').value_or(0);
    return foundation::core::parse<std::uint64_t>(all.window(0, space).value_or(fixy::TextView{})).value_or(0);
}

// The refused mints of the case below, and the pages of one of them.
constexpr std::size_t kRefusals = 64;
constexpr std::size_t kRefusedPages = std::size_t{1} << 14;

// A refused mint unmaps the memory that it made.  Memory that stayed would
// add 1 GiB for each 16 refusals, and only two pages of each are touched.
template <class Brand>
void test_a_refused_copy_unmaps_its_memory(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner,
                                           fs::TemporaryFile const& file) {
    std::uint64_t const before = mapped_pages();
    CRUCIBLE_TEST_ASSERT(kTest, before != 0);
    for (std::size_t refusal = 0; refusal < kRefusals; ++refusal) {
        CRUCIBLE_TEST_ASSERT(kTest,
                             error_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kRefusedPages * kPageBytes}))
                                 == Errno::enxio);
    }
    std::uint64_t const after = mapped_pages();
    CRUCIBLE_TEST_ASSERT(kTest, after < before + kRefusedPages);
    fixy::check::pass(kTest, "  test_a_refused_copy_unmaps_its_memory: PASSED\n");
}

// The memory of a copy is read-only.  The populate door faults in pages for a
// write, and it gives an error for memory that the process cannot write.  A
// kernel that gives the error for writable memory too shows nothing.
template <class Brand>
void test_a_copy_is_read_only(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner,
                              fs::TemporaryFile const& file) {
    auto writable = region_of(mm::mint_mmap_anon<WriteCopy, Anonymous>(ctx, owner, kPageBytes)).expect("a page maps");
    auto const copy =
        region_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{kPageBytes})).expect("the copy of a page");
    bool is_writable_populated = false;
    for (fixy::View<std::byte const> const bytes : writable.template view<std::byte const>()) {
        is_writable_populated = fixy::pages::populate_pages(ctx, fixy::pages::storage_of(bytes)).is_ok();
    }
    if (!is_writable_populated) {
        fixy::report(fixy::Sink::Out, "[skipped] the kernel has no MADV_POPULATE_WRITE\n");
        return;
    }
    bool is_copy_populated = true;
    for (fixy::View<std::byte const> const bytes : copy.template view<std::byte const>()) {
        is_copy_populated = fixy::pages::populate_pages(ctx, fixy::pages::storage_of(bytes)).is_ok();
    }
    CRUCIBLE_TEST_ASSERT(kTest, !is_copy_populated);
    CRUCIBLE_TEST_ASSERT(kTest, shows_page(copy, 0, 0));
    fixy::check::pass(kTest, "  test_a_copy_is_read_only: PASSED\n");
}

// A write to the file and a decrease of its size after the copy change no byte
// of the copy.
template <class Brand>
void test_a_copy_keeps_its_bytes(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner) {
    fs::TemporaryFile const file = temporary_file(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, file.is_open() && fill(ctx, file));
    auto const copy =
        region_of(mm::mint_file_copy(ctx, owner, file.ref(), ByteCount{2 * kPageBytes})).expect("the copy of the file");
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{0}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, shows_page(copy, 0, 0) && shows_page(copy, 1, 1));
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{kFileBytes}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, shows_page(copy, 0, 0) && shows_page(copy, 1, 1));
    fixy::check::pass(kTest, "  test_a_copy_keeps_its_bytes: PASSED\n");
}

// The control of the licence unchanged_file.  The map shows the zero of a file
// that another handle made shorter and longer again, and each read of a page
// past the end ends the process with SIGBUS, also of a page that this process
// wrote.
template <class Brand>
void shows_the_risks_of_the_licence(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner) {
    fs::TemporaryFile const file = temporary_file(ctx);
    CRUCIBLE_TEST_ASSERT(kTest, file.is_open() && fill(ctx, file));
    auto mapping = region_of(mm::mint_mmap<WriteCopy, Private, Unchanged>(ctx, owner, file.ref(), 2 * kPageBytes))
                       .expect("the private map of the file");
    CRUCIBLE_TEST_ASSERT(kTest, first_word(mapping, 0) == word_of_file(0, 0));
    // The second page is a private copy from here on.
    for (fixy::View<std::uint64_t, 1> const word : mapping.template view_at<std::uint64_t, 1>(kPageBytes)) {
        word.at(fixy::index<0>()) = 5;
    }
    CRUCIBLE_TEST_ASSERT(kTest, first_word(mapping, 1) == 5);
    // Another handle of the file takes the words of the first page away and
    // gives the page zeros.  The map shows the change of the page that this
    // process did not write.
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{0}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{kFileBytes}).is_ok());
    CRUCIBLE_TEST_ASSERT(kTest, first_word(mapping, 0) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, first_word(mapping, 1) == 0);
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{0}).is_ok());
    fixy::check::expect_dies_by(
        kTest, [&mapping]() noexcept { static_cast<void>(first_word(mapping, 0)); },
        fixy::signal::FatalSignal::bus_error);
    fixy::check::expect_dies_by(
        kTest, [&mapping]() noexcept { static_cast<void>(first_word(mapping, 1)); },
        fixy::signal::FatalSignal::bus_error);
    fixy::check::pass(kTest, "  shows_the_risks_of_the_licence: PASSED\n");
}

}  // namespace

int main() {
    fixy::report(fixy::Sink::Out, "test_os_file_copy:\n");
    IoBlockCtx const ctx{eff::testing::test()};
    fs::TemporaryFile const file = temporary_file(ctx);
    if (!file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the file system of /tmp has no file with no name\n");
        return 0;
    }
    CRUCIBLE_TEST_ASSERT(kTest, fill(ctx, file));
    auto owner = perm::mint_permission_root<FileRegion>();
    test_a_copy_holds_its_range(ctx, owner, file);
    test_a_copy_refuses_a_range_outside_the_file(ctx, owner, file);
    test_a_refused_copy_unmaps_its_memory(ctx, owner, file);
    test_a_copy_is_read_only(ctx, owner, file);
    test_a_copy_keeps_its_bytes(ctx, owner);
    shows_the_risks_of_the_licence(ctx, owner);
    foundation::linear::drop(std::move(owner));
    fixy::check::pass(kTest, "test_os_file_copy: all PASSED\n");
    return 0;
}
