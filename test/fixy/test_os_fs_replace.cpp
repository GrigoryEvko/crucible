// The durable replace of fixy/os/Fs.h (THE DURABLE REPLACE): replace_file_at
// and replace_file put new bytes in place of a file through a temporary file,
// a sync and a rename.
//
// The cases replace a file two times, stop the write of the temporary file
// after half of the bytes with an error and with a crash, refuse a name that
// is not one final name, give eexist when each temporary name is in use, and
// replace a file through a path whose directory part is a symbolic link.
// CMake makes that link in the directory of the test before each run
// (replace_fixture.cmake).

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/effects/Effect.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace eff = ::foundation::effects;
namespace fs = ::fixy::fs;
namespace src = ::fixy::tags::source;
using ::foundation::os::Errno;

using Exclusive = ::fixy::atom::fs::mode<fs::open_mode::WriteExclusive>;
using ReadFile = fs::FileOf<fs::read_only>;
using Step = ::fixy::Result<::fixy::Unit, Errno>;

// The context of each door of the test.
constexpr ::fixy::TestRunnerCtx kCtx{eff::testing::test()};

// The bytes of one file of the test.
using Contents = ::fixy::FixedList<std::byte, 128>;

constexpr ::fixy::TextView kOldText{"the whole old file\n"};
constexpr ::fixy::TextView kNewText{"the whole new file, which is longer than the old one\n"};

// The text of the stop of the crash case.
constexpr ::fixy::TextView kCrashText{"the write of the temporary file stops after half of the bytes"};

[[nodiscard]] fixy::Path<src::Sanitized> sanitized(fixy::TextView text) {
    return fixy::sanitize_path(fixy::mint_tagged<src::External>(text)).expect("each test path passes the sanitizer");
}

[[nodiscard]] fs::TemporaryDirectory make_owner() {
    return std::move(fs::mint_temporary_directory(kCtx).expect("the temporary directory opens")).consume();
}

[[nodiscard]] Contents contents_of(fixy::TextView text) {
    Contents bytes{};
    for (char const character : text) {
        static_cast<void>(bytes.append(static_cast<std::byte>(static_cast<unsigned char>(character)))
                              .expect("the text of a case fits its list"));
    }
    return bytes;
}

// The first half of the bytes.
[[nodiscard]] fixy::View<std::byte const> half_of(Contents const& bytes) {
    return bytes.view().window(0, bytes.size() / 2).expect("a half of the bytes is in the bytes");
}

// The bytes of the file name under dir, or no value when it does not open.
template <::fixy::os::RightSet Rights>
[[nodiscard]] fixy::Option<Contents> read_entry(fs::DirectoryRef<Rights> dir, fixy::CText name) {
    return fs::mint_file_at<fs::read_only>(kCtx, dir, name)
        .match(
            [](fixy::Linear<ReadFile> opened) -> fixy::Option<Contents> {
                ReadFile const file = std::move(opened).consume();
                fixy::FixedArray<std::byte, 128> buffer{};
                std::size_t const count = buffer.with_mut(
                    [&file](auto run) noexcept { return fs::read_full(kCtx, file.ref(), run.view()).value_or(0); });
                fixy::FixedArray<std::byte, 128> const& filled = buffer;
                Contents read{};
                for (std::byte const byte : filled.view().window(0, count).expect("the read fills its buffer")) {
                    static_cast<void>(read.append(byte).expect("the read fits its list"));
                }
                return fixy::some(read);
            },
            [](Errno) -> fixy::Option<Contents> { return fixy::none; });
}

// True when the file name under dir holds the bytes.
template <::fixy::os::RightSet Rights>
[[nodiscard]] bool holds_bytes(fs::DirectoryRef<Rights> dir, fixy::CText name, fixy::View<std::byte const> bytes) {
    return read_entry(dir, name).is_some_and(
        [bytes](Contents const& read) { return read.size() == bytes.size() && fixy::equal(read.view(), bytes); });
}

// True when an entry is at name under dir.
template <::fixy::os::RightSet Rights>
[[nodiscard]] bool holds_entry(fs::DirectoryRef<Rights> dir, fixy::CText name) {
    return fs::has_entry_at(kCtx, dir, name).value_or(true);
}

