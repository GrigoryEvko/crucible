#include <fixy/os/Topology.h>

#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/os/Directory.h>
#include <foundation/os/Errno.h>
#include <foundation/os/File.h>
#include <foundation/os/PathText.h>
#include <foundation/os/Syscall.h>

#include <linux/auxvec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The probe of the topology.  It runs one time for each process, so this file
// is the one place that compiles it, and an includer of Topology.h compiles
// none of the reads.
//
// THE READS
//   The probe reads each file of sysfs and of /proc through the file door
//   foundation::os::read_file, and it lists each directory through the
//   directory door foundation::os::for_each_entry.  It reads the affinity of
//   the process with the raw system call primitive.  Each read gives a Result,
//   and an error keeps the fallback of the fact that the read gives.  No read
//   throws, so the probe has no handler of an exception.  A failed allocation
//   of a vector ends the process, as each failed allocation of the tree does.
//
// THE PARSE
//   The parsers below read a number as std::from_chars reads it: an optional
//   minus sign for a signed number and one or more decimal digits, with no
//   other sign.  A number that leaves the range of its type is no number.

namespace fixy::concurrent {

namespace topology_detail {

namespace {

namespace base_core = ::foundation::core;
namespace os_door = ::foundation::os;

// The largest count of CPUs that one CPU list can give.  A larger list is
// malformed, and its parse gives no CPU, so a list such as "0-2147483647"
// does not exhaust the memory of the process.
constexpr std::int64_t kLargestCpuListCount = std::int64_t{1} << 20;

// A number at the start of a text (THE PARSE).  used is the count of the
// characters that the number takes.
struct ParsedInt {
    bool is_number = false;
    int value = 0;
    std::size_t used = 0;
};

struct ParsedSize {
    bool is_number = false;
    std::size_t value = 0;
};

// The decimal digits at the start of text, after the sign at position.  The
// magnitude saturates, so a caller sees a value that leaves its range.
struct Digits {
    std::size_t end = 0;
    std::uint64_t magnitude = 0;
    bool is_too_large = false;
};

[[nodiscard]] Digits read_digits(std::string_view text, std::size_t position) noexcept {
    Digits digits{.end = position};
    while (digits.end < text.size() && text[digits.end] >= '0' && text[digits.end] <= '9') {
        auto const digit = static_cast<std::uint64_t>(text[digits.end] - '0');
        std::uint64_t scaled = 0;
        if (__builtin_mul_overflow(digits.magnitude, std::uint64_t{10}, &scaled)
            || __builtin_add_overflow(scaled, digit, &digits.magnitude)) {
            digits.is_too_large = true;
            digits.magnitude = ~std::uint64_t{0};
        }
        ++digits.end;
    }
    return digits;
}

[[nodiscard]] ParsedInt parse_leading_int(std::string_view text) noexcept {
    bool const is_negative = !text.empty() && text[0] == '-';
    std::size_t const first_digit = is_negative ? 1 : 0;
    Digits const digits = read_digits(text, first_digit);
    if (digits.end == first_digit || digits.is_too_large) return ParsedInt{};
    constexpr auto kLargest = static_cast<std::uint64_t>(__INT_MAX__);
    if (digits.magnitude > (is_negative ? kLargest + 1 : kLargest)) return ParsedInt{};
    std::int64_t const signed_value =
        is_negative ? -static_cast<std::int64_t>(digits.magnitude) : static_cast<std::int64_t>(digits.magnitude);
    return ParsedInt{.is_number = true, .value = static_cast<int>(signed_value), .used = digits.end};
}

[[nodiscard]] ParsedSize parse_leading_size(std::string_view text) noexcept {
    Digits const digits = read_digits(text, 0);
    if (digits.end == 0 || digits.is_too_large) return ParsedSize{};
    return ParsedSize{.is_number = true, .value = static_cast<std::size_t>(digits.magnitude)};
}

// The characters of text after its first count characters.  The caller
// checked that count is at most the size of text, so the probe calls no
// substr, which can throw.
[[nodiscard]] std::string_view tail_of(std::string_view text, std::size_t count) noexcept {
    text.remove_prefix(count);
    return text;
}

}  // namespace

std::size_t parse_size_suffix_(std::string_view s) noexcept {
    if (s.empty()) return 0;
    std::size_t multiplier = 1;
    std::string_view digits = s;
    char const suffix = static_cast<char>(s.back() | 0x20);  // the lowercase letter
    if (suffix == 'k' || suffix == 'm' || suffix == 'g') {
        multiplier = suffix == 'k' ? std::size_t{1} << 10 : suffix == 'm' ? std::size_t{1} << 20 : std::size_t{1} << 30;
        digits.remove_suffix(1);
    }
    ParsedSize const parsed = parse_leading_size(digits);
    if (!parsed.is_number) return 0;
    std::size_t bytes = 0;
    if (__builtin_mul_overflow(parsed.value, multiplier, &bytes)) return ~std::size_t{0};
    return bytes;
}

std::vector<int> parse_cpu_list_(std::string_view s) noexcept {
    std::vector<int> result;
    std::size_t position = 0;
    while (position < s.size()) {
        while (position < s.size() && (s[position] == ' ' || s[position] == ',')) {
            ++position;
        }
        if (position >= s.size()) break;
        ParsedInt const first = parse_leading_int(tail_of(s, position));
        if (!first.is_number) return {};
        position += first.used;
        int last = first.value;
        if (position < s.size() && s[position] == '-') {
            ++position;
            ParsedInt const end = parse_leading_int(tail_of(s, position));
            if (!end.is_number) return {};
            position += end.used;
            last = end.value;
        }
        auto const count = std::int64_t{last} - std::int64_t{first.value} + 1;
        if (count > kLargestCpuListCount || static_cast<std::int64_t>(result.size()) + count > kLargestCpuListCount) {
            return {};
        }
        for (std::int64_t cpu = first.value; cpu <= last; ++cpu) {
            result.push_back(static_cast<int>(cpu));
        }
    }
    return result;
}

std::vector<int> parse_int_list_(std::string_view s) noexcept {
    std::vector<int> result;
    std::size_t position = 0;
    while (position < s.size()) {
        while (position < s.size() && (s[position] == ' ' || s[position] == '\t')) {
            ++position;
        }
        if (position >= s.size()) break;
        ParsedInt const parsed = parse_leading_int(tail_of(s, position));
        if (!parsed.is_number) return {};
        position += parsed.used;
        result.push_back(parsed.value);
    }
    return result;
}

namespace {

// The longest path that the probe builds: the root of the tree and a path of
// sysfs under it.  A longer path reads as a file that is not there.
constexpr std::size_t kPathCapacity = 1024;

// The longest value file that the probe reads.  The kernel gives at most one
// page for a value of sysfs.
constexpr std::size_t kValueCapacity = 4096;

// The bytes of /proc/cpuinfo that the probe reads.  The block of the first
// CPU holds the vendor and the model, and it is much shorter.
constexpr std::size_t kCpuInfoCapacity = 16384;

// The bytes of the auxiliary vector that the probe reads.  The kernel gives
// fewer than 64 entries of 16 bytes.
constexpr std::size_t kAuxvCapacity = 4096;

// The words of the affinity mask: 1024 CPUs, the size of cpu_set_t of the C
// library, so the count agrees with CPU_COUNT of sched_getaffinity(3).
constexpr std::size_t kAffinityWords = 16;

// The page size when the auxiliary vector does not give it.
constexpr std::size_t kFallbackPageSize = 4096;

using Path = os_door::PathText<kPathCapacity>;

// The characters of a TextView of the base, for the parsers above.
[[nodiscard]] std::string_view view_of(base_core::TextView text) noexcept {
    return {base_core::detail::text_first_(text), text.size()};
}

// A path that the probe builds piece by piece.  A piece that does not fit
// makes the path partial, and a partial path reads as a file that is not
// there.
class PathBuilder {
public:
    explicit PathBuilder(std::string_view root) noexcept {
        for (char const character : root) {
            is_whole_ = is_whole_ && path_.append(character).is_ok();
        }
    }

