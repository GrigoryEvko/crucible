// A file of the language: language.txt holds it in a language row with the
// layer low, and its own layer is high.  utils/assay/guard/legacy/check_plugin.py compiles
// language_user.cpp, which includes this header, in report mode with no plant
// and with each PLANT_LANGUAGE_ macro.  Each plant is an error.  The header
// with no plant has one finding and no error: the use of std::nothrow, which a
// row with `until` and `in` admits here.

#pragma once

#include <cstddef>
#include <meta>
#include <new>
#include <source_location>
#include <vector>

#if defined(PLANT_LANGUAGE_HIGH_HEADER)
#include <cstdint>
#endif

#if defined(PLANT_LANGUAGE_SYSTEM_HEADER)
#include <unistd.h>
#endif

#if defined(PLANT_LANGUAGE_DOOR_HEADER)
#include <sys/socket.h>
#endif

namespace fixy::language_test {

// The base holds the raw pointers and the arrays of its families, and the row
// with `in` admits std::nullptr_t in this file.
struct Plain {
    int* raw = nullptr;
    int values[2]{};
    std::nullptr_t none = nullptr;
};

inline int plain_sum(Plain const& plain) {
    int const first = plain.values[0];
    return first + plain.values[1];
}

// The row with `consteval-only` admits the members of the std::vector of
// std::meta::info that a query returns, which the compiler permits only in a
// constant evaluation.
consteval unsigned long member_count(std::meta::info type) {
    return std::meta::nonstatic_data_members_of(type, std::meta::access_context::current()).size();
}

// The row with `until` and `in` admits std::nothrow in this file.  The use is
// a finding of the kind replace_pending:std::nothrow, and no error.
inline void* allocate_byte() noexcept { return ::operator new(1, std::nothrow); }

#if defined(PLANT_LANGUAGE_STD_CALL)
inline void swap_values(int& left, int& right) { std::swap(left, right); }
#endif

#if defined(PLANT_LANGUAGE_RUNTIME_VECTOR)
consteval unsigned long item_count(std::vector<int> const& items) { return items.size(); }
#endif

#if defined(PLANT_LANGUAGE_MEMBER)
struct Holder {
    std::vector<int> items;
};
#endif

#if defined(PLANT_LANGUAGE_LOCAL)
inline unsigned long local_count() {
    std::vector<int> items{};
    return items.size();
}
#endif

#if defined(PLANT_LANGUAGE_PARAMETER)
inline unsigned line_of(std::source_location where) { return where.line(); }
#endif

#if defined(PLANT_LANGUAGE_ALIAS)
using Items = std::vector<int>;
#endif

#if defined(PLANT_LANGUAGE_TYPEDEF)
inline std::ptrdiff_t distance = 0;
#endif

#if defined(PLANT_LANGUAGE_BASE)
struct Derived : std::vector<int> {};
#endif

#if defined(PLANT_LANGUAGE_TEMPLATE)
template <class T>
struct Box {
    std::vector<T> items;
};
#endif

}  // namespace fixy::language_test
