#pragma once

// The runner of every boundary fuzz binary.
//
// fuzz_main.cpp holds the edges of the foreign ABI: the entry of libFuzzer,
// the persistent loop of AFL++, main, and the crash path in a signal handler
// and in the death callback of the sanitizer.  It defines the functions that
// this header declares, and main gives the arguments to run_fuzz_main.  The
// seeds come from the harness, which builds them from the real writers, so a
// format change moves the seeds with it.  The binary runs in one of these
// modes:
//
//   <binary> FILE                 feeds FILE once
//   <binary> DIR [--mutate=N]     feeds every file in DIR and every seed
//   <binary> --mutate=N           feeds every seed, then N mutated copies,
//                                 from a fixed stream (the ctest smoke run)
//   <binary> ... --crash-file=P   writes the input in flight to P when a
//                                 claim, a contract or a sanitizer stops
//                                 the process
//   <binary> --write-seeds=DIR    writes the seeds to DIR, for afl-fuzz -i
//   <binary>                      AFL++ persistent mode, when the binary was
//                                 built with afl-g++-fast: the input comes
//                                 from shared memory, 10000 inputs per fork
//
// The mutation mode has no coverage feedback.  It only makes the smoke run
// reach past the seeds.  Coverage-guided runs use afl-fuzz, see
// test/fuzz/README.md and `loop fuzz` (utils/assay/loop/fuzz.py).
//
// The runner holds only types of the base.  A path of the command line goes
// through the sanitizer of fixy/os/Path.h, so a path with a ".." part is
// refused.  The directory of --write-seeds is one new directory under a
// directory that exists.  A corpus directory gives each entry that opens and
// reads as a file.  The open takes no symbolic link.

#include "edit_buffer.h"
#include "entry.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Args.h>
#include <fixy/os/Fs.h>
#include <fixy/os/Path.h>
#include <foundation/core/Frame.h>
#include <foundation/core/Sort.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Directory.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::fuzz::boundary {

// Feeds one input to the harness of the binary (fuzz_main.cpp).
void run_input(::fixy::View<std::byte const> input) noexcept;

// The seeds of the harness of the binary (fuzz_main.cpp).
[[nodiscard]] SeedList harness_seeds() noexcept;

// Arms the crash file at path (fuzz_main.cpp).  From then on the death path of
// a claim, a contract and a sanitizer writes the input in flight to path.
void arm_crash_file(::fixy::TextView path) noexcept;

// The input in flight, which the death path writes to the crash file.  The
// runner sets it before each input, and its storage lives until the next one.
inline ::fixy::View<std::byte const> current_input{};

namespace runner_detail {

namespace fs = ::fixy::fs;
using ::foundation::os::Errno;

inline constexpr ::fixy::TestRunnerCtx kCtx{::foundation::effects::testing::test()};

// A count of inputs, such as the mutated copies of a smoke run.
using InputCount = ::fixy::Quantity<::fixy::unit::Element, std::uint64_t>;

// The bytes that one mutation can insert: eight edits of at most 64 bytes.
inline constexpr std::size_t kMutationRoom = 8 * 64;

// The edge values of a mutation that sets a byte.
inline constexpr ::fixy::FixedArray<std::byte, 8> kMutationEdges{
    std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80},
    std::byte{0xFF}, std::byte{0xFE}, std::byte{0x10}, std::byte{0x40},
};

// splitmix64: a fixed, portable stream, so the smoke run is the same run on
// every host.
class MutationStream {
public:
    [[nodiscard]] constexpr std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t state_ = 0x6372756369626c65ULL;
};

