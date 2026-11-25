// The type of the file of an opened number (the opened descriptor of
// fixy/os/Fd.h): a Handle of the kind File is a regular file, and a Handle of
// the kind CharacterDevice a character device.
//
// Each file door, in each mode, refuses a path that names a character device,
// a terminal, a link to a device, a FIFO or a directory, and the refusal
// closes the number of the open.  A regular file, a file of procfs, a file of
// an exclusive open and a file with no name open.  The device door opens
// /dev/null and /dev/zero, it makes no file, and it refuses each file that is
// no character device.
//
// The last case opens a terminal in a session of its own.  Neither the refusal
// of the file door nor the open of the device door makes it the controlling
// terminal of the process.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <fixy/os/Process.h>
#include <foundation/Quarantine.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Directory.h>
#include <foundation/os/OpenFlags.h>

#include "../scratch_dir.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

// The context of the test, which each check of the Check family takes.
constexpr ::fixy::TestRunnerCtx kTest{::foundation::effects::testing::test()};

}  // namespace

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: a FIFO, a link and a terminal that the base does not make, and the type bits")
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

// The type bits of foundation/os/OpenFlags.h are the bits of the C library.
using ::foundation::os::FileType;
static_assert(::foundation::os::kFileTypeMask == S_IFMT);
static_assert(std::to_underlying(FileType::fifo) == S_IFIFO && std::to_underlying(FileType::directory) == S_IFDIR);
static_assert(std::to_underlying(FileType::character_device) == S_IFCHR
              && std::to_underlying(FileType::block_device) == S_IFBLK);
static_assert(std::to_underlying(FileType::regular) == S_IFREG && std::to_underlying(FileType::socket) == S_IFSOCK);
static_assert(std::to_underlying(FileType::symbolic_link) == S_IFLNK);

// Makes a FIFO at path and keeps it open for reading and writing, so no open of
// it waits for the other end.  It gives the descriptor, or -1.
[[nodiscard]] int make_open_fifo(::crucible::test::ScratchPath const& path) noexcept {
    char const* const text = ::foundation::core::foreign_c_str(path.c_text());
    if (::mkfifo(text, 0600) != 0) return -1;
    return ::open(text, O_RDWR | O_CLOEXEC);
}

void close_fifo(int descriptor) noexcept { static_cast<void>(::close(descriptor)); }

// Makes a symbolic link at path to /dev/null.
[[nodiscard]] bool make_device_link(::crucible::test::ScratchPath const& path) noexcept {
    return ::symlink("/dev/null", ::foundation::core::foreign_c_str(path.c_text())) == 0;
}

// A pair of a terminal: the descriptor of the side that holds it, and the path
// of its device node.
class Terminal final {
public:
    Terminal() noexcept : holder_{::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC)} {
        is_open_ = holder_ >= 0 && ::grantpt(holder_) == 0 && ::unlockpt(holder_) == 0
                && ::ptsname_r(holder_, path_, sizeof path_) == 0;
        while (is_open_ && path_[length_] != '\0') {
            ++length_;
        }
    }
    Terminal(Terminal const&) = delete("a terminal has one owner");
    Terminal& operator=(Terminal const&) = delete("a terminal has one owner");
    ~Terminal() noexcept {
        if (holder_ >= 0) static_cast<void>(::close(holder_));
    }

    [[nodiscard]] bool is_open() const noexcept { return is_open_; }

    // The path of the device node.  The text borrows the terminal.
    [[nodiscard]] ::fixy::TextView path() const& noexcept {
        return ::fixy::mint_foreign_text(path_, length_).characters();
    }

private:
    int holder_ = -1;
    bool is_open_ = false;
    char path_[128] = {};
    std::size_t length_ = 0;
};

// Makes the calling process the leader of a new session, which has no
// controlling terminal.
[[nodiscard]] bool start_a_session() noexcept { return ::setsid() >= 0; }

// True when the process has a controlling terminal, which /dev/tty names.
[[nodiscard]] bool has_controlling_terminal() noexcept {
    int const descriptor = ::open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY);
    if (descriptor < 0) return false;
    static_cast<void>(::close(descriptor));
    return true;
}

}  // namespace
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace src = ::fixy::tags::source;
using ::crucible::test::make_scratch_path;
using ::crucible::test::ScratchPath;
using ::foundation::os::Errno;

