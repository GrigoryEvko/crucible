// Tests of foundation/core/Algorithms.h: equal, find, the scans with a
// predicate, the lower bound of a key and the bytes of an object, over the
// View of a FixedArray, and the text forms over a TextView.

#include <foundation/Quarantine.h>
#include <foundation/core/Algorithms.h>
#include <foundation/core/Array.h>

#include "../test_assert.h"
#include "philox_stream.h"

#include <cstddef>
#include <cstdint>

#include <algorithm>
#include <cstring>
#include <string_view>

namespace {

using ::foundation::core::FixedArray;
using ::foundation::core::TextView;

struct Record {
    std::uint64_t key = 0;
    std::uint32_t count = 0;
    std::uint32_t flags = 0;
};

enum class Shade : std::uint8_t {
    dark,
    light,
    bright
};

void test_equal_of_views() {
    FixedArray<std::uint32_t, 5> const first{1, 2, 3, 4, 5};
    FixedArray<std::uint32_t, 5> const second{1, 2, 3, 4, 5};
    FixedArray<std::uint32_t, 5> const third{1, 2, 3, 4, 6};
    assert(::foundation::core::equal(first.view(), second.view()));
    assert(!::foundation::core::equal(first.view(), third.view()));
    assert(!::foundation::core::equal(first.view(), first.view().window(0, 4).expect("four elements fit")));
    FixedArray<Shade, 3> const shades{Shade::dark, Shade::bright, Shade::light};
    FixedArray<Shade, 3> const same_shades{Shade::dark, Shade::bright, Shade::light};
    assert(::foundation::core::equal(shades.view(), same_shades.view()));
}

void test_find_and_scans() {
    FixedArray<int, 6> const numbers{4, 8, 15, 16, 23, 42};
    assert(::foundation::core::find(numbers.view(), 16).expect("16 is in the run") == 3);
    assert(::foundation::core::find(numbers.view(), 5).is_none());
    auto const is_even = [](int value) noexcept { return value % 2 == 0; };
    auto const is_odd = [](int value) noexcept { return value % 2 != 0; };
    assert(::foundation::core::find_if(numbers.view(), is_odd).expect("15 is odd") == 2);
    assert(::foundation::core::count_if(numbers.view(), is_even) == 4);
    assert(::foundation::core::any_of(numbers.view(), is_odd));
    assert(!::foundation::core::all_of(numbers.view(), is_even));
    auto const below_fifty = [](int value) noexcept { return value < 50; };
    assert(::foundation::core::all_of(numbers.view(), below_fifty));
}

void test_lower_bound_by_key() {
    FixedArray<Record, 5> const records{Record{2, 0, 0}, Record{4, 0, 0}, Record{4, 1, 0}, Record{9, 0, 0},
                                        Record{12, 0, 0}};
    auto const key_of = [](Record const& record) noexcept { return record.key; };
    assert(::foundation::core::lower_bound_by_key(records.view(), key_of, std::uint64_t{4}) == 1);
    assert(::foundation::core::lower_bound_by_key(records.view(), key_of, std::uint64_t{5}) == 3);
    assert(::foundation::core::lower_bound_by_key(records.view(), key_of, std::uint64_t{1}) == 0);
    assert(::foundation::core::lower_bound_by_key(records.view(), key_of, std::uint64_t{13}) == 5);
}

// The bytes of a plain record are its bytes in memory, in order.
void test_bytes_of_a_record() {
    Record const record{0x0102030405060708u, 0x0A0B0C0Du, 0x11121314u};
    auto const image = ::foundation::core::bytes(record);
    static_assert(decltype(image)::extent == sizeof(Record));
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: memcpy is the model of the bytes of an object")
    unsigned char copied[sizeof(Record)];
    std::memcpy(copied, &record, sizeof(Record));
    std::size_t position = 0;
    for (std::byte const byte : image) {
        assert(static_cast<unsigned char>(byte) == copied[position]);
        ++position;
    }
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(position == sizeof(Record));
}

void test_text_search() {
    TextView const path{"/sys/devices/system/node/node3/cpulist"};
    assert(::foundation::core::find(path, '/').expect("the path holds a slash") == 0);
    assert(::foundation::core::find(path, TextView{"node3"}).expect("the path names the node") == 25);
    assert(::foundation::core::find(path, TextView{"node9"}).is_none());
    assert(::foundation::core::starts_with(path, TextView{"/sys/"}));
    assert(::foundation::core::ends_with(path, TextView{"cpulist"}));
    assert(!::foundation::core::ends_with(path, TextView{"cpumap"}));
    assert(::foundation::core::equal(path.window(1, 3).expect("the window fits"), TextView{"sys"}));
}

// The letters of the property.  The ORACLE region holds the same text as a
// std::string_view.
constexpr TextView letters{"abcabbacbbcabacccababcbbaacbcabcbacabbbcaacbabcabbcaaabcbcbacbbacaabccbabacbcaabbcab"
                           "cabacbbbacaccbabcabcbaabcbcacbbaacbcbacabcaabbcbacbacbcabcaabcbcabbcaacbabcbacbaacb"};

// The property: find, starts_with, ends_with and equal of random windows of
// the letters give the answers of std::string_view.
void test_text_operations_agree_with_std() {
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::string_view is the model of the text operations")
    std::string_view const model_letters =
        "abcabbacbbcabacccababcbbaacbcabcbacabbbcaacbabcabbcaaabcbcbacbbacaabccbabacbcaabbcab"
        "cabacbbbacaccbabcabcbaabcbcacbbaacbcbacabcaabbcbacbacbcabcaabcbcabbcaacbabcbacbaacb";
    std::size_t const model_size = model_letters.size();
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    assert(letters.size() == model_size);
    ::foundation::test::PhiloxStream stream{0x7E57F1ED0000C003u};
    for (int trial = 0; trial < 4096; ++trial) {
        std::size_t const offset = stream.below(letters.size() + 1);
        std::size_t const size = stream.below(letters.size() - offset + 1);
        std::size_t const needle_offset = stream.below(letters.size() - 4);
        std::size_t const needle_size = stream.below(5);
        TextView const text = letters.window(offset, size).expect("the window fits the letters");
        TextView const needle = letters.window(needle_offset, needle_size).expect("the needle fits the letters");
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: std::string_view is the model of the text operations")
        std::string_view const model = model_letters.substr(offset, size);
        std::string_view const needle_model = model_letters.substr(needle_offset, needle_size);
        std::size_t const model_char = model.find('b');
        std::size_t const model_text = model.find(needle_model);
        bool const model_starts = model.starts_with(needle_model);
        bool const model_ends = model.ends_with(needle_model);
        bool const model_equal = model == needle_model;
        bool const is_model_char_found = model_char != std::string_view::npos;
        bool const is_model_text_found = model_text != std::string_view::npos;
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        auto const found_char = ::foundation::core::find(text, 'b');
        assert(found_char.is_some() == is_model_char_found);
        for (std::size_t const position : found_char) {
            assert(position == model_char);
        }
        auto const found_text = ::foundation::core::find(text, needle);
        assert(found_text.is_some() == is_model_text_found);
        for (std::size_t const position : found_text) {
            assert(position == model_text);
        }
        assert(::foundation::core::starts_with(text, needle) == model_starts);
        assert(::foundation::core::ends_with(text, needle) == model_ends);
        assert(::foundation::core::equal(text, needle) == model_equal);
    }
}

// The property: find, find_if and count_if of random runs of integers give
// the answers of the std algorithms.
void test_view_scans_agree_with_std() {
    ::foundation::test::PhiloxStream stream{0x5CA45CA40000D004u};
    FixedArray<std::uint32_t, 64> run;
    auto const is_small = [](std::uint32_t value) noexcept { return value < 2; };
    for (int trial = 0; trial < 2048; ++trial) {
        for (std::uint32_t& value : run) {
            value = static_cast<std::uint32_t>(stream.below(8));
        }
        std::size_t const size = stream.below(65);
        auto const window = run.view().window(0, size).expect("the size is at most 64");
        CRUCIBLE_I_KNOW_WHAT_IM_DOING("ORACLE: the std algorithms are the model of the scans")
        std::uint32_t model[64];
        std::size_t position = 0;
        for (std::uint32_t const value : run) {
            model[position] = value;
            ++position;
        }
        std::uint32_t const* const model_first = model;
        std::uint32_t const* const model_end = model_first + size;
        std::uint32_t const* const model_three = std::find(model_first, model_end, 3u);
        std::uint32_t const* const model_small = std::find_if(model_first, model_end, is_small);
        auto const model_count = static_cast<std::size_t>(std::count_if(model_first, model_end, is_small));
        bool const is_three_found = model_three != model_end;
        bool const is_small_found = model_small != model_end;
        auto const three_position = static_cast<std::size_t>(model_three - model_first);
        auto const small_position = static_cast<std::size_t>(model_small - model_first);
        CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
        auto const found_three = ::foundation::core::find(window, 3u);
        assert(found_three.is_some() == is_three_found);
        for (std::size_t const found : found_three) {
            assert(found == three_position);
        }
        auto const found_small = ::foundation::core::find_if(window, is_small);
        assert(found_small.is_some() == is_small_found);
        for (std::size_t const found : found_small) {
            assert(found == small_position);
        }
        assert(::foundation::core::count_if(window, is_small) == model_count);
        assert(::foundation::core::any_of(window, is_small) == is_small_found);
    }
}

}  // namespace

int main() {
    test_equal_of_views();
    test_find_and_scans();
    test_lower_bound_by_key();
    test_bytes_of_a_record();
    test_text_search();
    test_text_operations_agree_with_std();
    test_view_scans_agree_with_std();
    crucible::test::pass("test_core_algorithms: all tests passed\n");
    return 0;
}
