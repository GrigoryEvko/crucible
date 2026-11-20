// The main translation unit of one boundary fuzz binary.
//
// CMake compiles this file once per harness with CRUCIBLE_FUZZ_HARNESS_NAME
// set, and links it with the object of that harness, built from
// harness_entry.cpp.  A new boundary is a new header under harnesses/ and one
// line in test/fuzz/CMakeLists.txt.
//
// This file holds the edges of the foreign ABI, each in an ABI region.  The
// entry of libFuzzer and the persistent loop of AFL++ take the input as a
// pointer to raw bytes and a count.  main takes the arguments of the C
// runtime.  The crash path runs in a signal handler and in the death callback
// of the sanitizer, where only the calls of the C library that are safe in a
// signal handler can run.  The runner of runner_main.h does the rest with
// types of the base.

#include "entry.h"
#include "runner_main.h"

#include <fixy/Core.h>
#include <fixy/os/Args.h>
#include <foundation/Quarantine.h>
#include <foundation/core/Region.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>

CRUCIBLE_FUZZ_DECLARE_HARNESS_OF(CRUCIBLE_FUZZ_HARNESS_NAME)

void crucible::fuzz::boundary::run_input(::fixy::View<std::byte const> input) noexcept {
    CRUCIBLE_FUZZ_ENTRY_RUN(CRUCIBLE_FUZZ_HARNESS_NAME)(input);
}

crucible::fuzz::boundary::SeedList crucible::fuzz::boundary::harness_seeds() noexcept {
    return CRUCIBLE_FUZZ_ENTRY_SEEDS(CRUCIBLE_FUZZ_HARNESS_NAME)();
}

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the fuzzer gives its input as a pointer to raw bytes and a count")

#ifdef __AFL_HAVE_MANUAL_CONTROL
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wpedantic"
__AFL_FUZZ_INIT();
#pragma GCC diagnostic pop
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

// The input lives for the call, so its View is valid in the harness.
int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    ::crucible::fuzz::boundary::run_input(
        ::foundation::core::byte_view(::foundation::core::mint_foreign_view(data, size)));
    return 0;
}

namespace {

#ifdef __AFL_HAVE_MANUAL_CONTROL
// AFL++ persistent mode: the input comes from shared memory, 10000 inputs for
// each fork.
int run_persistent_loop() {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wpedantic"
    __AFL_INIT();
    unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;
    while (__AFL_LOOP(10000)) {
        const auto len = static_cast<std::size_t>(__AFL_FUZZ_TESTCASE_LEN);
        (void)LLVMFuzzerTestOneInput(buf, len);
    }
#pragma GCC diagnostic pop
    return 0;
}
#endif

}  // namespace

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

// The death path calls only functions of the C library that are safe in a
// signal handler.
CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: the death path runs in a signal handler and in the death callback of the sanitizer")

// The calls of the C library and of the sanitizer that the death path makes.
#include <fcntl.h>
#include <sanitizer/common_interface_defs.h>
#include <unistd.h>

namespace {

// The file that receives the input in flight when the process dies.  The
// path is copied into a fixed buffer when the runner arms it, so the death
// path calls only functions that are safe in a signal handler.
std::array<char, 4096> crash_path{};

void save_current_input() noexcept {
    if (crash_path[0] == '\0') return;
    const int fd = ::open(crash_path.data(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return;
    const ::fixy::View<std::byte const> input = ::crucible::fuzz::boundary::current_input;
    const std::byte* const first = ::foundation::core::foreign_first(input);
    std::size_t done = 0;
    while (done < input.size()) {
        const ssize_t n = ::write(fd, first + done, input.size() - done);
        if (n <= 0) break;
        done += static_cast<std::size_t>(n);
    }
    (void)::close(fd);
}

void on_abort_signal(int signal_number) noexcept {
    save_current_input();
    (void)std::signal(signal_number, SIG_DFL);
    (void)std::raise(signal_number);
}

}  // namespace

// A claim and a contract end in abort, and a sanitizer report ends in its
// death callback.  Both save the input in flight.
void crucible::fuzz::boundary::arm_crash_file(::fixy::TextView path) noexcept {
    std::size_t count = 0;
    for (const char character : path) {
        if (count + 1 == crash_path.size()) break;
        crash_path[count] = character;
        ++count;
    }
    crash_path[count] = '\0';
    (void)std::signal(SIGABRT, &on_abort_signal);
    __sanitizer_set_death_callback(&save_current_input);
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

CRUCIBLE_I_KNOW_WHAT_IM_DOING("ABI: main takes the arguments of the C runtime as a count and an array of texts")

int main(int argc, char** argv) {
#ifdef __AFL_HAVE_MANUAL_CONTROL
    if (argc < 2) return run_persistent_loop();
#endif
    return ::crucible::fuzz::boundary::run_fuzz_main(::fixy::os::args(argc, argv));
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
