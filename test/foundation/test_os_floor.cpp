// Tests of the door directory of the base floor: the file door of
// foundation/os/File.h, the directory door of foundation/os/Directory.h, the
// path of foundation/os/PathText.h and the kernel floor of
// foundation/os/KernelFloor.h.
//
// The start-up check of the kernel floor runs before main in each program
// that links the raw system call primitive, so this test runs only on a
// kernel that the check admits.

#include <foundation/os/Directory.h>
#include <foundation/os/File.h>
#include <foundation/os/KernelFallbacks.h>
#include <foundation/os/KernelFloor.h>
#include <foundation/os/PathText.h>

#include <foundation/Platform.h>
#include <foundation/core/Algorithms.h>

#include <foundation/reflect/EnumName.h>

#include <cstddef>
#include <cstdint>

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

// A path holds each piece in order, and its CText reads the same characters.
void test_builds_a_path() {
    os::PathText<32> path;
    CRUCIBLE_FATAL_INVARIANT(path.append(core::CText{"/proc/"}).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append_decimal(42).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append_decimal(0).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append('x').is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.size() == 10);
    CRUCIBLE_FATAL_INVARIANT(path.c_text().view() == core::TextView{"/proc/420x"});

    // The largest 64-bit value has 20 digits, the most that a value has.
    os::PathText<64> widest;
    CRUCIBLE_FATAL_INVARIANT(widest.append_decimal(core::max_of<std::uint64_t>).is_ok());
    CRUCIBLE_FATAL_INVARIANT(widest.c_text().view() == core::TextView{"18446744073709551615"});
    CRUCIBLE_FATAL_INVARIANT(widest.append_decimal(10000000000000000000ULL).is_ok());
    CRUCIBLE_FATAL_INVARIANT(widest.size() == 40);
}

// An append that does not fit gives enametoolong, and an append of a zero
// byte gives einval.  The path keeps the characters that it held before.
void test_refuses_an_append_that_does_not_fit() {
    os::PathText<4> path;
    CRUCIBLE_FATAL_INVARIANT(path.append(core::CText{"abc"}).is_ok());
    bool const is_too_long =
        path.append(core::CText{"de"})
            .match([](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::enametoolong; });
    CRUCIBLE_FATAL_INVARIANT(is_too_long);
    CRUCIBLE_FATAL_INVARIANT(path.append_decimal(12345).is_err());
    bool const is_zero = path.append('\0').match([](core::Unit) { return false; },
                                                 [](os::Errno error) { return error == os::Errno::einval; });
    CRUCIBLE_FATAL_INVARIANT(is_zero);
    CRUCIBLE_FATAL_INVARIANT(path.c_text().view() == core::TextView{"abc"});
    CRUCIBLE_FATAL_INVARIANT(path.append('d').is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append('e').is_err());
    CRUCIBLE_FATAL_INVARIANT(path.c_text().view() == core::TextView{"abcd"});
}

// The hex append writes a fixed count of lower-case digits, with the zeros
// that lead, so a hash splits into the two parts of an object path.  A count
// that does not fit gives enametoolong, and the path keeps its characters.
void test_appends_hex_digits() {
    std::uint64_t volatile hash = 0x0a1b2c3d4e5f6071ULL;
    os::PathText<25> path;
    CRUCIBLE_FATAL_INVARIANT(path.append(core::CText{"objects/"}).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append_hex<2>(hash >> 56U).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append('/').is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.append_hex<14>(hash).is_ok());
    CRUCIBLE_FATAL_INVARIANT(path.c_text().view() == core::TextView{"objects/0a/1b2c3d4e5f6071"});
    bool const is_too_long = path.append_hex<16>(hash).match(
        [](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::enametoolong; });
    CRUCIBLE_FATAL_INVARIANT(is_too_long);
    CRUCIBLE_FATAL_INVARIANT(path.c_text().view() == core::TextView{"objects/0a/1b2c3d4e5f6071"});
}

// The listing of /proc/self gives the name of each entry, such as status,
// and gives neither "." nor "..".
void test_lists_a_directory() {
    int entries = 0;
    bool has_status = false;
    bool has_dot_entry = false;
    bool const is_listed =
        os::for_each_entry("/proc/self", [&](core::CText name) noexcept {
            ++entries;
            has_status = has_status || name.view() == core::TextView{"status"};
            has_dot_entry = has_dot_entry || name.view() == core::TextView{"."} || name.view() == core::TextView{".."};
            return os::Visit::proceed;
        }).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_listed);
    CRUCIBLE_FATAL_INVARIANT(entries > 10);
    CRUCIBLE_FATAL_INVARIANT(has_status);
    CRUCIBLE_FATAL_INVARIANT(!has_dot_entry);
}

// A visitor that answers stop gets no more names.
void test_stops_a_listing() {
    int entries = 0;
    bool const is_listed = os::for_each_entry("/proc/self", [&entries](core::CText) noexcept {
                               ++entries;
                               return os::Visit::stop;
                           }).is_ok();
    CRUCIBLE_FATAL_INVARIANT(is_listed);
    CRUCIBLE_FATAL_INVARIANT(entries == 1);
}

