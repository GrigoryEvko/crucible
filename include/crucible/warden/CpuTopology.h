#pragma once

// CPU and NUMA placement read straight out of sysfs and procfs. A
// general topology library would cover far more than the handful of
// facts needed here, so the few files are parsed directly instead.
//
// A query that cannot read what it needs returns an empty set or a
// sentinel rather than failing, so an older kernel, a chroot or an
// unusual filesystem costs a degraded answer and nothing worse.
//
// Only the two parsers below carry gnu::pure. Their answer is a
// function of the characters of the TextView they are handed, and
// that is memory the compiler models, so a reader can check the
// justification by reading the body: no file is opened and no kernel
// state is consulted.
//
// The queries that read sysfs or procfs deliberately carry no such
// attribute. gnu::pure promises the result depends only on the
// arguments and on memory the compiler can see, and the contents of
// /sys are neither. GCC 16 at -O2 acts on the promise: two source-level
// reads of one path emit a single read when the reader is marked pure
// and two when it is not. Folding is a wrong answer rather than a fast
// one for a sampling query such as cpu_cur_freq_khz, whose value moves
// every few milliseconds. Nothing here sits on a hot path, so the fold
// bought nothing worth that.
//
// Each query that reads a file, or that lists a directory, takes a
// context that owns IO and Block, because the open and the read can wait
// on the kernel.  The gate is fixy::CtxFitsFileOpen.  A query of the
// affinity of the calling thread can also ask the kernel through the
// affinity door, so its gate is CtxFitsAffinityQuery.  Only the two
// parsers take no context.
//
// Each query of this header is a gate: it checks the context, and it calls
// its body in src/warden/CpuTopology.cpp.  No query is hot, so an includer
// compiles the gates and not the bodies.

#include <fixy/Core.h>
#include <fixy/os/OwnedFile.h>
#include <fixy/os/Sched.h>
#include <foundation/contracts/Pre.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace crucible::warden {

// The tests reach into this namespace to exercise the parser on its
// own, so these names are part of the tested surface.

namespace detail {

// One past the largest CPU id that a list may name.  The kernel's own
// ceiling, CONFIG_NR_CPUS, is at most 8192 on every distribution kernel.
inline constexpr std::size_t kMaxCpuCount = 8192;

}  // namespace detail

class CpuSetCursor;

// A set of CPU ids, each below detail::kMaxCpuCount, as one bit for each
// id.  So the set is sorted and holds no duplicate, whatever the text that
// a parser read repeats.  The loop `for (int cpu : set)` gives each id in
// ascending order.  A set has 1 KiB.
class CpuSet {
public:
    static constexpr std::size_t capacity = detail::kMaxCpuCount;

    // The empty set.
    constexpr CpuSet() noexcept = default;

    // Adds each id from first to last, both included.  Complexity: linear in
    // the count of the words of the range.
    constexpr void insert_range(std::size_t first, std::size_t last) noexcept {
        CRUCIBLE_PRE(first <= last && last < capacity);
        for (std::size_t word = first / 64; word <= last / 64; ++word) {
            std::size_t const low = word * 64;
            std::size_t const from = first > low ? first - low : 0;
            std::size_t const to = last < low + 63 ? last - low : 63;
            words_.at(word_of_(low)) |= ::fixy::low_mask<std::uint64_t>(static_cast<int>(to - from + 1)) << from;
        }
    }

    constexpr void insert(std::size_t cpu) noexcept {
        CRUCIBLE_PRE(cpu < capacity);
        words_.at(word_of_(cpu)) |= std::uint64_t{1} << (cpu % 64);
    }

    // True when the set holds cpu.  A negative id and an id at or past the
    // capacity are in no set.
    [[nodiscard]] constexpr bool contains(int cpu) const noexcept {
        if (cpu < 0 || static_cast<std::size_t>(cpu) >= capacity) {
            return false;
        }
        std::size_t const id = static_cast<std::size_t>(cpu);
        return ((words_.at(word_of_(id)) >> (id % 64)) & 1u) != 0;
    }

