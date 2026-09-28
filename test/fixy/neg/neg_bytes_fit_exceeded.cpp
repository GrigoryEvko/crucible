// ensure_bytes_fit compares a computed byte count with a budget.  Nine
// 8-byte fields are 72 bytes, and a cache line holds 64.

#include <fixy/Checked.h>

#include <cstdint>

consteval int nine_words_in_one_cache_line() {
    fixy::ensure_bytes_fit<
        64, fixy::safe_struct_bytes<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
                                    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>>();
    return 0;
}

int main() { return nine_words_in_one_cache_line(); }