// Applies one to eight byte-level edits: flip a bit, set a byte to an edge
// value, insert a byte, delete a byte, or splice in a run of the donor.
inline void mutate(EditBuffer& bytes, ::fixy::View<std::byte const> donor, MutationStream& stream) noexcept {
    std::uint64_t const edits = 1 + stream.next() % 8;
    for (std::uint64_t edit = 0; edit < edits; ++edit) {
        std::uint64_t const random = stream.next();
        ByteCount const at{bytes.is_empty() ? 0 : (random >> 8) % bytes.size()};
        switch (random % 5) {
            case 0:
                if (!bytes.is_empty()) bytes.xor_at(at, static_cast<std::byte>(1U << ((random >> 4) % 8)));
                break;
            case 1:
                if (!bytes.is_empty()) {
                    bytes.set_at(at, *kMutationEdges.view().get((random >> 4) % 8).expect("an edge of the table"));
                }
                break;
            case 2: {
                ::fixy::FixedArray<std::byte, 1> const inserted{static_cast<std::byte>(random >> 16)};
                bytes.insert_at(at, inserted.view());
                break;
            }
            case 3:
                if (!bytes.is_empty()) bytes.erase_at(at);
                break;
            default:
                if (donor.size() != 0) {
                    std::size_t const from = (random >> 20) % donor.size();
                    std::size_t const longest = 1 + (random >> 40) % 64;
                    std::size_t const length = ::fixy::min(longest, donor.size() - from);
                    bytes.insert_at(at, donor.window(from, length).expect("the run is in the donor"));
                }
                break;
        }
    }
}

// True when left comes before right in the order of their bytes: the first
// byte that differs decides, and a prefix comes first.  This is the order of
// std::ranges::sort over vectors of bytes.  Complexity: linear in the shorter.
template <class Left, class Right>
[[nodiscard]] bool precedes(Left const& left, Right const& right) noexcept {
    ::fixy::View<std::byte const> const first = left;
    ::fixy::View<std::byte const> const second = right;
    std::size_t const shorter = ::fixy::min(first.size(), second.size());
    for (std::size_t position = 0; position < shorter; ++position) {
        std::byte const from_first = *first.get(position).expect("a position below the shorter count");
        std::byte const from_second = *second.get(position).expect("a position below the shorter count");
        if (from_first != from_second) return from_first < from_second;
    }
    return first.size() < second.size();
}

// The bytes of the file, from its start to its end, or no value when a read
// fails.
[[nodiscard]] inline ::fixy::Option<SeedBytes> read_seed(fs::FileOf<fs::read_only> const& file) noexcept {
    std::int64_t const size = fs::file_size(kCtx, file.ref()).value_or(-1);
    if (size < 0) return ::fixy::none;
    SeedBytes buffer = ::fixy::mint_heap_array_zeroed<std::byte>(test_alloc(), static_cast<std::size_t>(size));
    std::size_t const capacity = buffer.size();
    auto [count, filled] = ::fixy::with_mut(std::move(buffer), [&file, capacity](auto run) noexcept {
        return fs::read_full(kCtx, file.ref(), std::move(run)).value_or(capacity + 1);
    });
    if (count > filled.size()) return ::fixy::none;
    if (count == filled.size()) return ::fixy::Option<SeedBytes>::some(std::move(filled));
    return ::fixy::Option<SeedBytes>::some(
        copy_seed(::fixy::View<std::byte const>{filled.view()}.window(0, count).expect("a read fits its buffer")));
}

// The sanitized path of an argument, or no value after a report.
[[nodiscard]] inline ::fixy::Option<::fixy::Path<::fixy::tags::source::Sanitized>>
sanitized_argument(::fixy::TextView argument) noexcept {
    return ::fixy::sanitize_path(::fixy::mint_tagged<::fixy::tags::source::External>(argument))
        .match(
            [](::fixy::Path<::fixy::tags::source::Sanitized> path) noexcept {
                return ::fixy::Option<::fixy::Path<::fixy::tags::source::Sanitized>>::some(std::move(path));
            },
            [argument](
                ::fixy::PathTraversalError) noexcept -> ::fixy::Option<::fixy::Path<::fixy::tags::source::Sanitized>> {
                ::fixy::report(::fixy::Sink::Err,
                               "fuzz: '{}' is empty, too long, or holds a zero byte or a '..' part\n", argument);
                return ::fixy::none;
            });
}

inline void feed(::fixy::View<std::byte const> input) noexcept {
    current_input = input;
    run_input(input);
}

