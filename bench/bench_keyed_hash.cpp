// The keyed hash of the base (foundation/core/KeyedHash.h) against the pinned
// official BLAKE3 (utils/pins.txt), at the sizes of a key of a federation:
//
//   16 and 64 bytes      a short content
//   156 bytes            the image of a tensor descriptor
//   1024 and 1828 bytes  one chunk, and the size of a region record
//   4 KiB to 128 KiB     the lanes of many chunks
//
// Each row hashes the bytes i % 251 at i under the key of the official test
// vectors.  The official C dispatches to its arm of the host, AVX-512 on a
// host of x86-64-v4, so the ratio of two rows of one size is the ratio of the
// base to the state of the art on the host.  A tools directory with no BLAKE3
// at its pin gives no reference row.

#include <foundation/core/KeyedHash.h>

#include <fixy/Core.h>

#include "bench_harness.h"
#include "bench_keyed_hash_references.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {

namespace fc = ::foundation::core;

inline constexpr std::size_t kLongest = 131072;
inline constexpr std::size_t kRows = 8;

// The bytes i % 251 at i, as the official test vectors hold them.
struct Input final {
    fc::FixedArray<std::byte, kLongest> bytes{};

    Input() noexcept {
        std::size_t at = 0;
        for (std::byte& byte : bytes) {
            byte = static_cast<std::byte>(at % 251);
            ++at;
        }
    }
};

// The one input of the rows, built at its first use.
[[nodiscard]] Input const& input_bytes() noexcept {
    static Input const input{};
    return input;
}

// The key text of the official test vectors.
[[nodiscard]] fc::FixedArray<std::byte, 32> key_bytes() noexcept {
    constexpr fc::TextView text{"whats the Elvish word for friend"};
    fc::FixedArray<std::byte, 32> bytes{};
    auto letter = text.begin();
    for (std::byte& byte : bytes) {
        byte = static_cast<std::byte>(*letter);
        ++letter;
    }
    return bytes;
}

[[nodiscard]] fc::View<std::byte const> prefix(Input const& input, std::size_t size) noexcept {
    return fc::View<std::byte const>{input.bytes.view()}.window(0, size).expect("each size is inside the input");
}

#if defined(CRUCIBLE_BENCH_BLAKE3)
CRUCIBLE_I_KNOW_WHAT_IM_DOING("MEASURE: the bytes of a row go to the door of the pinned official BLAKE3")

// The output of the official keyed hash of the first size bytes of the input.
[[nodiscard]] fc::FixedArray<std::byte, 32> official_keyed_hash(fc::FixedArray<std::byte, 32> const& key,
                                                                Input const& input, std::size_t size) noexcept {
    fc::FixedArray<std::byte, 32> out{};
    bench_keyed_hash_references::blake3_keyed(&key.at(fc::index<0>()), &input.bytes.at(fc::index<0>()), size,
                                              &out.at(fc::index<0>()));
    return out;
}

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
#endif

}  // namespace

int main() {
    bench::print_system_info();
    bench::elevate_priority();
    const bench::JsonTail json = bench::env_json();

    Input const& input = input_bytes();
    fc::FixedArray<std::byte, 32> const key = key_bytes();
    fc::KeyedHashKeyBytes const base_key{key.view()};

    ::fixy::report(::fixy::Sink::Out, "=== keyed hash ===\n\n");

    // The body of the row of the base at one size.
    auto const base_row = [&input, &base_key](std::size_t size) noexcept {
        fc::View<std::byte const> const message = prefix(input, size);
        return [&base_key, message] {
            bench::do_not_optimize(message);
            auto const digest = fc::keyed_hash(base_key, message);
            bench::do_not_optimize(digest);
        };
    };

    fc::FixedArray<bench::Report, kRows> base_reports{
        bench::run("keyed_hash of the base       16 B", base_row(16)),
        bench::run("keyed_hash of the base       64 B", base_row(64)),
        bench::run("keyed_hash of the base      156 B", base_row(156)),
        bench::run("keyed_hash of the base     1024 B", base_row(1024)),
        bench::run("keyed_hash of the base     1828 B", base_row(1828)),
        bench::run("keyed_hash of the base     4096 B", base_row(4096)),
        bench::run("keyed_hash of the base    16384 B", base_row(16384)),
        bench::run("keyed_hash of the base   131072 B", base_row(131072)),
    };
    bench::emit_reports_text(base_reports.view());

#if defined(CRUCIBLE_BENCH_BLAKE3)
    // The body of the row of the official C at one size.
    auto const official_row = [&input, &key](std::size_t size) noexcept {
        return [&input, &key, size] {
            bench::do_not_optimize(size);
            auto const digest = official_keyed_hash(key, input, size);
            bench::do_not_optimize(digest);
        };
    };

    fc::FixedArray<bench::Report, kRows> official_reports{
        bench::run("official BLAKE3 C            16 B", official_row(16)),
        bench::run("official BLAKE3 C            64 B", official_row(64)),
        bench::run("official BLAKE3 C           156 B", official_row(156)),
        bench::run("official BLAKE3 C          1024 B", official_row(1024)),
        bench::run("official BLAKE3 C          1828 B", official_row(1828)),
        bench::run("official BLAKE3 C          4096 B", official_row(4096)),
        bench::run("official BLAKE3 C         16384 B", official_row(16384)),
        bench::run("official BLAKE3 C        131072 B", official_row(131072)),
    };
    bench::emit_reports_text(official_reports.view());

    // The two arms give one output at each size of the rows.
    constexpr fc::FixedArray<std::size_t, kRows> sizes{16, 64, 156, 1024, 1828, 4096, 16384, 131072};
    for (std::size_t const size : sizes) {
        fc::FixedArray<std::byte, 32> const official = official_keyed_hash(key, input, size);
        if (!::fixy::equal(fc::keyed_hash(base_key, prefix(input, size)).bytes(), official)) {
            ::fixy::fatal("the keyed hash of the base and the official BLAKE3 give two outputs of one input");
        }
    }

    ::fixy::report(::fixy::Sink::Out, "\n=== compare with the official BLAKE3 ===\n");
    std::size_t at = 0;
    for (bench::Report const& report : base_reports) {
        bench::compare(report, official_reports.at(fc::Index<kRows>::check(at).expect("a row"))).print_text();
        ++at;
    }
#endif

    // One JSON tail holds the rows of the two groups, because the reader
    // reads only the last tail of the output.
    ::fixy::FixedList<bench::Report, 2 * kRows> all_reports{};
    for (bench::Report& report : base_reports)
        bench::add_report(all_reports, std::move(report));
#if defined(CRUCIBLE_BENCH_BLAKE3)
    for (bench::Report& report : official_reports)
        bench::add_report(all_reports, std::move(report));
#endif
    bench::emit_reports_json(all_reports.view(), json);
    return 0;
}
