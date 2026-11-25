#pragma once

// The marks of a loan key and of a keyed owner, and the trait that names a loan
// key.  The borrow types refuse a key as their element: Ref and Box of
// foundation/core/Ref.h, View and ViewMut of foundation/core/Region.h, and
// RefMut of foundation/core/Frame.h.  Frame.h defines LoanKey (the loan key, the
// dispatch brand), and the class states loan_key_class_.  The header includes
// only the read of an annotation and <meta>, so a borrow type pays no include of
// Frame.h for its refusal, and the parts of a class (foundation/core/TypeParts.h)
// read the marks with no include of it.

#include <foundation/reflect/Annotation.h>

#include <meta>

namespace foundation::core {

// The mark of a loan key and of a keyed owner.  The kind loan-key-static of
// the rule process-wide-state (utils/tools/quarantine/declaration.cpp, the
// loan-key statics) refuses an object of static or thread storage duration
// that reaches a class with the mark, because a body with no capture reaches
// such an object and the gate does not see it.  A class template whose
// instances are keyed only for some arguments writes loan_key_mark_when.
struct loan_key_mark final {};
struct no_loan_key_mark final {};

template <bool IsKeyed>
inline constexpr auto loan_key_mark_when = no_loan_key_mark{};

template <>
inline constexpr auto loan_key_mark_when<true> = loan_key_mark{};

namespace detail {

// The annotation of the class LoanKey alone.  A keyed owner states loan_key_mark,
// and not this annotation.
struct loan_key_class_ final {};

// True for a LoanKey of each brand, with its cv-qualifiers and its reference
// removed.  A borrow type refuses such an element, so no body keeps a borrow of
// a key past the frame that lent it.  A raw pointer to a key stays open in the
// base, and the quarantine of the product code refuses each raw pointer.  A
// class that is not complete is no key.
[[nodiscard]] consteval bool is_loan_key_type_(std::meta::info type) {
    std::meta::info const bare = std::meta::dealias(std::meta::remove_cvref(type));
    if (!std::meta::is_class_type(bare) || !std::meta::is_complete_type(bare)) {
        return false;
    }
    return ::foundation::reflect::carries_annotation(bare, ^^loan_key_class_);
}

}  // namespace detail

}  // namespace foundation::core