// The open of a missing directory gives ENOENT, and the open of a file
// gives ENOTDIR.
void test_gives_the_error_of_a_listing() {
    auto visit_none = [](core::CText) noexcept { return os::Visit::proceed; };
    bool const is_missing =
        os::for_each_entry("/proc/the-floor-test-names-no-directory", visit_none)
            .match([](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::enoent; });
    CRUCIBLE_FATAL_INVARIANT(is_missing);
    bool const is_file =
        os::for_each_entry("/proc/self/status", visit_none)
            .match([](core::Unit) { return false; }, [](os::Errno error) { return error == os::Errno::enotdir; });
    CRUCIBLE_FATAL_INVARIANT(is_file);
}

// Each listing closes its descriptor, so many listings leave no descriptor
// open.
void test_closes_each_listing_descriptor() {
    for (int listing = 0; listing < 70000; ++listing) {
        bool const is_listed =
            os::for_each_entry("/proc/sys", [](core::CText) noexcept { return os::Visit::stop; }).is_ok();
        CRUCIBLE_FATAL_INVARIANT(is_listed);
    }
}

// The verdict of the start-up check for each kind of release text.
void test_judges_each_release_text() {
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("2.6.0") == os::KernelVerdict::too_old);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("999.0") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("5.5.19") == os::KernelVerdict::too_old);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("5.6.0") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("7.1.4-202.fc44.x86_64") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.1.") == os::KernelVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.x") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("x.1") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("70000.1") == os::KernelVerdict::unreadable);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("6.1.70000") == os::KernelVerdict::unreadable);
}

// The floor is 5.6 on the two architectures: the release of openat2, which
// each open beneath a directory issues, and the newest call that is no
// feature call.  The rows of the flags that a door needs in each call count
// too, so a new such row above 5.6 fails this test.
void test_derives_the_floor() {
    os::KernelVersion const floor = os::minimum_kernel();
    CRUCIBLE_FATAL_INVARIANT(std::is_eq(floor <=> os::KernelVersion{5, 6, 0}));
}

// The count of the rows of the flags that break a rule of the flags of
// foundation/os/KernelFloor.h: a row with no word, a fallback row with no
// fallback text, a row of the need each_call with a fallback text, a row of
// a word that came before 2.6.0, or a row that a release far in the future
// does not admit.
consteval std::size_t count_rows_that_break_a_rule() {
    std::size_t broken = 0;
    ::foundation::reflect::for_each_enumerator_value<os::KernelFlag>([&broken](os::KernelFlag flag) {
        os::FlagFloor const row = os::flag_floor(flag);
        bool const has_fallback_text = row.fallback[0] != '\0';
        bool const is_fallback_row = row.need == os::FlagNeed::fallback;
        bool const is_broken = row.word[0] == '\0' || has_fallback_text != is_fallback_row
                            || !(os::KernelVersion{2, 6, 0} < row.release)
                            || os::judge_flag(flag, os::KernelVersion{999, 0, 0}) != os::FlagVerdict::admitted;
        broken += is_broken ? 1 : 0;
    });
    return broken;
}

// Each row obeys the rules of the flags.
void test_each_flag_row_obeys_the_rules() {
    static_assert(count_rows_that_break_a_rule() == 0, "a row of flag_floor breaks a rule of THE FLAGS");
    CRUCIBLE_FATAL_INVARIANT(count_rows_that_break_a_rule() == 0);
}

// The verdict of each row above the floor on a release that lacks its word
// and on the release that added it.  A release that lacks the word gives the
// fallback of the row, so the process runs there.
void test_judges_each_flag_above_the_floor() {
    using os::FlagVerdict;
    using os::KernelFlag;
    using os::KernelVersion;
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::advise_populate_write, KernelVersion{5, 13, 19})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::advise_populate_write, KernelVersion{5, 14, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_coop_taskrun, KernelVersion{5, 18, 19})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_coop_taskrun, KernelVersion{5, 19, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_single_issuer, KernelVersion{5, 19, 17})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_single_issuer, KernelVersion{6, 0, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::advise_collapse, KernelVersion{6, 0, 19})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::advise_collapse, KernelVersion{6, 1, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_defer_taskrun, KernelVersion{6, 0, 19})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::ring_defer_taskrun, KernelVersion{6, 1, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::tcp_rto_min_us, KernelVersion{6, 14, 11})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::tcp_rto_min_us, KernelVersion{6, 15, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::thp_refusal_flags, KernelVersion{6, 17, 13})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::thp_refusal_flags, KernelVersion{6, 18, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::memory_file_no_exec, KernelVersion{6, 2, 16})
                             == FlagVerdict::fallback);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::memory_file_no_exec, KernelVersion{6, 3, 0})
                             == FlagVerdict::admitted);
}