    // Complexity: linear in the words of the set.
    [[nodiscard]] constexpr bool is_empty() const noexcept {
        for (std::uint64_t const word : words_) {
            if (word != 0) {
                return false;
            }
        }
        return true;
    }

    // The count of the ids.  Complexity: linear in the words of the set.
    [[nodiscard]] constexpr std::size_t size() const noexcept {
        std::size_t count = 0;
        for (std::uint64_t const word : words_) {
            count += static_cast<std::size_t>(::fixy::popcount(word));
        }
        return count;
    }

    // The ids that the two sets both hold.
    [[nodiscard]] constexpr CpuSet intersection(CpuSet const& other) const noexcept {
        CpuSet both{};
        for (WordIndex const index : words_.indices()) {
            both.words_.at(index) = words_.at(index) & other.words_.at(index);
        }
        return both;
    }

    // The lowest id, or no value for the empty set.
    [[nodiscard]] constexpr ::fixy::Option<int> first() const noexcept {
        std::size_t const position = next_from_(0);
        if (position == capacity) {
            return ::fixy::none;
        }
        return ::fixy::some(static_cast<int>(position));
    }

    // The highest id, or no value for the empty set.  Complexity: linear in
    // the words of the set.
    [[nodiscard]] constexpr ::fixy::Option<int> last() const noexcept {
        ::fixy::Option<int> highest = ::fixy::none;
        for (WordIndex const index : words_.indices()) {
            std::uint64_t const word = words_.at(index);
            if (word != 0) {
                std::size_t const top_bit = 63 - static_cast<std::size_t>(::fixy::countl_zero(word));
                highest = ::fixy::some(static_cast<int>(index.raw() * 64 + top_bit));
            }
        }
        return highest;
    }

    [[nodiscard]] constexpr CpuSetCursor begin() const& noexcept;
    [[nodiscard]] constexpr CpuSetCursor end() const& noexcept;
    void begin() const&& = delete("a cursor of a temporary CpuSet points into an object that the full expression ends");
    void end() const&& = delete("a cursor of a temporary CpuSet points into an object that the full expression ends");

    [[nodiscard]] friend constexpr bool operator==(CpuSet const& left, CpuSet const& right) noexcept {
        return left.words_ == right.words_;
    }

private:
    friend class CpuSetCursor;

    static constexpr std::size_t kWords = capacity / 64;
    using WordIndex = ::fixy::Index<kWords>;

    // The word of an id below the capacity.
    [[nodiscard]] static constexpr WordIndex word_of_(std::size_t id) noexcept {
        return WordIndex::check(id / 64).expect("an id below the capacity of a CpuSet has a word of the set");
    }

    // The first id at or after position that the set holds, or the capacity
    // when no id is there.  Complexity: linear in the words after position.
    [[nodiscard]] constexpr std::size_t next_from_(std::size_t position) const noexcept {
        while (position < capacity) {
            std::uint64_t const word = words_.at(word_of_(position)) >> (position % 64);
            if (word != 0) {
                return position + static_cast<std::size_t>(::fixy::countr_zero(word));
            }
            position = (position / 64 + 1) * 64;
        }
        return capacity;
    }

    ::foundation::core::FixedArray<std::uint64_t, kWords> words_{};
};

// The position of a loop over a CpuSet: the next id of the set, or the
// capacity of the set at the end.  It comes only from begin() and end() of
// a CpuSet, and it borrows that set.
class CpuSetCursor {
public:
    [[nodiscard]] constexpr int operator*() const noexcept { return static_cast<int>(position_); }

    constexpr CpuSetCursor& operator++() noexcept {
        position_ = (*set_).next_from_(position_ + 1);
        return *this;
    }

    [[nodiscard]] friend constexpr bool operator==(CpuSetCursor const& left, CpuSetCursor const& right) noexcept {
        return left.position_ == right.position_;
    }

private:
    friend class CpuSet;

    constexpr CpuSetCursor(::fixy::Ref<CpuSet const> set, std::size_t position) noexcept
        : set_{set}, position_{position} {}

