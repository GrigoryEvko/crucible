// Sealed memory and the end of a shared file (fixy/os/Fs.h and
// fixy/os/OwnedMmap.h), run against the kernel.
//
// A shared map of sealed memory never faults past an end:
//   - a map whose range is not inside the memory gives enxio
//   - a second open of the memory, through /proc/self/fd, is a regular file
//     to this process, as it is to each other one.  A decrease of its size
//     gives eperm, an increase is admitted, and the maps read and write on.
//
// The mint takes the pages of the memory, and an error of the mint closes the
// descriptor that it made.  The proof of the close is the number of the next
// descriptor, which is the lowest number that is free.
//
// The control shows the risk that the licence faults_on_truncate names: a
// shared map of a regular file ends the process with SIGBUS after a decrease
// of the size of the file.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Mmap.h>
#include <fixy/os/OwnedMmap.h>
#include <fixy/os/Path.h>
#include <fixy/os/Signal.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/PathText.h>
#include <foundation/permissions/Permission.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace eff = foundation::effects;
namespace fs = fixy::fs;
namespace mm = fixy::mmap;
namespace perm = foundation::permissions;

namespace {

using foundation::os::Errno;

// The context of the test, which each check of the Check family takes.
constexpr fixy::TestRunnerCtx kTest{eff::testing::test()};

using IoBlockCtx = eff::ExecCtx<eff::Test, eff::Row<eff::Effect::Test, eff::Effect::Alloc, eff::Effect::IO,
                                                    eff::Effect::Block, eff::Effect::Nondet>>;
using ReadWrite = fixy::atom::mmap::with_prot<mm::prot::ReadWrite>;
using Shared = fixy::atom::mmap::with_share<mm::share::Shared>;
using Faults = fixy::atom::mmap::faults_on_truncate;
using OpenReadWrite = fixy::atom::fs::mode<fs::open_mode::ReadWrite>;

struct SealedRegion {
    using permission_row = eff::Row<>;
};
struct FileRegion {
    using permission_row = eff::Row<>;
};

constexpr std::size_t kPageBytes = 4096;
constexpr std::int64_t kMemoryBytes = 2 * static_cast<std::int64_t>(kPageBytes);

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

// Sealed memory of length bytes, or an empty handle with the error.
struct Minted {
    fs::SealedMemory memory{};
    Errno error{};
};

[[nodiscard]] Minted mint_memory(IoBlockCtx const& ctx, std::int64_t length) {
    return fs::mint_sealed_memory(ctx, fs::FileOffset{length})
        .match([](fixy::Linear<fs::SealedMemory> made) { return Minted{.memory = std::move(made).consume()}; },
               [](Errno error) { return Minted{.error = error}; });
}

// A second open of the memory of a path as a regular file, or an empty handle
// with the error.
struct Opened {
    fs::FileOf<OpenReadWrite> file{};
    Errno error{};
};

[[nodiscard]] Opened open_again(IoBlockCtx const& ctx, fixy::Path<fixy::tags::source::Sanitized> const& path) {
    return fs::mint_file<OpenReadWrite>(ctx, path).match(
        [](fixy::Linear<fs::FileOf<OpenReadWrite>> opened) { return Opened{.file = std::move(opened).consume()}; },
        [](Errno error) { return Opened{.error = error}; });
}

// The number of a descriptor, in decimal.
using NumberText = foundation::os::PathText<64>;

[[nodiscard]] NumberText number_of(fs::SealedMemory const& memory) {
    NumberText text{};
    fixy::os::append_descriptor_number(text, memory.ref()).expect("a number fits the text");
    return text;
}

// The word at the start of the page of index page of a mapping, which the
// test writes and reads.
template <class Mapping>
[[nodiscard]] fixy::Option<mm::SharedView<std::uint64_t, 1>> word_of_page(Mapping& mapping, std::size_t page) {
    return mapping.template view_at<std::uint64_t, 1>(page * kPageBytes);
}

// Writes value through one map and reads it through another one.
template <class First, class Second>
[[nodiscard]] bool shares_a_word(First& first, Second& second, std::size_t page, std::uint64_t value) {
    for (mm::SharedView<std::uint64_t, 1> const written : word_of_page(first, page)) {
        written.store_release(fixy::index<0>(), value);
    }
    bool is_shared = false;
    for (mm::SharedView<std::uint64_t, 1> const read : word_of_page(second, page)) {
        is_shared = read.load_acquire(fixy::index<0>()) == value;
    }
    return is_shared;
}

// A length that is not above zero is refused, and the refusal leaves no
// descriptor: the next memory takes the number that the memory before the
// refusals had.
void refuses_a_length_and_keeps_no_descriptor(IoBlockCtx const& ctx) {
    NumberText before{};
    {
        Minted const first = mint_memory(ctx, kMemoryBytes);
        CRUCIBLE_TEST_ASSERT(kTest, first.memory.is_open());
        before = number_of(first.memory);
    }
    CRUCIBLE_TEST_ASSERT(kTest, mint_memory(ctx, 0).error == Errno::einval);
    CRUCIBLE_TEST_ASSERT(kTest, mint_memory(ctx, -1).error == Errno::einval);
    Minted const after = mint_memory(ctx, kMemoryBytes);
    CRUCIBLE_TEST_ASSERT(kTest, after.memory.is_open());
    NumberText const number = number_of(after.memory);
    CRUCIBLE_TEST_ASSERT(kTest, number.c_text().view() == before.c_text().view());
    fixy::check::pass(kTest, "  refuses_a_length_and_keeps_no_descriptor: PASSED\n");
}

// Each map of a range outside the memory gives enxio: one page too long, at
// an offset past the end, and one page too long at an offset inside.  The
// borrow of an empty handle gives ebadf.
template <class Brand>
void refuses_a_map_past_the_end(IoBlockCtx const& ctx, perm::Permission<SealedRegion, Brand> const& owner) {
    Minted const minted = mint_memory(ctx, kMemoryBytes);
    CRUCIBLE_TEST_ASSERT(kTest, minted.memory.is_open());
    auto const error_at = [&ctx, &owner, &minted](std::size_t length, std::int64_t offset) {
        return error_of(
            mm::mint_mmap<ReadWrite, Shared>(ctx, owner, minted.memory.ref(), length, fs::FileOffset{offset}));
    };
    constexpr std::int64_t kPage = static_cast<std::int64_t>(kPageBytes);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(3 * kPageBytes, 0) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(2 * kPageBytes + 1, 0) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(2 * kPageBytes, kPage) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(kPageBytes, 2 * kPage) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(kPageBytes, 3 * kPage) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(kPageBytes, -kPage) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(static_cast<std::size_t>(-1), 0) == Errno::enxio);
    CRUCIBLE_TEST_ASSERT(kTest, error_at(2 * kPageBytes, 0) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_at(kPageBytes, kPage) == Errno{});
    fs::SealedMemory const empty{};
    CRUCIBLE_TEST_ASSERT(kTest, error_of(mm::mint_mmap<ReadWrite, Shared>(ctx, owner, empty.ref(), kPageBytes))
                                    == Errno::ebadf);
    fixy::check::pass(kTest, "  refuses_a_map_past_the_end: PASSED\n");
}

