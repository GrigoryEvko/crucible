// The step of a Seat lends its payload to a function for the call, and puts the
// result of the function in the same storage.  A function that captures an
// Option of a View by reference could keep a borrow of the payload there, and
// the caller could read it after the step ended the payload.  step refuses a
// function that can store a borrow outside the step (LentArmStoresNoBorrow of
// foundation/core/Choice.h).

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Seat.h>

int main() {
    auto seat = ::foundation::core::mint_seat(1);
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    seat.step([&kept](int&& payload) noexcept { return payload + (kept.is_some() ? 1 : 0); });
    return 0;
}