    ::fixy::Ref<CpuSet const> set_;
    std::size_t position_ = CpuSet::capacity;
};

constexpr CpuSetCursor CpuSet::begin() const& noexcept { return CpuSetCursor{::fixy::to(*this), next_from_(0)}; }

constexpr CpuSetCursor CpuSet::end() const& noexcept { return CpuSetCursor{::fixy::to(*this), capacity}; }

// A query of the affinity of the calling thread reads procfs, and when
// procfs gives no answer, it asks the kernel through the affinity door of
// fixy/os/Sched.h.  So its context can open a file and read the affinity.
template <class Ctx>
concept CtxFitsAffinityQuery = ::fixy::CtxFitsFileOpen<Ctx> && ::fixy::sched::CtxFitsRuntimeAffinity<Ctx>;

struct CoreSelector {
    bool prefer_isolcpu = true;
    bool prefer_p_core = true;
    bool avoid_smt_sibling = true;
    int explicit_cpu = -1;  // taken only if it is also allowed

    // The first CPU is where the timer interrupt lands by default,
    // where a newly registered interrupt is first steered, and where
    // deferred kernel callbacks run unless the command line moved them
    // elsewhere. Its hyperthread sibling shares the low-level caches
    // with that work. Both are noisier than the rest of the machine.
    //
    // This is a preference and not an exclusion: the last-resort branch
    // of select_hot_cpu still returns the first CPU when it is the only
    // one available.
    bool avoid_cpu0 = true;

    int numa_hint = -1;  // -1 expresses no preference
};