// A second open of the memory as a regular file cannot make it shorter, and
// it can make it longer.  The map of before the change reads and writes each
// page on, and the map of the new page is admitted after the increase.
template <class Brand>
void keeps_its_size_for_each_open(IoBlockCtx const& ctx, perm::Permission<SealedRegion, Brand> const& owner) {
    Minted const minted = mint_memory(ctx, kMemoryBytes);
    CRUCIBLE_TEST_ASSERT(kTest, minted.memory.is_open());
    auto first = region_of(mm::mint_mmap<ReadWrite, Shared>(ctx, owner, minted.memory.ref(), 2 * kPageBytes))
                     .expect("the map of the memory");
    auto second = region_of(mm::mint_mmap<ReadWrite, Shared>(ctx, owner, minted.memory.ref(), 2 * kPageBytes))
                      .expect("the second map of the memory");
    CRUCIBLE_TEST_ASSERT(kTest, shares_a_word(first, second, 0, 11) && shares_a_word(second, first, 1, 13));

    NumberText path{};
    path.append(fixy::TextView{"/proc/self/fd/"}).expect("the path fits the text");
    fixy::os::append_descriptor_number(path, minted.memory.ref()).expect("the number fits the text");
    auto const sanitized = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(path.c_text().view()))
                               .expect("the path passes the check");
    Opened const again = open_again(ctx, sanitized);
    if (!again.file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the memory does not open again through /proc/self/fd (errno {})\n",
                     foundation::os::error_number(again.error));
        return;
    }
    CRUCIBLE_TEST_ASSERT(kTest, fs::file_size(ctx, again.file.ref()).value_or(-1) == kMemoryBytes);
    for (std::int64_t const shorter : fixy::FixedArray<std::int64_t, 3>{0, 1, kMemoryBytes - 1}) {
        Errno const refused = fs::set_file_size(ctx, again.file.ref(), fs::FileOffset{shorter}).err().value_or(Errno{});
        CRUCIBLE_TEST_ASSERT(kTest, refused == Errno::eperm);
    }
    CRUCIBLE_TEST_ASSERT(kTest, fs::file_size(ctx, again.file.ref()).value_or(-1) == kMemoryBytes);
    CRUCIBLE_TEST_ASSERT(kTest, shares_a_word(first, second, 0, 17) && shares_a_word(second, first, 1, 19));

    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, again.file.ref(), fs::FileOffset{kMemoryBytes + 4096}).is_ok());
    auto longer = region_of(mm::mint_mmap<ReadWrite, Shared>(ctx, owner, minted.memory.ref(), 3 * kPageBytes))
                      .expect("the map of the longer memory");
    CRUCIBLE_TEST_ASSERT(kTest, shares_a_word(longer, first, 1, 23) && shares_a_word(longer, longer, 2, 29));
    fixy::check::pass(kTest, "  keeps_its_size_for_each_open: PASSED\n");
}

