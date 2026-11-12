#pragma once

// The open mark of the base: a template that each file can specialize.
//
// C++ has no orphan rule, so each unit can specialize each template that it
// can name.  A specialization of a trait in a different file changes what
// each gate that reads the trait admits.  The rule specialization-home of the
// quarantine plugin keeps each specialization of a template of the tree in
// the home of the template, the file that defines it
// (utils/tools/quarantine/specialization.cpp).  A template that the design
// extends from each file, such as a trait that a type states beside its own
// definition, carries one P3394 annotation on its primary declaration, with
// its reason:
//
//     template <class T>
//     struct [[= ::foundation::open_extension{"a type states its trait beside its own definition"}]] trait;
//
// Reflection reads the annotation through std::meta::annotations_of, and the
// plugin reads the same annotation in the tree of GCC.  No macro writes the
// annotation, so a build with no plugin reads it too.
//
// GCC merges the annotation of a second declaration into the template, and
// an annotation keeps no location.  So a file can add the mark to a template
// of a different file.  The ledger utils/ledgers/specialization-home.txt
// holds each marked template with its reason, and the rule refuses a mark
// that no row admits.

#include <cstddef>
#include <meta>

namespace foundation {

// The number of bytes of the reason of a mark, with its final zero.
inline constexpr std::size_t open_extension_reason_size = 256;

// The annotation of a template that each file can specialize.  The reason is
// a text of one or more bytes, and fewer than open_extension_reason_size.  A
// constant evaluation makes each mark, so a longer text is a compile error.
struct open_extension final {
    char reason[open_extension_reason_size]{};

    template <std::size_t Size>
        requires(Size > 1 && Size <= open_extension_reason_size)
    consteval open_extension(char const (&text)[Size]) noexcept {
        for (std::size_t index = 0; index < Size; ++index) {
            reason[index] = text[index];
        }
    }
};

// True when the entity carries the open mark.  An instance of a class
// template or of a variable template reads the annotations of its template.
// The read goes through a constant array, so it calls no member of the
// container that the query gives.  Complexity: linear in the number of
// annotations of the entity.
[[nodiscard]] consteval bool is_open_extension(std::meta::info entity) {
    const std::meta::info found =
        std::meta::reflect_constant_array(std::meta::annotations_of_with_type(entity, ^^open_extension));
    const std::meta::info found_type = std::meta::type_of(found);
    return std::meta::is_array_type(found_type) && std::meta::extent(found_type) != 0;
}

}  // namespace foundation
