// D001 reads the signature that a member pointer names through a deduction
// in a class that is not a template.  This file tries to give a throwing
// member function a noexcept signature: it writes an explicit
// specialization of the deduction that returns the box of a noexcept
// function.  An explicit specialization keeps the return type of the
// template, so no template declaration matches it.

#include <fixy/Collision.h>

struct callback_owner final {};

template <>
constexpr fixy::collision::detail::member_signature_box<void() noexcept>
fixy::collision::detail::member_pointer_parts_::pointee_of<void(), callback_owner>(void (callback_owner::*)()) noexcept;

int main() { return 0; }
