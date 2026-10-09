#include <fixy/os/NumaCounters.h>

#include <foundation/os/Errno.h>
#include <foundation/os/File.h>
#include <foundation/os/PathText.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::numa {

namespace {

// The bytes that a read of a numastat file takes.  The file of a node has
// six lines of at most 36 characters.
constexpr std::size_t counter_file_bytes = 512;

// The path of the numastat file of a node: the prefix, at most three digits
// for a node of at most 253, and the suffix.  The longest path has 41
// characters, so each append of a concrete node fits.
constexpr std::size_t counter_path_capacity = 64;
constexpr CText counter_path_prefix{"/sys/devices/system/node/node"};
constexpr CText counter_path_suffix{"/numastat"};

// The refusal of a step for an error of the kernel.
[[nodiscard]] constexpr NumaQueryError kernel_error(NumaQueryStep step, ::foundation::os::Errno error) noexcept {
    return NumaQueryError{step, static_cast<int>(std::to_underlying(error))};
}

// The names of the six counters, and the field of each one.
struct CounterName {
    char const* name;
    std::size_t length;
    std::uint64_t NodeAllocationCounters::* field;
};

constexpr CounterName counter_names[] = {
    {"numa_hit", 8, &NodeAllocationCounters::hit},
    {"numa_miss", 9, &NodeAllocationCounters::miss},
    {"numa_foreign", 12, &NodeAllocationCounters::foreign},
    {"interleave_hit", 14, &NodeAllocationCounters::interleave_hit},
    {"local_node", 10, &NodeAllocationCounters::local},
    {"other_node", 10, &NodeAllocationCounters::other},
};

constexpr std::size_t counter_count = sizeof(counter_names) / sizeof(counter_names[0]);

// True for a character that divides the name of a counter from its count.
[[nodiscard]] constexpr bool is_blank(char character) noexcept { return character == ' ' || character == '\t'; }

// True when the length characters from word are the name of the counter.
[[nodiscard]] constexpr bool names_counter(char const* word, std::size_t length, CounterName const& counter) noexcept {
    if (length != counter.length) return false;
    for (std::size_t index = 0; index < length; ++index) {
        if (word[index] != counter.name[index]) return false;
    }
    return true;
}

// The error of a text that is not a numastat file.
[[nodiscard]] constexpr NumaQueryError format_error() noexcept {
    return NumaQueryError{NumaQueryStep::CounterFormat, 0};
}

}  // namespace

Result<NodeAllocationCounters, NumaQueryError> parse_allocation_counters(TextView text) noexcept {
    char const* const characters = ::foundation::core::detail::text_first_(text);
    std::size_t const size = text.size();
    NodeAllocationCounters counters{};
    bool seen[counter_count]{};
    std::size_t position = 0;
    while (position < size) {
        std::size_t const word_start = position;
        while (position < size && !is_blank(characters[position]) && characters[position] != '\n')
            ++position;
        std::size_t const word_length = position - word_start;
        while (position < size && is_blank(characters[position]))
            ++position;
        std::uint64_t count = 0;
        std::size_t digits = 0;
        while (position < size && characters[position] >= '0' && characters[position] <= '9') {
            std::uint64_t const digit = static_cast<std::uint64_t>(characters[position] - '0');
            if (__builtin_mul_overflow(count, std::uint64_t{10}, &count)
                || __builtin_add_overflow(count, digit, &count)) {
                return err(format_error());
            }
            ++digits;
            ++position;
        }
        while (position < size && is_blank(characters[position]))
            ++position;
        bool const ends_line = position == size || characters[position] == '\n';
        if (word_length == 0 && digits == 0 && ends_line) {
            ++position;
            continue;
        }
        if (word_length == 0 || digits == 0 || !ends_line) {
            return err(format_error());
        }
        ++position;
        for (std::size_t index = 0; index < counter_count; ++index) {
            if (!names_counter(characters + word_start, word_length, counter_names[index])) continue;
            if (seen[index]) return err(format_error());
            seen[index] = true;
            counters.*(counter_names[index].field) = count;
        }
    }
    for (bool const is_seen : seen) {
        if (!is_seen) return err(format_error());
    }
    return counters;
}

namespace detail {

// The path goes into a PathText of the base, and the file door of the base
// reads the file.  So the read builds no path type of the library, and it
// throws nothing.
Result<NodeAllocationCounters, NumaQueryError> read_allocation_counters_(NumaNodeId node) noexcept {
    ::foundation::os::PathText<counter_path_capacity> path{};
    for (::foundation::os::Errno const error : path.append(counter_path_prefix).err()) {
        return err(kernel_error(NumaQueryStep::CounterPath, error));
    }
    for (::foundation::os::Errno const error : path.append_decimal(std::to_underlying(node)).err()) {
        return err(kernel_error(NumaQueryStep::CounterPath, error));
    }
    for (::foundation::os::Errno const error : path.append(counter_path_suffix).err()) {
        return err(kernel_error(NumaQueryStep::CounterPath, error));
    }
    FixedText<counter_file_bytes> text{};
    for (::foundation::os::Errno const error : ::foundation::os::read_file(path.c_text(), text).err()) {
        return err(kernel_error(NumaQueryStep::CounterRead, error));
    }
    return parse_allocation_counters(text.view());
}

}  // namespace detail

}  // namespace fixy::numa