// Writes each seed to the directory dir, as the file seed-N.  The parent of
// dir exists, and dir is made when it is missing.
[[nodiscard]] inline int write_seeds(::fixy::TextView dir) noexcept {
    // The last part of dir is the new directory, and the part before it is the
    // parent.  Slashes at the end name no part.  end is one past the last
    // character that is no slash, and slash is one past the last slash before
    // end.
    std::size_t end = 0;
    std::size_t counted = 0;
    for (char const character : dir) {
        ++counted;
        if (character != '/') end = counted;
    }
    std::size_t slash = 0;
    counted = 0;
    for (char const character : dir) {
        if (counted == end) break;
        ++counted;
        if (character == '/') slash = counted;
    }
    ::fixy::TextView const name = dir.window(slash, end - slash).expect("the last part is in the path");
    ::fixy::TextView const parent_text = slash == 0 ? ::fixy::TextView{"."}
                                       : slash == 1 ? ::fixy::TextView{"/"}
                                                    : dir.window(0, slash - 1).expect("the parent is in the path");
    ::fixy::Option<::fixy::Path<::fixy::tags::source::Sanitized>> const parent_path = sanitized_argument(parent_text);
    if (parent_path.is_none() || name.size() == 0) return 1;
    ::foundation::os::PathText<::fixy::kPathBytes> name_text{};
    if (name_text.append(name).is_err()) return 1;
    ::fixy::Result<fs::DirectoryHandle, Errno> made = parent_path.match(
        [&name_text](::fixy::Path<::fixy::tags::source::Sanitized> const& path) noexcept {
            return fs::open_directory(kCtx, path).and_then([&name_text](fs::DirectoryHandle parent) noexcept {
                fs::make_directory_at(kCtx, parent.ref(), name_text.c_text())
                    .match([](::fixy::Unit) noexcept {}, [](Errno) noexcept {});
                return fs::open_directory_at(kCtx, parent.ref(), name_text.c_text());
            });
        },
        [] noexcept -> ::fixy::Result<fs::DirectoryHandle, Errno> { return ::fixy::err(Errno::enoent); });
    if (made.is_err()) {
        ::fixy::report(::fixy::Sink::Err, "fuzz: the directory '{}' does not open\n", dir);
        return 1;
    }
    fs::DirectoryHandle const target = std::move(made).expect("the directory opened");
    SeedList const seeds = harness_seeds();
    bool is_written = true;
    seeds.with_view([&target, &is_written](::fixy::View<SeedBytes const> all) noexcept {
        using WriteTruncate = ::fixy::atom::fs::mode<fs::open_mode::WriteTruncate>;
        std::size_t index = 0;
        for (SeedBytes const& seed : all) {
            ::foundation::os::PathText<::fixy::kPathBytes> file_name{};
            file_name.append(::fixy::TextView{"seed-"}).expect("the name of a seed fits a path");
            file_name.append_decimal(index).expect("the name of a seed fits a path");
            ++index;
            is_written = is_written
                      && fs::mint_file_at<WriteTruncate>(kCtx, target.ref(), file_name.c_text())
                             .match(
                                 [&seed](::fixy::Linear<fs::FileOf<WriteTruncate>> opened) noexcept {
                                     fs::FileOf<WriteTruncate> const file = std::move(opened).consume();
                                     return fs::write_full(kCtx, file.ref(), ::fixy::View<std::byte const>{seed.view()})
                                         .is_ok();
                                 },
                                 [](Errno) noexcept { return false; });
        }
    });
    if (!is_written) {
        ::fixy::report(::fixy::Sink::Err, "fuzz: a seed did not write to '{}'\n", dir);
        return 1;
    }
    ::fixy::report(::fixy::Sink::Err, "fuzz: wrote {} seeds to {}\n", seeds.size(), dir);
    return 0;
}