// A release that lacks a word that a door needs in each call refuses the
// row.  The floor counts such a row, so the start-up check refuses the
// release too.
void test_refuses_a_flag_of_each_call() {
    using os::FlagVerdict;
    using os::KernelFlag;
    using os::KernelVersion;
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::resolve_beneath, KernelVersion{5, 5, 19})
                             == FlagVerdict::refused);
    CRUCIBLE_FATAL_INVARIANT(os::judge_release("5.5.19") == os::KernelVerdict::too_old);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::resolve_beneath, KernelVersion{5, 6, 0})
                             == FlagVerdict::admitted);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::open_path, KernelVersion{2, 6, 38}) == FlagVerdict::refused);
    CRUCIBLE_FATAL_INVARIANT(os::judge_flag(KernelFlag::open_path, KernelVersion{2, 6, 39}) == FlagVerdict::admitted);
}

// The count of the rows of the need fallback that the release running lacks,
// by judge_flag: the oracle of the text of the report.
[[nodiscard]] std::size_t count_fallback_rows(os::KernelVersion running) noexcept {
    std::size_t count = 0;
    ::foundation::reflect::for_each_enumerator_value<os::KernelFlag>([&](os::KernelFlag flag) noexcept {
        if (os::judge_flag(flag, running) == os::FlagVerdict::fallback) ++count;
    });
    return count;
}

// The count of the lines of a text.  Complexity: linear in its size.
[[nodiscard]] std::size_t count_lines(core::TextView text) noexcept {
    std::size_t count = 0;
    for (char const character : text) {
        if (character == '\n') ++count;
    }
    return count;
}

// The text of the report of the init door holds one line for each row of the need
// fallback that a release lacks, and nothing for a release that has each
// word.  The release of the floor lacks each row above it.  The release just
// before TCP_RTO_MIN_US lacks that word and the refusal flags of THP.
void test_reports_each_fallback_row_of_a_release() {
    core::FixedText<os::kernel_fallback_text_capacity> text{};
    os::KernelVersion const floor{5, 6, 0};
    core::TextView const at_floor =
        os::format_kernel_fallbacks(floor, text).expect("the rows of the floor fit the text of the report");
    CRUCIBLE_FATAL_INVARIANT(count_lines(at_floor) == count_fallback_rows(floor));
    CRUCIBLE_FATAL_INVARIANT(count_lines(at_floor) >= 7);
    CRUCIBLE_FATAL_INVARIANT(core::starts_with(at_floor, "foundation: the kernel release 5.6.0 lacks "));
    CRUCIBLE_FATAL_INVARIANT(core::find(at_floor, "MADV_POPULATE_WRITE, which 5.14.0 added. ").is_some());
    CRUCIBLE_FATAL_INVARIANT(core::find(at_floor, "MADV_COLLAPSE, which 6.1.0 added. ").is_some());

    os::KernelVersion const before_rto{6, 14, 11};
    core::TextView const at_rto = os::format_kernel_fallbacks(before_rto, text).expect("two rows fit the text");
    CRUCIBLE_FATAL_INVARIANT(count_lines(at_rto) == 2 && count_fallback_rows(before_rto) == 2);
    CRUCIBLE_FATAL_INVARIANT(core::find(at_rto, "TCP_RTO_MIN_US, which 6.15.0 added. ").is_some());
    CRUCIBLE_FATAL_INVARIANT(core::find(at_rto, "The option door gives enoprotoopt.").is_some());

    core::TextView const at_future = os::format_kernel_fallbacks(os::KernelVersion{999, 0, 0}, text).expect("empty");
    CRUCIBLE_FATAL_INVARIANT(at_future.size() == 0);
}

// The start-up check keeps the release of the running kernel, and the floor
// admits that release, so the report of the init door judges the release of
// this host.  The text of that release holds one line for each row it lacks.
void test_keeps_the_running_release() {
    os::KernelVersion const running = os::running_kernel();
    CRUCIBLE_FATAL_INVARIANT(!(running < os::minimum_kernel()));
    core::FixedText<os::kernel_fallback_text_capacity> text{};
    core::TextView const report = os::format_kernel_fallbacks(running, text).expect("the rows of this host fit");
    CRUCIBLE_FATAL_INVARIANT(count_lines(report) == count_fallback_rows(running));
}

}  // namespace

int main() {
    test_reads_a_whole_file();
    test_stops_at_a_full_buffer();
    test_gives_the_error_of_the_open();
    test_gives_the_error_of_the_read();
    test_closes_each_descriptor();
    test_builds_a_path();
    test_refuses_an_append_that_does_not_fit();
    test_appends_hex_digits();
    test_lists_a_directory();
    test_stops_a_listing();
    test_gives_the_error_of_a_listing();
    test_closes_each_listing_descriptor();
    test_judges_each_release_text();
    test_derives_the_floor();
    test_each_flag_row_obeys_the_rules();
    test_judges_each_flag_above_the_floor();
    test_refuses_a_flag_of_each_call();
    test_reports_each_fallback_row_of_a_release();
    test_keeps_the_running_release();
    return 0;
}
