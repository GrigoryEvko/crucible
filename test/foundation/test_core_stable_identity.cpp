// Tests of foundation/core/StableIdentity.h: the image of a function and of a
// list of types is the list of their stable names, each after its count of
// bytes in four little-endian bytes.

#include <foundation/core/StableIdentity.h>

#include <foundation/Platform.h>

#include <cstddef>
#include <cstdint>

namespace test_stable_identity {

struct FirstPart {};
struct SecondPart {};

int probe_function(FirstPart first, SecondPart second) noexcept;
int other_function(FirstPart first, SecondPart second) noexcept;

}  // namespace test_stable_identity

namespace {

namespace fc = ::foundation::core;
using fc::TextView;
using fc::View;
using test_stable_identity::FirstPart;
using test_stable_identity::other_function;
using test_stable_identity::probe_function;
using test_stable_identity::SecondPart;

// True when the image holds the fields in order and nothing after them.
// Complexity: linear in the size of the image.
template <std::size_t Count, std::size_t FieldCount>
[[nodiscard]] constexpr bool image_holds_fields(View<std::byte const, Count> image,
                                                fc::FixedArray<TextView, FieldCount> const& fields) noexcept {
    auto cursor = image.begin();
    std::size_t remaining = Count;
    for (TextView const field : fields) {
        if (remaining < 4 + field.size()) return false;
        std::size_t count = 0;
        for (std::size_t shift = 0; shift < 32; shift += 8) {
            count |= static_cast<std::size_t>(static_cast<std::uint8_t>(*cursor)) << shift;
            ++cursor;
        }
        if (count != field.size()) return false;
        for (char const letter : field) {
            if (static_cast<std::uint8_t>(*cursor) != static_cast<unsigned char>(letter)) return false;
            ++cursor;
        }
        remaining -= 4 + field.size();
    }
    return remaining == 0;
}

// True when the text ends with the suffix.
[[nodiscard]] constexpr bool text_ends_with(TextView text, TextView suffix) noexcept {
    if (suffix.size() > text.size()) return false;
    std::size_t at = 0;
    std::size_t const start = text.size() - suffix.size();
    auto cursor = suffix.begin();
    for (char const letter : text) {
        if (at >= start) {
            if (letter != *cursor) return false;
            ++cursor;
        }
        ++at;
    }
    return true;
}

// The image holds the name of the function, the name of its type and the
// name of each type, in order.
void test_fields() {
    static_assert(image_holds_fields(fc::stable_identity_image<&probe_function, FirstPart, SecondPart>(),
                                     fc::FixedArray<TextView, 4>{
                                         fc::stable_function_name_text<&probe_function>(),
                                         fc::stable_function_type_name_text<&probe_function>(),
                                         fc::stable_name_text<FirstPart>(),
                                         fc::stable_name_text<SecondPart>(),
                                     }));
    static_assert(text_ends_with(fc::stable_function_name_text<&probe_function>(), "probe_function"));
    static_assert(text_ends_with(fc::stable_name_text<SecondPart>(), "SecondPart"));
    static_assert(fc::stable_function_type_name_text<&probe_function>()
                  == fc::stable_function_type_name_text<&other_function>());
}

template <std::size_t FirstCount, std::size_t SecondCount>
[[nodiscard]] constexpr bool images_differ(View<std::byte const, FirstCount> first,
                                           View<std::byte const, SecondCount> second) noexcept {
    if constexpr (FirstCount != SecondCount) {
        return true;
    } else {
        auto cursor = second.begin();
        for (std::byte const byte : first) {
            if (byte != *cursor) return true;
            ++cursor;
        }
        return false;
    }
}

// Two functions of one type, two orders of the types and two counts of the
// types give two images.  One list gives one image.
void test_images_differ() {
    static_assert(images_differ(fc::stable_identity_image<&probe_function, FirstPart, SecondPart>(),
                                fc::stable_identity_image<&other_function, FirstPart, SecondPart>()));
    static_assert(images_differ(fc::stable_identity_image<&probe_function, FirstPart, SecondPart>(),
                                fc::stable_identity_image<&probe_function, SecondPart, FirstPart>()));
    static_assert(images_differ(fc::stable_identity_image<&probe_function, FirstPart>(),
                                fc::stable_identity_image<&probe_function, FirstPart, FirstPart>()));
    static_assert(!images_differ(fc::stable_identity_image<&probe_function, FirstPart>(),
                                 fc::stable_identity_image<&probe_function, FirstPart>()));
}

// A run-time read of the image gives its bytes: three counts and the three
// names.
void test_run_time_read() {
    View<std::byte const> const image{fc::stable_identity_image<&probe_function, FirstPart>()};
    std::size_t const count = 12 + fc::stable_function_name_text<&probe_function>().size()
                            + fc::stable_function_type_name_text<&probe_function>().size()
                            + fc::stable_name_text<FirstPart>().size();
    CRUCIBLE_FATAL_INVARIANT(image.size() == count);
    fc::FixedArray<TextView, 3> const fields{
        fc::stable_function_name_text<&probe_function>(),
        fc::stable_function_type_name_text<&probe_function>(),
        fc::stable_name_text<FirstPart>(),
    };
    bool const holds_fields = image_holds_fields(fc::stable_identity_image<&probe_function, FirstPart>(), fields);
    CRUCIBLE_FATAL_INVARIANT(holds_fields);
}

}  // namespace

int test_stable_identity::probe_function(FirstPart /*first*/, SecondPart /*second*/) noexcept { return 1; }
int test_stable_identity::other_function(FirstPart /*first*/, SecondPart /*second*/) noexcept { return 2; }

int main() {
    test_fields();
    test_images_differ();
    test_run_time_read();
    return 0;
}