// The temporary name of try attempt of a replace of "file".
[[nodiscard]] ::foundation::os::PathText<64> temporary_name(std::uint64_t attempt) {
    ::foundation::os::PathText<64> name{};
    name.append(fixy::TextView{"file.tmp-"}).expect("the temporary name fits its text");
    name.append_hex<2>(attempt).expect("the temporary name fits its text");
    return name;
}

// A replace of a missing file makes it, and a second replace puts the new bytes
// in place of the old ones.  Each replace leaves no temporary file.
[[nodiscard]] int check_replace_makes_and_replaces() {
    fs::TemporaryDirectory const dir = make_owner();
    Contents const old_bytes = contents_of(kOldText);
    Contents const new_bytes = contents_of(kNewText);
    if (fs::replace_file_at(kCtx, dir.ref(), "file", old_bytes.view()).is_err()) return 1;
    if (!holds_bytes(dir.ref(), "file", old_bytes.view())) return 2;
    if (fs::replace_file_at(kCtx, dir.ref(), "file", new_bytes.view()).is_err()) return 3;
    if (!holds_bytes(dir.ref(), "file", new_bytes.view())) return 4;
    ::foundation::os::PathText<64> const first = temporary_name(0);
    if (holds_entry(dir.ref(), first.c_text())) return 5;
    return 0;
}

// A write of the temporary file that gives an error after half of the bytes
// gives that error.  The old file stays whole, and the door removes the
// temporary file.
[[nodiscard]] int check_failed_write_keeps_the_old_file() {
    fs::TemporaryDirectory const dir = make_owner();
    Contents const old_bytes = contents_of(kOldText);
    Contents const new_bytes = contents_of(kNewText);
    if (fs::replace_file_at(kCtx, dir.ref(), "file", old_bytes.view()).is_err()) return 11;
    Step const replaced = fs::detail::replace_file_steps_(kCtx, dir.ref(), "file", [&new_bytes](auto file) -> Step {
        static_cast<void>(fs::write_full(kCtx, file, half_of(new_bytes)).is_ok());
        return fixy::err(Errno::enospc);
    });
    if (!replaced.is_err_and([](Errno const& error) { return error == Errno::enospc; })) return 12;
    if (!holds_bytes(dir.ref(), "file", old_bytes.view())) return 13;
    ::foundation::os::PathText<64> const first = temporary_name(0);
    if (holds_entry(dir.ref(), first.c_text())) return 14;
    return 0;
}

// A crash between the write and the rename: the write of the temporary file
// stops after half of the bytes, and the process ends.  The old file stays
// whole, and the temporary file of the crash stays with the half.  The next
// replace takes the next temporary name.
[[nodiscard]] int check_crash_before_the_rename_keeps_the_old_file() {
    fs::TemporaryDirectory const dir = make_owner();
    Contents const old_bytes = contents_of(kOldText);
    Contents const new_bytes = contents_of(kNewText);
    if (fs::replace_file_at(kCtx, dir.ref(), "file", old_bytes.view()).is_err()) return 21;
    fixy::check::expect_dies(
        kCtx,
        [&dir, &new_bytes] {
            static_cast<void>(fs::detail::replace_file_steps_(kCtx, dir.ref(), "file", [&new_bytes](auto file) -> Step {
                                  static_cast<void>(fs::write_full(kCtx, file, half_of(new_bytes)).is_ok());
                                  fixy::fatal("the write of the temporary file stops after half of the bytes");
                              }).is_ok());
        },
        kCrashText);
    ::foundation::os::PathText<64> const first = temporary_name(0);
    ::foundation::os::PathText<64> const second = temporary_name(1);
    if (!holds_bytes(dir.ref(), "file", old_bytes.view())) return 22;
    if (!holds_bytes(dir.ref(), first.c_text(), half_of(new_bytes))) return 23;
    if (fs::replace_file_at(kCtx, dir.ref(), "file", new_bytes.view()).is_err()) return 24;
    if (!holds_bytes(dir.ref(), "file", new_bytes.view())) return 25;
    if (holds_entry(dir.ref(), second.c_text())) return 26;
    return 0;
}