namespace detail {

// The first count characters of text, or the whole text when count is not
// less than its size.
[[nodiscard]] constexpr ::fixy::TextView text_prefix(::fixy::TextView text, std::size_t count) noexcept {
    return text.window(0, count < text.size() ? count : text.size()).value_or(::fixy::TextView{});
}

// The characters of text after its first count characters, or no character
// when count is not less than its size.
[[nodiscard]] constexpr ::fixy::TextView text_suffix(::fixy::TextView text, std::size_t count) noexcept {
    if (count >= text.size()) {
        return ::fixy::TextView{};
    }
    return text.window(count, text.size() - count).value_or(::fixy::TextView{});
}

// The characters of text with no space or tab at either end.  Complexity:
// linear in the size of text.
[[nodiscard]] constexpr ::fixy::TextView trim_blanks(::fixy::TextView text) noexcept {
    std::size_t first = text.size();
    std::size_t end = 0;
    std::size_t position = 0;
    for (char const character : text) {
        if (character != ' ' && character != '\t') {
            if (first == text.size()) {
                first = position;
            }
            end = position + 1;
        }
        ++position;
    }
    if (first == text.size()) {
        return ::fixy::TextView{};
    }
    return text.window(first, end - first).value_or(::fixy::TextView{});
}

// The id of a CPU in decimal digits, or no value for text that is not a
// run of decimal digits, and for an id at or past kMaxCpuCount.  The parse
// of the Report family also reads a 0x prefix, so the walk of the digits
// refuses each character other than a digit first.
[[nodiscard]] constexpr ::fixy::Option<std::size_t> parse_cpu_id(::fixy::TextView digits) noexcept {
    if (digits.size() == 0) {
        return ::fixy::none;
    }
    for (char const character : digits) {
        if (character < '0' || character > '9') {
            return ::fixy::none;
        }
    }
    return ::fixy::parse<std::size_t>(digits).match(
        [](std::size_t cpu) noexcept -> ::fixy::Option<std::size_t> {
            if (cpu >= kMaxCpuCount) {
                return ::fixy::none;
            }
            return ::fixy::some(cpu);
        },
        [](::fixy::ParseError) noexcept -> ::fixy::Option<std::size_t> { return ::fixy::none; });
}

// A kernel CPU list names single CPUs and inclusive ranges, separated
// by commas, as in "0-3,5,7-9,15".  Spaces and tabs around an entry are
// skipped.  Text that is not such a list gives an empty set: an entry
// that is not a number or a range, an id at or past kMaxCpuCount, or a
// range whose end is below its start.  Each caller treats an empty set
// as an unknown answer, so malformed text costs a degraded answer.
//
// Pure: the answer reads only the characters of text, and it opens no
// file.  Complexity: linear in the size of text, plus the words of each
// range.
[[nodiscard, gnu::pure]] constexpr CpuSet parse_cpulist(::fixy::TextView text) noexcept {
    CpuSet named{};
    ::fixy::TextView rest = text;
    while (rest.size() != 0) {
        std::size_t const comma = ::fixy::find(rest, ',').value_or(rest.size());
        ::fixy::TextView const entry = trim_blanks(text_prefix(rest, comma));
        rest = text_suffix(rest, comma + 1);
        if (entry.size() == 0) {
            continue;
        }

        std::size_t const dash = ::fixy::find(entry, '-').value_or(entry.size());
        ::fixy::Option<std::size_t> first_id = parse_cpu_id(text_prefix(entry, dash));
        if (first_id.is_none()) {
            return CpuSet{};
        }
        std::size_t const first = std::move(first_id).expect("the test above found an id");
        std::size_t last = first;
        if (dash < entry.size()) {
            ::fixy::Option<std::size_t> last_id = parse_cpu_id(text_suffix(entry, dash + 1));
            if (last_id.is_none()) {
                return CpuSet{};
            }
            last = std::move(last_id).expect("the test above found an id");
        }
        if (last < first) {
            return CpuSet{};
        }
        named.insert_range(first, last);
    }
    return named;
}

// /proc/thread-self/status carries one line per field. The line of
// interest reads "Cpus_allowed_list:" followed by a tab and a CPU list.
//
// Pure: the caller hands in the already-read text, so this reads only
// the characters of status and opens no file.
[[nodiscard, gnu::pure]] constexpr CpuSet parse_cpus_allowed_list(::fixy::TextView status) noexcept {
    constexpr ::fixy::TextView key = "Cpus_allowed_list:";
    ::fixy::TextView rest = status;
    while (rest.size() != 0) {
        std::size_t const line_end = ::fixy::find(rest, '\n').value_or(rest.size());
        ::fixy::TextView const line = text_prefix(rest, line_end);
        rest = text_suffix(rest, line_end + 1);
        if (::fixy::starts_with(line, key)) {
            return parse_cpulist(text_suffix(line, key.size()));
        }
    }
    return CpuSet{};
}

// The bodies of the queries below, in src/warden/CpuTopology.cpp.  Each
// reads sysfs or procfs, so only a gate of this header calls one.  A read
// that fails gives the answer that its query documents.
[[nodiscard]] int read_online_cpu_count() noexcept;
[[nodiscard]] CpuSet read_allowed_cpus_of_thread() noexcept;
[[nodiscard]] CpuSet cpus_of_affinity(::fixy::Option<::foundation::algebra::lattices::AffinityMask> mask) noexcept;
[[nodiscard]] CpuSet read_isolated_cpus() noexcept;
[[nodiscard]] CpuSet read_smt_siblings(int cpu) noexcept;
[[nodiscard]] bool read_is_p_core(int cpu) noexcept;
[[nodiscard]] int read_numa_node_of(int cpu) noexcept;
[[nodiscard]] int read_numa_node_of_device(::fixy::CText sysfs_numa_node_path) noexcept;
[[nodiscard]] std::uint64_t read_cpu_cur_freq_khz(int cpu) noexcept;
[[nodiscard]] std::uint64_t read_cpu_max_freq_khz(int cpu) noexcept;
[[nodiscard]] int select_hot_cpu_of(CpuSet const& allowed, CoreSelector const& sel, CpuSet const& exclude) noexcept;
[[nodiscard]] CpuSet select_warm_cpus_of(CpuSet const& allowed, int hot_cpu, int count) noexcept;

}  // namespace detail

// glibc answers _SC_NPROCESSORS_ONLN with a read of
// /sys/devices/system/cpu/online, and this query reads the same file.
// One on failure, which is the conservative answer.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline int num_online_cpus(Ctx const&) noexcept {
    return detail::read_online_cpu_count();
}

