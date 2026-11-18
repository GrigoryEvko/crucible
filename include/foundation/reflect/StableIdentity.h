#pragma once

// The stable names of foundation/reflect/Hash.h as characters of static
// storage, for a header of the language that names no std::string.
//
//   stable_function_name_chars<Key>()        the printed name of a function
//   stable_function_type_name_chars<Key>()   the stable name of its type
//   stable_name_chars<Type>()                the stable name of a type
//
// Key is the reflection of a function key, ^^function_key<&f>.  Each text is
// the text that the 64-bit id of the same name hashes: stable_function_name_id,
// stable_function_id and stable_type_id.  Each is read in a function of the
// namespace foundation::reflect, as the ids of foundation/reflect/Hash.h read
// it, so each translation unit reads it in one scope.  A function or a type
// with no stable identity stops the constant evaluation with the refusal of
// that header.  foundation/core/StableIdentity.h makes the image of the texts.

#include <foundation/reflect/EnumName.h>
#include <foundation/reflect/Hash.h>

#include <cstddef>
#include <meta>

namespace foundation::reflect {

namespace detail {

// The characters of a text, with static storage.  The text has a type that
// depends on a template parameter, so only a unit that names the text
// instantiates the static string (foundation/reflect/Anchor.h).  A stable
// name holds no zero character, so the count is the count of characters
// before the first zero.
template <std::meta::info Owner, class Text>
[[nodiscard]] consteval NameChars static_name_chars_(Text const& text) {
    char const* const first = std::define_static_string(text);
    std::size_t size = 0;
    while (first[size] != '\0') {
        ++size;
    }
    return NameChars{first, size};
}

}  // namespace detail

template <std::meta::info Key>
[[nodiscard]] consteval detail::NameChars stable_function_name_chars() {
    // The id refuses a function with no stable identity, with the refusal
    // of foundation/reflect/Hash.h.
    static_cast<void>(stable_function_name_id(Key));
    return detail::static_name_chars_<Key>(std::meta::display_string_of(detail::pointer_of_key(Key)));
}

template <std::meta::info Key>
[[nodiscard]] consteval detail::NameChars stable_function_type_name_chars() {
    return detail::static_name_chars_<Key>(
        stable_name(std::meta::remove_pointer(std::meta::type_of(detail::pointer_of_key(Key)))));
}

template <std::meta::info Type>
[[nodiscard]] consteval detail::NameChars stable_name_chars() {
    return detail::static_name_chars_<Type>(stable_name(Type));
}

}  // namespace foundation::reflect