// A name that is not one final name gives einval, and the door makes no file.
[[nodiscard]] int check_name_is_one_final_name() {
    fs::TemporaryDirectory const dir = make_owner();
    Contents const bytes = contents_of(kOldText);
    if (fs::make_directory_at(kCtx, dir.ref(), "inner").is_err()) return 31;
    fixy::FixedArray<fixy::CText, 4> const names{fixy::CText{""}, fixy::CText{"inner/file"}, fixy::CText{"."},
                                                 fixy::CText{".."}};
    for (fixy::CText const name : names) {
        if (!fs::replace_file_at(kCtx, dir.ref(), name, bytes.view()).is_err_and([](Errno const& error) {
                return error == Errno::einval;
            })) {
            return 32;
        }
    }
    if (holds_entry(dir.ref(), "inner/file")) return 33;
    return 0;
}

// When each temporary name is in use, the door gives eexist, and the old file
// stays whole.
[[nodiscard]] int check_each_name_in_use_gives_eexist() {
    fs::TemporaryDirectory const dir = make_owner();
    Contents const old_bytes = contents_of(kOldText);
    if (fs::replace_file_at(kCtx, dir.ref(), "file", old_bytes.view()).is_err()) return 41;
    for (std::uint64_t attempt = 0; attempt < fs::kReplaceTries; ++attempt) {
        ::foundation::os::PathText<64> const name = temporary_name(attempt);
        bool const is_made = fs::mint_file_at<Exclusive>(kCtx, dir.ref(), name.c_text())
                                 .match(
                                     [](fixy::Linear<fs::FileOf<Exclusive>> opened) {
                                         fs::FileOf<Exclusive> const file = std::move(opened).consume();
                                         return file.is_open();
                                     },
                                     [](Errno) { return false; });
        if (!is_made) return 42;
    }
    Contents const new_bytes = contents_of(kNewText);
    if (!fs::replace_file_at(kCtx, dir.ref(), "file", new_bytes.view()).is_err_and([](Errno const& error) {
            return error == Errno::eexist;
        })) {
        return 43;
    }
    if (!holds_bytes(dir.ref(), "file", old_bytes.view())) return 44;
    return 0;
}

// replace_file opens the directory part of its path, and a symbolic link at
// the last part of that part opens as its target.  A path with no directory
// part takes the current directory, and a path of a directory gives eisdir or
// einval.
[[nodiscard]] int check_replace_of_a_path() {
    Contents const bytes = contents_of(kOldText);
    if (fs::replace_file(kCtx, sanitized("replace_fixture/link/file"), bytes.view()).is_err()) return 51;
    fs::DirectoryHandle const target =
        fs::open_directory(kCtx, sanitized("replace_fixture/target")).expect("the directory of the fixture opens");
    if (!holds_bytes(target.ref(), "file", bytes.view())) return 52;
    if (fs::unlink_at(kCtx, target.ref(), "file").is_err()) return 53;
    if (fs::replace_file(kCtx, sanitized("replace-bare-file"), bytes.view()).is_err()) return 54;
    fs::DirectoryHandle const here = fs::open_directory(kCtx, sanitized(".")).expect("the current directory opens");
    if (!holds_bytes(here.ref(), "replace-bare-file", bytes.view())) return 55;
    if (fs::unlink_at(kCtx, here.ref(), "replace-bare-file").is_err()) return 56;
    if (!fs::replace_file(kCtx, sanitized("/"), bytes.view()).is_err_and([](Errno const& error) {
            return error == Errno::eisdir;
        })) {
        return 57;
    }
    if (!fs::replace_file(kCtx, sanitized("replace_fixture/target/"), bytes.view()).is_err_and([](Errno const& error) {
            return error == Errno::einval;
        })) {
        return 58;
    }
    return 0;
}

}  // namespace

int main() {
    int failed = check_replace_makes_and_replaces();
    if (failed == 0) failed = check_failed_write_keeps_the_old_file();
    if (failed == 0) failed = check_crash_before_the_rename_keeps_the_old_file();
    if (failed == 0) failed = check_name_is_one_final_name();
    if (failed == 0) failed = check_each_name_in_use_gives_eexist();
    if (failed == 0) failed = check_replace_of_a_path();
    if (failed != 0) {
        fixy::report(fixy::Sink::Err, "test_os_fs_replace: check {} failed\n", failed);
        return failed;
    }
    fixy::report(fixy::Sink::Out, "test_os_fs_replace: passed\n");
    return 0;
}
