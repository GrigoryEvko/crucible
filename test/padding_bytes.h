#pragma once

// Counts the padding bits of a type, as -ftrivial-auto-var-init=zero sees
// them.
//
// With -ftrivial-auto-var-init=zero, GCC writes zero to each padding byte of
// each automatic object that has an initializer, and of each object that a
// return statement makes.  For a fixed list of such objects, it writes one
// store for each padding hole of each element, with no loop.  A list of 256
// padded elements, returned through std::expected, gave a function of 934 KB
// of machine code.  utils/scripts/check-padded-lists.py rejects a new padded
// list, and a test of the element type calls expect_no_padding_byte, so that
// the type keeps each padding byte as a member.
//
// The count reads the layout by reflection, with the walk of
// utils/scripts/check-padded-lists.py.  The value bits of a type are the bit
// ranges that its scalar members, its bit-fields and its bases hold, at their
// offsets, and each other bit is padding.  A floating-point member has no
// padding bit, and long double holds 80 value bits.  __builtin_clear_padding
// writes the same bytes, but GCC refuses it for a type that is not trivially
// copyable, for example a type with an atomic or a refined member.  The walk
// reads private members too, because a wrapper keeps its value in a private
// member, so utils/scripts/unchecked-access-allowlist.txt names this file.
// The walk gives no reflection of a member to its caller.

#include "test_assert.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <meta>
#include <string_view>
#include <utility>
#include <vector>

namespace crucible::test {

namespace padding_detail {

// The bits [first, last) of an object.
using BitRange = std::pair<std::size_t, std::size_t>;

consteval std::size_t value_bit_count(std::meta::info type);

// Appends each bit range of an object of the type that holds a value, for an
// object that starts at first_bit.  Complexity: linear in the number of
// scalar subobjects, and an array of an element with no padding bit is one
// range.
consteval void collect_value_bits(std::meta::info type, std::size_t first_bit, std::vector<BitRange>& ranges) {
    type = std::meta::remove_cv(std::meta::dealias(type));
    if (std::meta::is_reference_type(type)) {
        ranges.push_back({first_bit, first_bit + sizeof(void*) * 8});
        return;
    }
    if (std::meta::is_array_type(type)) {
        const std::meta::info element = std::meta::remove_extent(type);
        const std::size_t count = std::meta::extent(type);
        const std::size_t element_bits = std::meta::size_of(element) * 8;
        if (value_bit_count(element) == element_bits) {
            ranges.push_back({first_bit, first_bit + count * element_bits});
            return;
        }
        for (std::size_t index = 0; index < count; ++index) {
            collect_value_bits(element, first_bit + index * element_bits, ranges);
        }
        return;
    }
    if (std::meta::is_class_type(type) || std::meta::is_union_type(type)) {
        const auto context = std::meta::access_context::unchecked();
        for (const std::meta::info base : std::meta::bases_of(type, context)) {
            const auto offset = static_cast<std::size_t>(std::meta::offset_of(base).bytes);
            collect_value_bits(std::meta::type_of(base), first_bit + offset * 8, ranges);
        }
        for (const std::meta::info member : std::meta::nonstatic_data_members_of(type, context)) {
            const auto offset = std::meta::offset_of(member);
            const std::size_t member_bit =
                first_bit + static_cast<std::size_t>(offset.bytes) * 8 + static_cast<std::size_t>(offset.bits);
            if (std::meta::is_bit_field(member)) {
                ranges.push_back({member_bit, member_bit + std::meta::bit_size_of(member)});
            } else {
                collect_value_bits(std::meta::type_of(member), member_bit, ranges);
            }
        }
        return;
    }
    const std::size_t scalar_bits = type == ^^long double ? 80 : std::meta::size_of(type) * 8;
    ranges.push_back({first_bit, first_bit + scalar_bits});
}

// The number of bits of an object of the type that hold a value.
// Complexity: n log n in the number of bit ranges.
consteval std::size_t value_bit_count(std::meta::info type) {
    std::vector<BitRange> ranges;
    collect_value_bits(type, 0, ranges);
    std::sort(ranges.begin(), ranges.end());
    std::size_t covered = 0;
    std::size_t reach = 0;
    for (const auto& [first, last] : ranges) {
        const std::size_t from = first > reach ? first : reach;
        if (last > from) {
            covered += last - from;
            reach = last;
        }
    }
    return covered;
}

}  // namespace padding_detail

// The number of padding bits of the type.  A polymorphic type holds a vtable
// pointer that no member names, so the count refuses one.
consteval std::size_t padding_bit_count(std::meta::info type) {
    type = std::meta::remove_cv(std::meta::dealias(type));
    if (std::meta::is_polymorphic_type(type)) {
        throw std::meta::exception(u8"a polymorphic type holds a vtable pointer, and the walk cannot count it",
                                   ^^padding_bit_count);
    }
    return std::meta::size_of(type) * 8 - padding_detail::value_bit_count(type);
}

// The type of a data member of a class, a private one too.  A test names a
// private class through the member that holds it.
consteval std::meta::info member_type(std::meta::info owner, std::string_view name) {
    for (const std::meta::info member :
         std::meta::nonstatic_data_members_of(owner, std::meta::access_context::unchecked())) {
        if (std::meta::has_identifier(member) && std::meta::identifier_of(member) == name) {
            return std::meta::type_of(member);
        }
    }
    throw std::meta::exception(u8"the class has no data member of that name", ^^member_type);
}

// Aborts the test when the type has a padding bit, and names the type.
template <std::meta::info Type>
void expect_no_padding_byte() noexcept {
    constexpr std::size_t padding_bits = padding_bit_count(Type);
    if (padding_bits != 0u) {
        std::fprintf(stderr,
                     "%s has %zu padding bits.  Make each padding byte a member, as "
                     "include/crucible/ledger/Verdict.h does with pad.\n",
                     std::define_static_string(std::meta::display_string_of(Type)), padding_bits);
    }
    assert(padding_bits == 0u);
}

}  // namespace crucible::test
