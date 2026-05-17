// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-IS-SessionHandle fixture #3: fixy::is::IsSessionHandle rejects
// an unrelated plain type when used as a `requires` clause gate.
//
// Violation: a function template `requires fixy::is::IsSessionHandle<T>`
// rejects any T that is not a recognized session-handle shape.
// Routing through the fixy::is alias must reject identically.
//
// Expected diagnostic: "constraints not satisfied" / "IsSessionHandle".

#include <crucible/fixy/Is.h>

namespace fis = crucible::fixy::is;

namespace neg_fixy_is_session_handle_rejects_plain {

struct TypeIsSessionHandleRejectsPlain {
    int x = 0;
};

template <typename T>
    requires fis::IsSessionHandle<T>
[[nodiscard]] constexpr int gate(T const&) noexcept { return 1; }

}  // namespace neg_fixy_is_session_handle_rejects_plain

int main() {
    namespace tags = neg_fixy_is_session_handle_rejects_plain;

    tags::TypeIsSessionHandleRejectsPlain plain{};
    // Should FAIL: plain is not a session handle.
    [[maybe_unused]] auto bad = tags::gate(plain);
    return 0;
}