namespace detail {

// Reached when procfs is unreadable or malformed. Ask the kernel
// directly, and if even that fails assume every online CPU is
// available.
template <class Ctx>
    requires CtxFitsAffinityQuery<Ctx>
[[nodiscard, gnu::cold]] inline CpuSet allowed_cpus_fallback(Ctx const& ctx) noexcept {
    return cpus_of_affinity(::fixy::sched::read_affinity(ctx).ok());
}

}  // namespace detail

// The CPUs the calling thread may run on, as granted by whatever placed
// it. Procfs is the authoritative source and is consulted first. The file
// is the status of the calling thread. /proc/self/status gives the
// affinity of the main thread of the process, and the fallback below
// gives the affinity of the calling thread, so the two would not agree.
template <class Ctx>
    requires CtxFitsAffinityQuery<Ctx>
[[nodiscard]] inline CpuSet allowed_cpus(Ctx const& ctx) noexcept {
    CpuSet allowed = detail::read_allowed_cpus_of_thread();
    if (!allowed.is_empty()) {
        return allowed;
    }
    return detail::allowed_cpus_fallback(ctx);
}

// The CPUs the kernel command line has withheld from the scheduler.
// Empty when no such argument was given.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline CpuSet isolated_cpus(Ctx const&) noexcept {
    return detail::read_isolated_cpus();
}

template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline CpuSet smt_siblings(Ctx const&, int cpu) noexcept {
    return detail::read_smt_siblings(cpu);
}

// On a hybrid part the core-type file reads "Core" for a performance
// core and "Atom" for an efficiency core. A uniform part does not
// publish the file at all, and its absence counts as a performance
// core so that every CPU on such a host answers the same way.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline bool is_p_core(Ctx const&, int cpu) noexcept {
    return detail::read_is_p_core(cpu);
}

// The NUMA node a CPU belongs to, or -1 when it cannot be determined.
// The kernel publishes the membership as a symlink named after the
// node, so the node number is the number of the entry of the CPU
// directory whose name starts with "node".
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline int numa_node_of(Ctx const&, int cpu) noexcept {
    return detail::read_numa_node_of(cpu);
}

// The NUMA node nearest a device. The caller passes the sysfs path of
// that device's numa_node file. -1 when it cannot be read.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline int numa_node_of_device(Ctx const&, ::fixy::CText sysfs_numa_node_path) noexcept {
    return detail::read_numa_node_of_device(sysfs_numa_node_path);
}

// Zero when the frequency driver publishes nothing for this CPU.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline std::uint64_t cpu_cur_freq_khz(Ctx const&, int cpu) noexcept {
    return detail::read_cpu_cur_freq_khz(cpu);
}

// Zero when the frequency driver publishes nothing for this CPU.
template <::fixy::CtxFitsFileOpen Ctx>
[[nodiscard]] inline std::uint64_t cpu_max_freq_khz(Ctx const&, int cpu) noexcept {
    return detail::read_cpu_max_freq_khz(cpu);
}

// Returns -1 only when this task is allowed no CPU at all, which is
// unrecoverable and means the caller should refuse to start.
template <class Ctx>
    requires CtxFitsAffinityQuery<Ctx>
[[nodiscard]] inline int select_hot_cpu(Ctx const& ctx, const CoreSelector& sel,
                                        CpuSet const& exclude = CpuSet{}) noexcept {
    return detail::select_hot_cpu_of(allowed_cpus(ctx), sel, exclude);
}

// At most count companion CPUs for the supporting threads, from the CPUs
// that the calling thread may run on.  The CPUs on the same NUMA node as
// the given CPU are chosen first, and the given CPU itself is never among
// them.  Complexity: linear in the allowed CPUs, times the cost of one
// sysfs read.
template <class Ctx>
    requires CtxFitsAffinityQuery<Ctx>
[[nodiscard]] inline CpuSet select_warm_cpus(Ctx const& ctx, int hot_cpu, int count) noexcept {
    return detail::select_warm_cpus_of(allowed_cpus(ctx), hot_cpu, count);
}

}  // namespace crucible::warden