// Feeds the seeds in the order of their bytes, then mutations copies.  The
// walk order of a directory depends on the file system, so the order of the
// bytes keeps the mutation stream the same on every host.
[[nodiscard]] inline int run_seeds(SeedList const& seeds, InputCount mutations) noexcept {
    seeds.with_view([mutations](::fixy::View<SeedBytes const> all) noexcept {
        auto const seed_at = [all](std::size_t position) noexcept {
            return ::fixy::View<std::byte const>{all.get(position).expect("a position of the seeds")->view()};
        };
        ::fixy::HeapArray<std::size_t> order = ::fixy::mint_heap_array_with<std::size_t>(
            test_alloc(), all.size(), [](std::size_t position) noexcept { return position; });
        ::fixy::HeapArray<std::size_t> scratch = ::fixy::mint_heap_array_zeroed<std::size_t>(test_alloc(), all.size());
        auto const is_before = [&seed_at](auto const left, auto const right) noexcept {
            return precedes(seed_at(left), seed_at(right));
        };
        order = ::foundation::core::with_mut(std::move(order), [&](auto elements) noexcept {
            scratch = ::foundation::core::with_mut(std::move(scratch), [&](auto room) noexcept {
                ::foundation::core::sort_stably(std::move(elements), std::move(room), is_before)
                    .expect("the order and its scratch hold one count");
            });
        });
        ::fixy::View<std::size_t const> const sorted = order.view();
        auto const sorted_seed = [&seed_at, sorted](std::uint64_t random) noexcept {
            std::size_t const position = random % sorted.size();
            return seed_at(*sorted.get(position).expect("a position of the order"));
        };
        for (std::size_t const position : sorted) {
            feed(seed_at(position));
        }
        MutationStream stream{};
        for (std::uint64_t index = 0; index < mutations.raw(); ++index) {
            ::fixy::View<std::byte const> const base = sorted_seed(stream.next());
            ::fixy::View<std::byte const> const donor = sorted_seed(stream.next());
            EditBuffer bytes{base, ByteCount{kMutationRoom}};
            mutate(bytes, donor, stream);
            feed(bytes.view());
        }
    });
    ::fixy::report(::fixy::Sink::Err, "fuzz: replayed {} seeds and {} mutations\n", seeds.size(), mutations.raw());
    return 0;
}

using SanitizedPath = ::fixy::Path<::fixy::tags::source::Sanitized>;

// The bytes of the file at path, or no value when it does not open or read.
[[nodiscard]] inline ::fixy::Option<SeedBytes> read_path(SanitizedPath const& path) noexcept {
    ::fixy::Option<SeedBytes> read{};
    fs::mint_file<fs::read_only>(kCtx, path)
        .match(
            [&read](::fixy::Linear<fs::FileOf<fs::read_only>> opened) noexcept {
                fs::FileOf<fs::read_only> const file = std::move(opened).consume();
                read = read_seed(file);
            },
            [](Errno) noexcept {});
    return read;
}

// Adds the bytes of each entry of the directory dir at path to seeds, in the
// order of the kernel.  An entry that does not open or read as a file, such as
// a directory or a symbolic link, gives no seed.
inline void add_directory_seeds(SanitizedPath const& path, fs::DirectoryHandle const& dir, SeedList& seeds) noexcept {
    static_cast<void>(
        ::foundation::os::for_each_entry(path.value().c_text(), [&dir, &seeds](::fixy::CText name) noexcept {
            fs::mint_file_at<fs::read_only>(kCtx, dir.ref(), name)
                .match(
                    [&seeds](::fixy::Linear<fs::FileOf<fs::read_only>> opened) noexcept {
                        fs::FileOf<fs::read_only> const file = std::move(opened).consume();
                        read_seed(file).match([&seeds](SeedBytes seed) noexcept { add_seed(seeds, std::move(seed)); },
                                              [] noexcept {});
                    },
                    [](Errno) noexcept {});
            return ::foundation::os::Visit::proceed;
        }));
}

