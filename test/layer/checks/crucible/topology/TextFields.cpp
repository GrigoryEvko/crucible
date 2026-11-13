// The compile-time checks of crucible/topology/TextFields.h.

#include <crucible/topology/TextFields.h>

namespace crucible::topology::text {

static_assert(trim(" \t ab c \r\n") == ::fixy::TextView{"ab c"});
static_assert(trim(" \t\r\n").size() == 0);
static_assert(head("abc", 5) == ::fixy::TextView{"abc"});
static_assert(tail("abc", 1) == ::fixy::TextView{"bc"});
static_assert(tail("abc", 3).size() == 0);

consteval bool takes_each_line() {
    ::fixy::TextView rest = "one\ntwo\n\nthree";
    bool const is_first = take_line(rest) == ::fixy::TextView{"one"};
    bool const is_second = take_line(rest) == ::fixy::TextView{"two"};
    bool const is_empty_line = take_line(rest).size() == 0;
    bool const is_last = take_line(rest) == ::fixy::TextView{"three"};
    return is_first && is_second && is_empty_line && is_last && rest.size() == 0;
}
static_assert(takes_each_line());

// The first kind of separator that the line holds splits it, also when a
// later kind stands before it in the line.
static_assert(split_field("a = b: c", ":= ").key == ::fixy::TextView{"a = b"});
static_assert(split_field("a = b: c", ":= ").value == ::fixy::TextView{"c"});
static_assert(split_field("key\tvalue", ":\t").value == ::fixy::TextView{"value"});
static_assert(split_field("  key  ", ":").key == ::fixy::TextView{"key"});
static_assert(split_field("  key  ", ":").value.size() == 0);

static_assert(contains_ignoring_case("Ethernet controller", "ETHERNET"));
static_assert(!contains_ignoring_case("bridge", ""));
static_assert(!contains_ignoring_case("ab", "abc"));
static_assert(equal_ignoring_case("MLX5", "mlx5"));

static_assert(leading_digits("4096b 7p") == 4);
static_assert(leading_u64("100G").value_or(0) == 100);
static_assert(leading_u64("G100").is_none());
static_assert(leading_u64("18446744073709551616").is_none());
static_assert(first_word("  7p requeues 0") == ::fixy::TextView{"7p"});

}  // namespace crucible::topology::text