// The control: a shared map of a regular file, which the licence admits, ends
// the process with SIGBUS when the file gets shorter.
template <class Brand>
void shows_the_risk_of_the_licence(IoBlockCtx const& ctx, perm::Permission<FileRegion, Brand> const& owner) {
    auto const tmp = fixy::sanitize_path(fixy::mint_tagged<fixy::tags::source::External>(fixy::TextView{"/tmp"}))
                         .expect("/tmp passes the check");
    fs::DirectoryHandle const directory = fs::open_directory(ctx, tmp).expect("/tmp opens");
    fs::TemporaryFile const file =
        fs::mint_temporary_file_at(ctx, directory.ref())
            .match([](fixy::Linear<fs::TemporaryFile> opened) { return std::move(opened).consume(); },
                   [](Errno) { return fs::TemporaryFile{}; });
    if (!file.is_open()) {
        fixy::report(fixy::Sink::Out, "[skipped] the file system of /tmp has no file with no name\n");
        return;
    }
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{kMemoryBytes}).is_ok());
    auto mapping = region_of(mm::mint_mmap<ReadWrite, Shared, Faults>(ctx, owner, file.ref(), 2 * kPageBytes))
                       .expect("the map of the file");
    CRUCIBLE_TEST_ASSERT(kTest, fs::set_file_size(ctx, file.ref(), fs::FileOffset{0}).is_ok());
    fixy::check::expect_dies_by(
        kTest,
        [&mapping]() noexcept {
            for (mm::SharedView<std::uint64_t, 1> const word : word_of_page(mapping, 1)) {
                word.store_release(fixy::index<0>(), 1);
            }
        },
        fixy::signal::FatalSignal::bus_error);
    fixy::check::pass(kTest, "  shows_the_risk_of_the_licence: PASSED\n");
}

}  // namespace

int main() {
    IoBlockCtx const ctx{eff::testing::test()};
    if (mint_memory(ctx, kMemoryBytes).error == Errno::enosys) {
        fixy::report(fixy::Sink::Out, "[skipped] the kernel has no memory files\n");
        return 0;
    }
    refuses_a_length_and_keeps_no_descriptor(ctx);
    auto sealed_owner = perm::mint_permission_root<SealedRegion>();
    refuses_a_map_past_the_end(ctx, sealed_owner);
    keeps_its_size_for_each_open(ctx, sealed_owner);
    foundation::linear::drop(std::move(sealed_owner));
    auto file_owner = perm::mint_permission_root<FileRegion>();
    shows_the_risk_of_the_licence(ctx, file_owner);
    foundation::linear::drop(std::move(file_owner));
    fixy::check::pass(kTest, "test_os_sealed_memory: all PASSED\n");
    return 0;
}