    // Appends a string literal of the probe, or the name of an entry that a
    // listing gave.
    PathBuilder& add(base_core::CText text) noexcept {
        is_whole_ = is_whole_ && path_.append(text).is_ok();
        return *this;
    }

    PathBuilder& add_number(int value) noexcept {
        is_whole_ = is_whole_ && value >= 0 && path_.append_decimal(static_cast<std::uint64_t>(value)).is_ok();
        return *this;
    }

    [[nodiscard]] bool is_whole() const noexcept { return is_whole_; }
    [[nodiscard]] Path const& path() const noexcept { return path_; }

private:
    Path path_{};
    bool is_whole_ = true;
};

// The text of one value file of sysfs, as the probe read it before the
// doors: each line joined to the line before it with one space, as a loop
// of std::getline joins them, and the white space at the end removed.
// Empty when the read fails.
class Value {
public:
    [[nodiscard]] std::string_view text() const& noexcept {
        return {base_core::detail::text_first_(bytes_.view()), size_};
    }
    std::string_view text() const&& = delete("the text of a temporary Value points into an object that the full "
                                             "expression ends");

    // Reads the file at path into the value.  Complexity: linear in the
    // bytes of the file.
    void read(base_core::CText path) noexcept {
        if (os_door::read_file(path, bytes_).is_err()) return;
        char* const bytes = base_core::detail::text_bytes_(bytes_);
        std::size_t const count = bytes_.size();
        std::size_t joined = 0;
        std::size_t start = 0;
        while (start < count) {
            std::size_t end = start;
            while (end < count && bytes[end] != '\n') {
                ++end;
            }
            if (joined != 0) {
                bytes[joined] = ' ';
                ++joined;
            }
            for (std::size_t index = start; index < end; ++index) {
                bytes[joined] = bytes[index];
                ++joined;
            }
            start = end + 1;
        }
        while (joined > 0
               && (bytes[joined - 1] == '\n' || bytes[joined - 1] == ' ' || bytes[joined - 1] == '\t'
                   || bytes[joined - 1] == '\r')) {
            --joined;
        }
        size_ = joined;
    }

private:
    base_core::FixedText<kValueCapacity> bytes_{};
    std::size_t size_ = 0;
};

[[nodiscard]] Value read_value(PathBuilder const& builder) noexcept {
    Value value{};
    if (builder.is_whole()) value.read(builder.path().c_text());
    return value;
}

// The id of an entry whose name is prefix and then decimal digits, as the
// probe read it before the doors: each character after the prefix is a
// digit, and the id is zero or more.  No value for each other name.
[[nodiscard]] base_core::Option<int> id_of_entry(base_core::CText name, std::string_view prefix) noexcept {
    std::string_view const text = view_of(name.view());
    if (text.size() <= prefix.size() || !text.starts_with(prefix)) return base_core::none;
    std::string_view const tail = tail_of(text, prefix.size());
    ParsedInt const parsed = parse_leading_int(tail);
    if (!parsed.is_number || parsed.used != tail.size() || parsed.value < 0) return base_core::none;
    return base_core::Option<int>::some(parsed.value);
}

// The ids of the entries of a directory whose names are prefix and then an
// id, in ascending order.  Empty when the listing fails, as the probe gave no
// entry when a walk of std::filesystem threw.  Complexity: linear in the
// entries, and n log n in the ids.
[[nodiscard]] std::vector<int> numbered_entries(PathBuilder const& directory, std::string_view prefix) noexcept {
    std::vector<int> ids;
    if (!directory.is_whole()) return ids;
    bool const is_listed =
        os_door::for_each_entry(directory.path().c_text(), [&ids, prefix](base_core::CText name) noexcept {
            id_of_entry(name, prefix).match([&ids](int id) { ids.push_back(id); }, [] {});
            return os_door::Visit::proceed;
        }).is_ok();
    if (!is_listed) return {};
    std::sort(ids.begin(), ids.end());
    return ids;
}

// The CPUs that the kernel reports ONLINE, from the CPU directory of the tree.
//
// The CPU directory holds entries such as "cpufreq" and "cpuidle" beside the
// entries of the CPUs, so only "cpu" and then digits counts.
//
// A `cpuN` directory survives the CPU going offline, and only `cpuN/online`
// flips to 0, so the directory alone counts hardware that the process can
// never run on.  Each other sysfs source that this file reads (node
// `cpulist`, cache `shared_cpu_list`) reports online CPUs only, and a mix of
// the two sets makes `num_smt_threads()` disagree with the union of
// `cores_on_node()`.
//
// Measured on the bench host with cores 280-287 offlined for SMT isolation:
// 384 `cpuN` directories against 376 CPUs in the two node cpulists.  A
// caller that sizes a per-CPU array or a thread pool from the larger number
// reserves for eight CPUs that cannot be scheduled on, and a scheduler that
// pins by index can pick one of them.
//
// `cpu0` ships no `online` file on most kernels, because it cannot be
// offlined, and a kernel built without CPU hotplug ships none at all.  A file
// that the probe cannot read therefore reads as online.  That is the
// fail-open direction, and it keeps this probe working on those kernels.
[[nodiscard]] std::vector<int> enumerate_cpus_(std::string_view sysfs_root) noexcept {
    std::vector<int> const listed = numbered_entries(PathBuilder{sysfs_root}.add("/devices/system/cpu"), "cpu");
    std::vector<int> cpus;
    for (int const cpu : listed) {
        Value const online =
            read_value(PathBuilder{sysfs_root}.add("/devices/system/cpu/cpu").add_number(cpu).add("/online"));
        if (online.text() == "0") continue;
        cpus.push_back(cpu);
    }
    return cpus;
}

// The CPUs that the process can run on: the count of the affinity mask, as
// CPU_COUNT gives it after sched_getaffinity(3).  Zero when the call fails,
// which the caller reads as "use the count of the online CPUs".
[[nodiscard]] std::size_t probe_process_cpu_count_() noexcept {
    std::uint64_t mask[kAffinityWords]{};
    return os_door::result_of(os_door::raw_syscall(os_door::Syscall::sched_getaffinity, 0, sizeof mask,
                                                   static_cast<void const*>(mask)))
        .match(
            [&mask](std::uint64_t) {
                // Each step clears the lowest set bit, so the count of the
                // steps is the count of the CPUs, with no call of libgcc.
                std::size_t count = 0;
                for (std::uint64_t word : mask) {
                    while (word != 0) {
                        word &= word - 1;
                        ++count;
                    }
                }
                return count;
            },
            [](os_door::Errno) { return std::size_t{0}; });
}

// The count of the online CPUs of the host, from the list of the kernel, as
// get_nprocs(3) of the C library reads it.  It reads /sys of the host and not
// the tree of a test, as the count of the standard library that the probe
// read before.  Zero when the list cannot be read.
[[nodiscard]] std::size_t probe_online_cpu_count_() noexcept {
    Value online{};
    online.read("/sys/devices/system/cpu/online");
    return parse_cpu_list_(online.text()).size();
}

// The page size that the kernel gives the process in the auxiliary vector
// (AT_PAGESZ), as sysconf(_SC_PAGESIZE) gives it.  Each entry is two words:
// the type and the value, and the type AT_NULL ends the vector.
[[nodiscard]] std::size_t probe_page_size_() noexcept {
    base_core::FixedText<kAuxvCapacity> auxiliary{};
    if (os_door::read_file("/proc/self/auxv", auxiliary).is_err()) return kFallbackPageSize;
    char const* const bytes = base_core::detail::text_first_(auxiliary.view());
    constexpr std::size_t kEntryBytes = 2 * sizeof(std::uint64_t);
    for (std::size_t offset = 0; offset + kEntryBytes <= auxiliary.size(); offset += kEntryBytes) {
        std::uint64_t type = 0;
        std::uint64_t value = 0;
        __builtin_memcpy(&type, bytes + offset, sizeof type);
        __builtin_memcpy(&value, bytes + offset + sizeof type, sizeof value);
        if (type == AT_NULL) break;
        if (type == AT_PAGESZ && value > 0) return static_cast<std::size_t>(value);
    }
    return kFallbackPageSize;
}

// The kernel writes all three settings and brackets the active one, as in
// "always [madvise] never".  Either "always" or "madvise" counts as
// available, the second one when the allocation asks for it.
[[nodiscard]] bool probe_hugepage_2mb_available_(std::string_view sysfs_root) noexcept {
    Value const contents = read_value(PathBuilder{sysfs_root}.add("/kernel/mm/transparent_hugepage/enabled"));
    std::string_view const text = contents.text();
    return text.find("[always]") != std::string_view::npos || text.find("[madvise]") != std::string_view::npos;
}

// The text of a line of /proc/cpuinfo after its colon and the space after
// it, when the line starts with key.  No text when the line is another line.
[[nodiscard]] std::string_view field_of(std::string_view line, std::string_view key) noexcept {
    if (!line.starts_with(key)) return {};
    std::size_t const colon = line.find(':');
    if (colon == std::string_view::npos || colon + 2 >= line.size()) return {};
    return tail_of(line, colon + 2);
}

// Only the first entry is read.  This assumes that each CPU of the machine
// is the same model.  A line that the buffer cuts is not read, so a cut
// model name never reads as the model.
[[nodiscard]] std::pair<std::string, std::string> probe_cpu_vendor_and_model_() noexcept {
    std::string vendor;
    std::string model;
    base_core::FixedText<kCpuInfoCapacity> info{};
    if (os_door::read_file("/proc/cpuinfo", info).is_err()) return {vendor, model};
    std::string_view text = view_of(info.view());
    if (info.size() == kCpuInfoCapacity) {
        std::size_t const last_break = text.rfind('\n');
        text =
            last_break == std::string_view::npos ? std::string_view{} : std::string_view{text.data(), last_break + 1};
    }
    while (!text.empty() && (vendor.empty() || model.empty())) {
        std::size_t const line_end = text.find('\n');
        std::string_view const line =
            line_end == std::string_view::npos ? text : std::string_view{text.data(), line_end};
        text = line_end == std::string_view::npos ? std::string_view{} : tail_of(text, line_end + 1);
        if (vendor.empty()) {
            vendor = std::string{field_of(line, "vendor_id")};
        }
        if (model.empty()) {
            model = std::string{field_of(line, "model name")};
        }
    }
    return {vendor, model};
}

// The cache sizes and the line size that the probe found.  The probe sets
// each field to the value that the topology already holds, so a field that
// sysfs does not give keeps the fallback.
struct CacheGeometry {
    std::size_t l1d = 0;
    std::size_t l1i = 0;
    std::size_t l2 = 0;
    std::size_t l3 = 0;
    std::size_t line = 0;
};

// Each of the four steps of the probe below is a function of its own.
// noinline prevents an inline expansion of the four steps into probe_linux_.
// One function with all four steps has more than 60 KB of machine code in a
// Debug build, and the debug-information passes become slow on it.

// Reads the cache directory of one CPU into geometry.  A CPU with no cache
// directory keeps the fallback sizes.  An entry of the directory is an index
// directory when its name is "index" and at least one more character.
[[gnu::noinline]] void probe_cache_geometry_(std::string_view sysfs_root, int cpu, CacheGeometry& geometry) noexcept {
    PathBuilder const cache_dir = PathBuilder{sysfs_root}.add("/devices/system/cpu/cpu").add_number(cpu).add("/cache");
    if (!cache_dir.is_whole()) return;
    static_cast<void>(os_door::for_each_entry(cache_dir.path().c_text(), [&](base_core::CText name) noexcept {
        std::string_view const entry = view_of(name.view());
        if (entry.size() < 6 || !entry.starts_with("index")) return os_door::Visit::proceed;
        PathBuilder index_dir = cache_dir;
        index_dir.add("/").add(name);
        Value const level_text = read_value(PathBuilder{index_dir}.add("/level"));
        Value const type_text = read_value(PathBuilder{index_dir}.add("/type"));
        Value const size_text = read_value(PathBuilder{index_dir}.add("/size"));
        Value const line_text = read_value(PathBuilder{index_dir}.add("/coherency_line_size"));

        ParsedInt const level = parse_leading_int(level_text.text());
        std::size_t const size = parse_size_suffix_(size_text.text());
        ParsedSize const line = parse_leading_size(line_text.text());

        if (line.is_number && line.value > 0) geometry.line = line.value;
        if (size == 0) return os_door::Visit::proceed;

        int const level_value = level.is_number ? level.value : 0;
        std::string_view const type = type_text.text();
        if (level_value == 1) {
            // Sysfs reports the two halves of L1 as separate entries.  Some
            // designs report one unified cache instead, which stands for
            // both.
            if (type == "Data") {
                geometry.l1d = size;
            } else if (type == "Instruction") {
                geometry.l1i = size;
            } else if (type == "Unified") {
                geometry.l1d = size;
                geometry.l1i = size;
            }
        } else if (level_value == 2) {
            geometry.l2 = size;
        } else if (level_value == 3) {
            geometry.l3 = size;
        }
        return os_door::Visit::proceed;
    }));
}

// Hardware threads on one core report the same core id, so the number of
// distinct ids is the number of physical cores, WITHIN ONE PACKAGE.
// `core_id` is unique per package, not per machine: every socket numbers its
// cores from 0, so socket 1's core 0 and socket 0's core 0 collide.  Counting
// bare core_ids therefore reports the cores of a single socket and divides
// the true count by the number of sockets.
//
// Measured on the bench host (2 x EPYC 9655): 96 distinct core_ids against
// 192 distinct (package, core) pairs.  That undercount fed `smt_factor()`,
// which is `threads_ / cores_`, and turned an SMT2 machine into an apparent
// SMT4 one, a number plausible enough to pass the sanity test in
// test_topology.cpp, which is why it went unnoticed.
//
// The pairs go into one sorted vector of keys, and the count of distinct
// keys is the count of the cores.  Zero when no CPU gives a core id.
// Complexity: n log n in the CPUs.
[[nodiscard, gnu::noinline]] std::size_t count_physical_cores_(std::string_view sysfs_root,
                                                               std::vector<int> const& cpus) noexcept {
    std::vector<std::uint64_t> keys;
    for (int const cpu : cpus) {
        PathBuilder const topology_dir =
            PathBuilder{sysfs_root}.add("/devices/system/cpu/cpu").add_number(cpu).add("/topology/");
        Value const core_text = read_value(PathBuilder{topology_dir}.add("core_id"));
        ParsedInt const core = parse_leading_int(core_text.text());
        int const core_id = core.is_number ? core.value : -1;
        if (core_id < 0) continue;

        // A single-socket kernel, or one without package topology, ships no
        // physical_package_id.  Package 0 for all is then correct, and it
        // reduces this count to the count of one package.
        Value const package_text = read_value(PathBuilder{topology_dir}.add("physical_package_id"));
        ParsedInt const package = parse_leading_int(package_text.text());
        int const package_id = package.is_number ? package.value : 0;

        keys.push_back((static_cast<std::uint64_t>(static_cast<std::uint32_t>(package_id)) << 32)
                       | static_cast<std::uint32_t>(core_id));
    }
    std::sort(keys.begin(), keys.end());
    return static_cast<std::size_t>(std::unique(keys.begin(), keys.end()) - keys.begin());
}

// Every CPU that shares one L3 reports the same set in shared_cpu_list, so
// the sorted set of each CPU, with each repeat dropped, leaves one entry for
// each L3.  The groups keep the order in which a CPU first reports them.
//
// No group when no CPU gives an L3.  Complexity: linear in the CPUs times
// the groups.
[[nodiscard, gnu::noinline]] std::vector<std::vector<int>> probe_l3_groups_(std::string_view sysfs_root,
                                                                            std::vector<int> const& cpus) noexcept {
    std::vector<std::vector<int>> all_groups;
    for (int const cpu : cpus) {
        PathBuilder const cache_dir =
            PathBuilder{sysfs_root}.add("/devices/system/cpu/cpu").add_number(cpu).add("/cache");
        if (!cache_dir.is_whole()) continue;
        static_cast<void>(os_door::for_each_entry(cache_dir.path().c_text(), [&](base_core::CText name) noexcept {
            std::string_view const entry = view_of(name.view());
            if (entry.size() < 6 || !entry.starts_with("index")) return os_door::Visit::proceed;
            PathBuilder index_dir = cache_dir;
            index_dir.add("/").add(name);
            Value const level_text = read_value(PathBuilder{index_dir}.add("/level"));
            ParsedInt const level = parse_leading_int(level_text.text());
            if (!level.is_number || level.value != 3) return os_door::Visit::proceed;

            Value const shared_text = read_value(PathBuilder{index_dir}.add("/shared_cpu_list"));
            std::vector<int> group = parse_cpu_list_(shared_text.text());
            std::sort(group.begin(), group.end());
            if (group.empty()) return os_door::Visit::proceed;
            if (std::find(all_groups.begin(), all_groups.end(), group) == all_groups.end()) {
                all_groups.push_back(std::move(group));
            }
            return os_door::Visit::stop;  // one L3 for each CPU
        }));
    }
    return all_groups;
}

// The three parallel tables of the NUMA nodes: the ids, the CPUs and the
// distance rows.
struct NumaTables {
    std::vector<int> node_ids;
    std::vector<std::vector<int>> cores_on_node;
    std::vector<std::vector<int>> distance;
};

// The node ids in ascending order.  They are ids, not positions: a host with
// node0 and node2 gives {0, 2}.  No node when sysfs names none.
[[nodiscard, gnu::noinline]] NumaTables probe_numa_tables_(std::string_view sysfs_root) noexcept {
    NumaTables tables{};
    std::vector<int> nodes = numbered_entries(PathBuilder{sysfs_root}.add("/devices/system/node"), "node");
    if (nodes.empty()) return tables;

    std::vector<std::vector<int>> new_cores_on_node(nodes.size());
    std::vector<std::vector<int>> new_numa_distance(nodes.size());

    for (std::size_t position = 0; position < nodes.size(); ++position) {
        PathBuilder const node_dir =
            PathBuilder{sysfs_root}.add("/devices/system/node/node").add_number(nodes[position]);
        Value const cpulist_text = read_value(PathBuilder{node_dir}.add("/cpulist"));
        std::vector<int> cpus_on_node = parse_cpu_list_(cpulist_text.text());
        std::sort(cpus_on_node.begin(), cpus_on_node.end());
        new_cores_on_node[position] = std::move(cpus_on_node);

        // A row with one entry per node indexes by position.  A missing row,
        // or one of another length, reads as the conventional distances: 10
        // to the node itself and 20 to each other node.
        Value const distance_text = read_value(PathBuilder{node_dir}.add("/distance"));
        std::vector<int> distance_row = parse_int_list_(distance_text.text());
        if (distance_row.size() != nodes.size()) {
            distance_row.assign(nodes.size(), 20);
            distance_row[position] = 10;
        }
        new_numa_distance[position] = std::move(distance_row);
    }

    tables.node_ids = std::move(nodes);
    tables.cores_on_node = std::move(new_cores_on_node);
    tables.distance = std::move(new_numa_distance);
    return tables;
}

}  // namespace

}  // namespace topology_detail

Topology::Topology(std::string_view sysfs_root) noexcept {
    // The online CPUs of the host, then the CPUs of the process, then one
    // CPU, as the count of the standard library falls back.
    std::size_t const online = topology_detail::probe_online_cpu_count_();
    std::size_t const allowed = topology_detail::probe_process_cpu_count_();
    cores_ = online != 0 ? online : allowed != 0 ? allowed : 1;
    threads_ = cores_;

    process_cpus_ = (allowed > 0) ? allowed : threads_;

    page_size_ = topology_detail::probe_page_size_();

    hugepage_2mb_ = topology_detail::probe_hugepage_2mb_available_(sysfs_root);

    auto [vendor, model] = topology_detail::probe_cpu_vendor_and_model_();
    cpu_vendor_ = std::move(vendor);
    cpu_model_ = std::move(model);

    // One node with id 0, and the conventional distance from a node to
    // itself.
    numa_node_ids_.push_back(0);
    cores_on_node_.push_back({});
    numa_distance_.push_back({10});
    for (std::size_t i = 0; i < cores_; ++i) {
        cores_on_node_[0].push_back(static_cast<int>(i));
    }

    probe_linux_(sysfs_root);
}

void Topology::log_summary(FILE* out) const noexcept {
    std::fprintf(out,
                 "fixy::Topology(source=%s):\n"
                 "  CPU:    vendor=\"%s\" model=\"%s\"\n"
                 "  Cores:  physical=%zu smt_threads=%zu smt_factor=%zu process_allowed=%zu\n"
                 "  Caches: l1d=%zu KB l1i=%zu KB l2=%zu KB l3=%zu MB line=%zu\n"
                 "  Groups: l3_clusters=%zu cores_per_socket=%zu\n"
                 "  NUMA:   nodes=%zu\n"
                 "  Memory: page_size=%zu B hugepage_2mb=%s\n",
                 (source_ == Source::Sysfs ? "Sysfs" : "Fallback"), cpu_vendor_.empty() ? "?" : cpu_vendor_.c_str(),
                 cpu_model_.empty() ? "?" : cpu_model_.c_str(), cores_, threads_, smt_factor(), process_cpus_,
                 l1d_ / 1024, l1i_ / 1024, l2_ / 1024, l3_ / (1024 * 1024), line_, l3_groups_.size(),
                 cores_per_socket(), numa_nodes(), page_size_, (hugepage_2mb_ ? "yes" : "no"));
}

void Topology::probe_linux_(std::string_view sysfs_root) noexcept {
    using namespace topology_detail;

    const auto cpus = enumerate_cpus_(sysfs_root);
    if (cpus.empty()) return;  // no sysfs: keep the fallback values

    // Sysfs beats the count of the online CPUs when CPUs have been unplugged,
    // but only because `enumerate_cpus_` filters on `cpuN/online`.  The
    // directory glob on its own counts unplugged CPUs.
    threads_ = cpus.size();

    // Reading one CPU's caches assumes every core has the same hierarchy.  A
    // part that mixes performance and efficiency cores would need each core
    // probed separately.

    // A CPU with no cache directory keeps the fallback sizes, and the probe
    // goes on to the cores, the L3 groups and the NUMA nodes.
    CacheGeometry geometry{.l1d = l1d_, .l1i = l1i_, .l2 = l2_, .l3 = l3_, .line = line_};
    probe_cache_geometry_(sysfs_root, cpus[0], geometry);
    l1d_ = geometry.l1d;
    l1i_ = geometry.l1i;
    l2_ = geometry.l2;
    l3_ = geometry.l3;
    line_ = geometry.line;

    const std::size_t physical_cores = count_physical_cores_(sysfs_root, cpus);
    if (physical_cores != 0) {
        cores_ = physical_cores;
    }

    auto all_groups = probe_l3_groups_(sysfs_root, cpus);
    if (!all_groups.empty()) {
        l3_groups_ = std::move(all_groups);
    }

    NumaTables numa = probe_numa_tables_(sysfs_root);
    if (!numa.node_ids.empty()) {
        numa_node_ids_ = std::move(numa.node_ids);
        cores_on_node_ = std::move(numa.cores_on_node);
        numa_distance_ = std::move(numa.distance);
    }

    source_ = Source::Sysfs;
}

}  // namespace fixy::concurrent
