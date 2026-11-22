// Negative-compile test. This file must fail to compile.
//
// A door that runs its body past the end of the frame of the call refuses a
// body that holds a borrow, because the borrow dangles after the frame ends
// (foundation/Closure.h).  A View of the caller is such a borrow.

#include <foundation/Closure.h>

#include <foundation/core/Array.h>
#include <foundation/core/Region.h>

namespace {

inline constexpr ::foundation::closure::DoorPolicy escaping_policy{::foundation::closure::Crossing::Escapes,
                                                                   ::foundation::closure::QttGrade::One};

template <class Body>
void run_later(Body const& body) noexcept {
    constexpr ::foundation::closure::ClosureVerdict verdict =
        ::foundation::closure::check_closure(^^Body, escaping_policy, "run_later");
    static_assert(verdict.is_admitted(), verdict);
    static_cast<void>(body());
}

struct CountsValues {
    ::foundation::core::View<int const> values;
    [[nodiscard]] std::size_t operator()() const noexcept { return values.size(); }
};

}  // namespace

int main() {
    ::foundation::core::FixedArray<int, 4> const values{1, 2, 3, 4};
    run_later(CountsValues{values.view()});
    return 0;
}
