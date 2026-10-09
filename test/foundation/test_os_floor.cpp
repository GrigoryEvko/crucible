// Tests of the door directory of the base floor: the file door of
// foundation/os/File.h and the kernel floor of foundation/os/KernelFloor.h.
//
// The start-up check of the kernel floor runs before main in each program
// that links the raw system call primitive, so this test runs only on a
// kernel that the check admits.

#include <foundation/os/File.h>
#include <foundation/os/KernelFloor.h>

#include <foundation/Platform.h>

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// /proc/sys/kernel/ostype holds the name of the kernel and a newline.
void test_reads_a_whole_file() {
    core::FixedText<64> text;
    bool const has_read = os::read_file("/proc/sys/kernel/ostype", text).is_ok();
    CRUCIBLE_FATAL_INVARIANT(has_read);
    CRUCIBLE_FATAL_INVARIANT(text.view() == core::TextView{"Linux\n"});
}

// A buffer that is full before the end of the file ends the read.
void test_stops_at_a_full_buffer() {
    core::FixedText<2> text;
    bool const has_read = os::read_file("/proc/sys/kernel/ostype", text).is_ok();
    CRUCIBLE_FATAL_INVARIANT(has_read);
    CRUCIBLE_FATAL_INVARIANT(text.view() == core::TextView{"Li"});
}

// The open of a missing file gives the error of the kernel, and the text
// holds no character.
void test_gives_the_error_of_the_open() {
    core::FixedText<8> text;
    bool const is_missing =
        os::read_file("/proc/sys/kernel/the-floor-test-names-no-file", text)
            .match([](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::enoent; });
    CRUCIBLE_FATAL_INVARIANT(is_missing);
    CRUCIBLE_FATAL_INVARIANT(text.size() == 0);
}

// A directory opens, and its read gives EISDIR.  The door closes the
// descriptor and gives the error of the read.
void test_gives_the_error_of_the_read() {
    core::FixedText<8> text;
    bool const is_directory =
        os::read_file("/proc", text)
            .match([](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::eisdir; });
    CRUCIBLE_FATAL_INVARIANT(is_directory);
    CRUCIBLE_FATAL_INVARIANT(text.size() == 0);
}

// The doors open and close one descriptor for each read, so many reads
// leave no descriptor open: the process has a limit of descriptors far
// below the count of reads.
void test_closes_each_descriptor() {
    for (int read = 0; read < 70000; ++read) {
        core::FixedText<8> text;
        bool const has_read = os::read_file("/proc/sys/kernel/ostype", text).is_ok();
        CRUCIBLE_FATAL_INVARIANT(has_read);
    }
}

// The verdict of the start-up check for each kind of release text.
void test_judges_each_release_text() {
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("2.6.0") == os::KernelVerdict::too_old);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("999.0") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("3.7.0") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("7.1.4-202.fc44.x86_64") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.1.") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.x") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("x.1") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("70000.1") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.1.70000") == os::KernelVerdict::unreadable);
}

// The floor is one release of the two architectures, and no newer than the
// first release that has each call of the enumeration on aarch64.
void test_derives_the_floor() {
    os::KernelVersion const floor = os::minimum_kernel();
    CRUCIBLE_FATAL_INVARIANT(!std::is_lt(floor <=> os::KernelVersion{2, 6, 0}));
    CRUCIBLE_FATAL_INVARIANT(!std::is_gt(floor <=> os::KernelVersion{3, 7, 0}));
}

}  // namespace

int main() {
    test_reads_a_whole_file();
    test_stops_at_a_full_buffer();
    test_gives_the_error_of_the_open();
    test_gives_the_error_of_the_read();
    test_closes_each_descriptor();
    test_judges_each_release_text();
    test_derives_the_floor();
    return 0;
}
