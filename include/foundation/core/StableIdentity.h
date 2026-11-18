#pragma once

// The stable identity image of a function and of a list of types.  A key
// that crosses a process boundary hashes these bytes with a keyed hash, in
// place of the 64-bit ids of foundation/reflect/Hash.h.  A peer that chooses
// its input can find two names of one 64-bit id, but two lists of names
// never give one image.
//
//   stable_function_name_text<FnPtr>()        the printed name of a function
//   stable_function_type_name_text<FnPtr>()   the stable name of its type
//   stable_name_text<T>()                     the stable name of a type
//   stable_identity_image<FnPtr, Types...>()  the image of the three kinds
//
// The image is a list of fields.  Each field is its count of bytes as four
// little-endian bytes, and then its bytes.  The fields are the printed name
// of the function, the stable name of its type, and the stable name of each
// type of the list, in order.  The count in front of each field makes the
// image a function of the list of names: two lists give one image only when
// they hold the same names in the same order.
//
// Each text is the text that the 64-bit id of the same name hashes
// (foundation/reflect/StableIdentity.h).  So the image holds the bound of
// foundation/reflect/Hash.h: one toolchain gives one image for one entity,
// and two toolchains can give two.
//
// Complexity: linear in the total length of the names, at compile time.  The
// texts and the image have static storage, and a run-time read is a read of
// their bytes.

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/reflect/StableIdentity.h>

#include <cstddef>
#include <cstdint>

namespace foundation::core {

template <auto FnPtr>
[[nodiscard]] constexpr TextView stable_function_name_text() noexcept {
    constexpr ::foundation::reflect::detail::NameChars name =
        ::foundation::reflect::stable_function_name_chars<^^::foundation::reflect::function_key<FnPtr>>();
    return detail::c_text_over_(name.first, name.size).view();
}

template <auto FnPtr>
[[nodiscard]] constexpr TextView stable_function_type_name_text() noexcept {
    constexpr ::foundation::reflect::detail::NameChars name =
        ::foundation::reflect::stable_function_type_name_chars<^^::foundation::reflect::function_key<FnPtr>>();
    return detail::c_text_over_(name.first, name.size).view();
}

template <class T>
[[nodiscard]] constexpr TextView stable_name_text() noexcept {
    constexpr ::foundation::reflect::detail::NameChars name = ::foundation::reflect::stable_name_chars<^^T>();
    return detail::c_text_over_(name.first, name.size).view();
}

namespace detail {

// The image of the function FnPtr and of the list Types.
template <auto FnPtr, class... Types>
struct StableIdentity final {
    static constexpr std::size_t field_count = 2 + sizeof...(Types);

    static constexpr FixedArray<TextView, field_count> fields{
        stable_function_name_text<FnPtr>(),
        stable_function_type_name_text<FnPtr>(),
        stable_name_text<Types>()...,
    };

    static constexpr std::size_t count = [] consteval {
        std::size_t total = 0;
        for (TextView const field : fields) {
            total += 4 + field.size();
        }
        return total;
    }();

    static constexpr FixedArray<std::byte, count> image = [] consteval {
        FixedArray<std::byte, count> out{};
        auto cursor = out.begin();
        for (TextView const field : fields) {
            // The compiler cannot hold a name of 4 GiB, so each count fits
            // its four bytes.
            for (std::size_t shift = 0; shift < 32; shift += 8) {
                *cursor = static_cast<std::byte>(static_cast<std::uint8_t>(field.size() >> shift));
                ++cursor;
            }
            for (char const letter : field) {
                *cursor = static_cast<std::byte>(static_cast<unsigned char>(letter));
                ++cursor;
            }
        }
        return out;
    }();
};

}  // namespace detail

// The bytes of the image of the function FnPtr and of the list Types.
template <auto FnPtr, class... Types>
[[nodiscard]] constexpr View<std::byte const, detail::StableIdentity<FnPtr, Types...>::count>
stable_identity_image() noexcept {
    return detail::StableIdentity<FnPtr, Types...>::image.view();
}

}  // namespace foundation::core
