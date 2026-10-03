#include <fixy/os/NumaCounters.h>

namespace fixy::numa {

namespace {

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

// The whole path goes into one buffer, and the path takes it in one
// constructor, so the build uses no path operator of the library.
std::filesystem::path allocation_counter_path_(NumaNodeId node) noexcept {
    constexpr char prefix[] = "/sys/devices/system/node/node";
    constexpr char suffix[] = "/numastat";
    // The prefix, at most three digits for a node of at most 253, the
    // suffix and the terminating zero.
    char text[sizeof(prefix) + 3 + sizeof(suffix)]{};
    std::size_t length = 0;
    for (std::size_t index = 0; index + 1 < sizeof(prefix); ++index) {
        text[length++] = prefix[index];
    }
    unsigned const number = std::to_underlying(node);
    unsigned const hundreds = number / 100;
    unsigned const tens = (number / 10) % 10;
    if (hundreds != 0) text[length++] = static_cast<char>('0' + hundreds);
    if (hundreds != 0 || tens != 0) text[length++] = static_cast<char>('0' + tens);
    text[length++] = static_cast<char>('0' + (number % 10));
    for (std::size_t index = 0; index + 1 < sizeof(suffix); ++index) {
        text[length++] = suffix[index];
    }
    return std::filesystem::path{text};
}

}  // namespace detail

}  // namespace fixy::numa