using WriteCreate = ::fixy::atom::fs::mode<fs::open_mode::WriteCreate>;
using WriteAppend = ::fixy::atom::fs::mode<fs::open_mode::WriteAppend>;
using WriteTruncate = ::fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;
using ReadWrite = ::fixy::atom::fs::mode<fs::open_mode::ReadWrite>;
using WriteExclusive = ::fixy::atom::fs::mode<fs::open_mode::WriteExclusive>;

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

// The error of a mint, or the zero error when it gave a handle, which ends
// here.
template <class Owned>
[[nodiscard]] Errno error_of(fixy::Result<fixy::Linear<Owned>, Errno> minted) {
    return std::move(minted).match(
        [](fixy::Linear<Owned> opened) {
            static_cast<void>(std::move(opened).consume());
            return Errno{};
        },
        [](Errno error) { return error; });
}

// The count of the descriptors of the process.
[[nodiscard]] std::size_t count_descriptors() {
    std::size_t count = 0;
    foundation::os::for_each_entry("/proc/self/fd", [&count](fixy::CText) noexcept {
        ++count;
        return foundation::os::Visit::proceed;
    }).expect("the list of the descriptors opens");
    return count;
}

// Each mode of mint_file refuses the path, and the refusal leaves no open
// descriptor.  The read-only mode gives eftype.  Each mode that can make a file
// gives of_creating, and the exclusive mode gives eexist, because its path
// names an entry.
void expect_each_mode_refuses(fixy::TextView text, Errno of_creating = Errno::eftype) {
    fixy::Path<src::Sanitized> const path = sanitized(text);
    std::size_t const before = count_descriptors();
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<fs::read_only>(kTest, path)) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteCreate>(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteAppend>(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteTruncate>(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<ReadWrite>(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteExclusive>(kTest, path)) == Errno::eexist);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_durable_truncate_file(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_durable_append_file(kTest, path)) == of_creating);
    CRUCIBLE_TEST_ASSERT(kTest, count_descriptors() == before && "a refusal closes the number of its open");
}

// A path of a device, of a terminal, of a link to a device, of a FIFO or of a
// directory names no File.
void test_no_file_door_opens_another_type() {
    fs::TemporaryDirectory const scratch =
        crucible::test::make_scratch_directory(kTest).expect("the scratch directory opens");
    ScratchPath const fifo_path = make_scratch_path(scratch, "fifo");
    ScratchPath const link_path = make_scratch_path(scratch, "link");
    int const fifo = make_open_fifo(fifo_path);
    CRUCIBLE_TEST_ASSERT(kTest, fifo >= 0 && make_device_link(link_path));
    Terminal const terminal{};
    CRUCIBLE_TEST_ASSERT(kTest, terminal.is_open() && "the host makes a terminal");

    expect_each_mode_refuses("/dev/null");
    expect_each_mode_refuses("/dev/zero");
    expect_each_mode_refuses(terminal.path());
    expect_each_mode_refuses(link_path.c_text().view());
    expect_each_mode_refuses(fifo_path.c_text().view());
    // The kernel itself refuses an open of a directory that can make a file.
    expect_each_mode_refuses(scratch.path().view(), Errno::eisdir);
    close_fifo(fifo);
    fixy::check::pass(kTest, "  test_no_file_door_opens_another_type: PASSED\n");
}