// target is a file, a directory, or no value for the generated seeds only.
[[nodiscard]] inline int run_corpus_or_file(::fixy::Option<::fixy::TextView> target, InputCount mutations) noexcept {
    SeedList seeds = harness_seeds();
    if (target.is_some()) {
        ::fixy::TextView const argument = std::move(target).value_or(::fixy::TextView{});
        ::fixy::Option<SanitizedPath> checked = sanitized_argument(argument);
        if (checked.is_none()) return 1;
        SanitizedPath const path = std::move(checked).expect("the argument passed the sanitizer");
        bool is_directory = false;
        bool is_other_entry = false;
        fs::open_directory(kCtx, path)
            .match(
                [&](fs::DirectoryHandle dir) noexcept {
                    is_directory = true;
                    add_directory_seeds(path, dir, seeds);
                },
                [&is_other_entry](Errno error) noexcept { is_other_entry = error != Errno::enotdir; });
        if (!is_directory) {
            ::fixy::Option<SeedBytes> read = is_other_entry ? ::fixy::Option<SeedBytes>{} : read_path(path);
            if (read.is_none()) {
                ::fixy::report(::fixy::Sink::Err, "fuzz: '{}' is neither a regular file nor a directory\n", argument);
                return 1;
            }
            SeedBytes const bytes = std::move(read).expect("the file was read");
            ::fixy::report(::fixy::Sink::Err, "fuzz: {} ({} bytes)\n", argument, bytes.size());
            feed(::fixy::View<std::byte const>{bytes.view()});
            return 0;
        }
    }
    if (seeds.size() == 0) add_seed(seeds, SeedBytes{});
    return run_seeds(seeds, mutations);
}

// The count that the value of a flag holds, or no value after a report.
[[nodiscard]] inline ::fixy::Option<std::uint64_t> flag_count(::fixy::TextView value) noexcept {
    return ::fixy::parse<std::uint64_t>(value).match(
        [](std::uint64_t count) noexcept { return ::fixy::Option<std::uint64_t>::some(count); },
        [value](::fixy::ParseError) noexcept -> ::fixy::Option<std::uint64_t> {
            ::fixy::report(::fixy::Sink::Err, "fuzz: '{}' is no decimal or 0x hexadecimal count\n", value);
            return ::fixy::none;
        });
}

}  // namespace runner_detail

// The body of main: the modes of the command line (the comment at the top of
// this header).
[[nodiscard]] inline int run_fuzz_main(::fixy::os::Args const arguments) noexcept {
    using namespace runner_detail;
    constexpr ::fixy::TextView kMutate{"--mutate="};
    constexpr ::fixy::TextView kCrashFile{"--crash-file="};
    constexpr ::fixy::TextView kWriteSeeds{"--write-seeds="};
    if (arguments.size() < 2) {
        ::fixy::report(::fixy::Sink::Err,
                       "Usage: {} [FILE | DIR] [--mutate=N] [--crash-file=PATH] | --write-seeds=DIR\n"
                       "  FILE              feeds the bytes of FILE once.\n"
                       "  DIR               feeds every file in DIR and every generated seed.\n"
                       "  --mutate=N        also feeds N mutated copies of the seeds.\n"
                       "  --crash-file=PATH writes the input in flight to PATH if the process dies.\n"
                       "  --write-seeds=DIR writes the generated seeds to DIR.\n",
                       arguments.get(0).expect("main has its program name").view());
        return 1;
    }
    ::fixy::Option<::fixy::TextView> target{};
    InputCount mutations{0};
    for (std::size_t position = 1; position < arguments.size(); ++position) {
        ::fixy::TextView const argument = arguments.get(position).expect("a position below the count").view();
        // The characters of the argument after the prefix of its flag.
        auto const after = [argument](::fixy::TextView const prefix) noexcept {
            return argument.window(prefix.size(), argument.size() - prefix.size())
                .expect("the argument starts with its prefix");
        };
        if (::fixy::starts_with(argument, kMutate)) {
            ::fixy::Option<std::uint64_t> count = flag_count(after(kMutate));
            if (count.is_none()) return 1;
            mutations = InputCount{std::move(count).value_or(0)};
        } else if (::fixy::starts_with(argument, kCrashFile)) {
            arm_crash_file(after(kCrashFile));
        } else if (::fixy::starts_with(argument, kWriteSeeds)) {
            return write_seeds(after(kWriteSeeds));
        } else {
            target = ::fixy::Option<::fixy::TextView>::some(argument);
        }
    }
    return run_corpus_or_file(target, mutations);
}

}  // namespace crucible::fuzz::boundary