// The doors beneath a directory refuse the same types.  A link at the last part
// gives eloop, because these doors follow none.
void test_no_door_beneath_opens_another_type() {
    fs::DirectoryHandle const devices = fs::open_directory(kTest, sanitized("/dev")).expect("/dev opens");
    fs::TemporaryDirectory const scratch =
        crucible::test::make_scratch_directory(kTest).expect("the scratch directory opens");
    CRUCIBLE_TEST_ASSERT(kTest, make_device_link(make_scratch_path(scratch, "link")));
    std::size_t const before = count_descriptors();
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fs::mint_file_at<fs::read_only>(kTest, devices.ref(), "null")) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file_at<ReadWrite>(kTest, devices.ref(), "null")) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fs::mint_file_at<fs::read_only>(kTest, scratch.ref(), "link")) == Errno::eloop);
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file_at<fs::read_only>(kTest, scratch.ref(), ".")) == Errno::eftype);
    fixy::GrowText text = fixy::mint_grow_text(64);
    CRUCIBLE_TEST_ASSERT(
        kTest, fs::read_whole_at(kTest, kTest.cap().alloc, devices.ref(), "zero", text).err().value_or(Errno{})
                   == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, count_descriptors() == before && "a refusal closes the number of its open");
    fixy::check::pass(kTest, "  test_no_door_beneath_opens_another_type: PASSED\n");
}

// A regular file opens in each mode, and so do a file of procfs and a file
// with no name.
void test_regular_files_open() {
    fs::TemporaryDirectory const scratch =
        crucible::test::make_scratch_directory(kTest).expect("the scratch directory opens");
    ScratchPath const file_path = make_scratch_path(scratch, "file");
    fixy::Path<src::Sanitized> const path = sanitized(file_path.c_text().view());
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteExclusive>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<fs::read_only>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteCreate>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteAppend>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteTruncate>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<ReadWrite>(kTest, path)) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file_at<fs::read_only>(kTest, scratch.ref(), "file")) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_temporary_file_at(kTest, scratch.ref())) == Errno{});
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fs::mint_file<fs::read_only>(kTest, sanitized("/proc/self/status"))) == Errno{});
    fixy::check::pass(kTest, "  test_regular_files_open: PASSED\n");
}

// The device door opens a device with the rights of its access, and the reads
// and the writes of the device work through it.
void test_device_door_opens_devices() {
    using fs::DeviceAccess;
    fs::CharacterDevice<DeviceAccess::read_write> const null =
        fs::mint_character_device<DeviceAccess::read_write>(kTest, sanitized("/dev/null"))
            .expect("/dev/null opens")
            .consume();
    CRUCIBLE_TEST_ASSERT(kTest, fs::write_full(kTest, null.ref(), fixy::TextView{"lost bytes"}).is_ok());
    auto const [read_of_null, no_bytes] = fixy::build_fixed_array<std::byte, 8>(
        [&null](auto run) noexcept { return fs::read_full(kTest, null.ref(), std::move(run)).value_or(99); });
    CRUCIBLE_TEST_ASSERT(kTest, read_of_null == 0 && "/dev/null is at its end at once");

    fs::DirectoryHandle const devices = fs::open_directory(kTest, sanitized("/dev")).expect("/dev opens");
    fs::CharacterDevice<DeviceAccess::read_only> const zero =
        fs::mint_character_device_at<DeviceAccess::read_only>(kTest, devices.ref(), "zero")
            .expect("/dev/zero opens beneath /dev")
            .consume();
    fixy::HeapArray<std::byte> sevens = fixy::mint_heap_array(kTest.cap().alloc, std::size_t{8}, std::byte{7});
    auto [read_of_zero, bytes] = fixy::with_mut(std::move(sevens), [&zero](auto run) noexcept {
        return fs::read_full(kTest, zero.ref(), std::move(run)).value_or(99);
    });
    std::size_t zeros = 0;
    for (std::byte const value : bytes.view()) {
        zeros += value == std::byte{0} ? 1U : 0U;
    }
    CRUCIBLE_TEST_ASSERT(kTest, read_of_zero == 8 && zeros == 8);
    CRUCIBLE_TEST_ASSERT(
        kTest, error_of(fs::mint_character_device<DeviceAccess::write_only>(kTest, sanitized("/dev/null"))) == Errno{});
    fixy::check::pass(kTest, "  test_device_door_opens_devices: PASSED\n");
}

// The device door refuses each file that is no character device, and it makes
// no file at a path that names none.
void test_device_door_refuses_another_type() {
    using fs::DeviceAccess;
    fs::TemporaryDirectory const scratch =
        crucible::test::make_scratch_directory(kTest).expect("the scratch directory opens");
    ScratchPath const file_path = make_scratch_path(scratch, "file");
    ScratchPath const fifo_path = make_scratch_path(scratch, "fifo");
    ScratchPath const missing_path = make_scratch_path(scratch, "missing");
    int const fifo = make_open_fifo(fifo_path);
    CRUCIBLE_TEST_ASSERT(kTest, fifo >= 0 && make_device_link(make_scratch_path(scratch, "link")));
    CRUCIBLE_TEST_ASSERT(kTest, error_of(fs::mint_file<WriteExclusive>(kTest, sanitized(file_path.c_text().view())))
                                    == Errno{});

    std::size_t const before = count_descriptors();
    auto const refusal_of = [](fixy::TextView path) {
        return error_of(fs::mint_character_device<DeviceAccess::read_only>(kTest, sanitized(path)));
    };
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(file_path.c_text().view()) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(fifo_path.c_text().view()) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(scratch.path().view()) == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of("/proc/self/status") == Errno::eftype);
    CRUCIBLE_TEST_ASSERT(kTest, refusal_of(missing_path.c_text().view()) == Errno::enoent);
    CRUCIBLE_TEST_ASSERT(kTest, fs::has_entry_at(kTest, scratch.ref(), "missing").value_or(true) == false);
    CRUCIBLE_TEST_ASSERT(kTest,
                         error_of(fs::mint_character_device_at<DeviceAccess::read_only>(kTest, scratch.ref(), "link"))
                             == Errno::eloop);
    CRUCIBLE_TEST_ASSERT(kTest, count_descriptors() == before && "a refusal closes the number of its open");
    fs::DirectoryHandle const empty{};
    CRUCIBLE_TEST_ASSERT(
        kTest, error_of(fs::mint_character_device_at<DeviceAccess::read_only>(kTest, empty.ref(), "/dev/null"))
                   == Errno::ebadf);
    close_fifo(fifo);
    fixy::check::pass(kTest, "  test_device_door_refuses_another_type: PASSED\n");
}

// The body of the child of the terminal case.  The child leads a session with
// no controlling terminal, and neither door gives it one.
void open_terminal_in_a_session() noexcept {
    using fs::DeviceAccess;
    if (!start_a_session() || has_controlling_terminal()) ::fixy::fatal("the child does not start a session");
    Terminal const terminal{};
    if (!terminal.is_open()) ::fixy::fatal("the host makes no terminal");
    if (error_of(fs::mint_file<ReadWrite>(kTest, sanitized(terminal.path()))) != Errno::eftype) {
        ::fixy::fatal("the file door opened a terminal");
    }
    if (has_controlling_terminal()) ::fixy::fatal("the file door made its terminal the controlling terminal");
    fs::CharacterDevice<DeviceAccess::read_write> const device =
        fs::mint_character_device<DeviceAccess::read_write>(kTest, sanitized(terminal.path()))
            .expect("the device door opens a terminal")
            .consume();
    if (has_controlling_terminal()) ::fixy::fatal("the device door made its terminal the controlling terminal");
    if (!device.is_open()) ::fixy::fatal("the device door gave an empty handle");
}

void test_no_open_takes_a_controlling_terminal() {
    auto run_session = [] noexcept { open_terminal_in_a_session(); };
    ::fixy::process::Child child =
        ::fixy::process::spawn_child(kTest, run_session).expect("the test starts the child of the session");
    const ::fixy::process::ChildReport report =
        ::fixy::process::wait(std::move(child)).expect("the test waits for the child of the session");
    if (!report.is_exit_with(0)) fixy::report(fixy::Sink::Err, "{}", report.output());
    CRUCIBLE_TEST_ASSERT(kTest, report.is_exit_with(0));
    fixy::check::pass(kTest, "  test_no_open_takes_a_controlling_terminal: PASSED\n");
}

}  // namespace

int main() {
    test_no_file_door_opens_another_type();
    test_no_door_beneath_opens_another_type();
    test_regular_files_open();
    test_device_door_opens_devices();
    test_device_door_refuses_another_type();
    test_no_open_takes_a_controlling_terminal();
    ::fixy::check::pass(kTest, "test_os_file_type: all tests passed\n");
    return 0;
}
