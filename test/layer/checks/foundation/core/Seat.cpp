// The compile-time checks of foundation/core/Seat.h.

#include <foundation/core/Seat.h>

#include <cstdint>
#include <type_traits>

namespace foundation::core {

namespace seat_check {

// A linear payload, as a session handle is.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Handle final {
    std::uint64_t word = 0;

    explicit Handle(std::uint64_t value) noexcept : word{value} {}
    Handle(Handle&&) noexcept = default;
    Handle& operator=(Handle&& source) noexcept {
        word = source.word;
        return *this;
    }
    Handle(Handle const&) = delete("a linear payload of the check");
    Handle& operator=(Handle const&) = delete("a linear payload of the check");
    ~Handle() = default;

    [[nodiscard]] Handle next() && noexcept { return Handle{word + 1}; }
    [[nodiscard]] std::uint64_t finish() && noexcept { return word; }
};

// A step of a Handle, a step that gives another type, and a step that can
// throw.
struct NextHandle final {
    Handle operator()(Handle&& handle) noexcept { return static_cast<Handle&&>(handle).next(); }
};
struct OtherType final {
    std::uint64_t operator()(Handle&& handle) noexcept { return static_cast<Handle&&>(handle).finish(); }
};
struct ThrowingStep final {
    Handle operator()(Handle&& handle) { return static_cast<Handle&&>(handle).next(); }
};

}  // namespace seat_check

// A Seat is full from its mint: it has no default constructor, no copy and no
// assignment, and it moves (THE RULES).
static_assert(!std::is_default_constructible_v<Seat<std::uint64_t>>);
static_assert(!std::is_copy_constructible_v<Seat<std::uint64_t>>);
static_assert(!std::is_copy_assignable_v<Seat<std::uint64_t>>);
static_assert(!std::is_move_assignable_v<Seat<std::uint64_t>>);
static_assert(std::is_nothrow_move_constructible_v<Seat<seat_check::Handle>>);

// A Seat of a linear payload is linear, and a Seat of a plain payload is not.
static_assert(::foundation::linear::is_linear_v<Seat<seat_check::Handle>>);
static_assert(!::foundation::linear::is_linear_v<Seat<std::uint64_t>>);

// A Seat is the storage of its payload and nothing more.
static_assert(sizeof(Seat<seat_check::Handle>) == sizeof(seat_check::Handle));
static_assert(alignof(Seat<seat_check::Handle>) == alignof(seat_check::Handle));

// A step gives the payload type, and it does not throw.
static_assert(StepOf<seat_check::NextHandle, seat_check::Handle>);
static_assert(!StepOf<seat_check::OtherType, seat_check::Handle>);
static_assert(!StepOf<seat_check::ThrowingStep, seat_check::Handle>);

// A payload is an object type that is not an array, not const and not
// volatile, whose move and end do not throw.
static_assert(SeatPayload<seat_check::Handle>);
static_assert(SeatPayload<std::uint64_t>);
static_assert(!SeatPayload<std::uint64_t const>);
static_assert(!SeatPayload<std::uint64_t[2]>);
static_assert(!SeatPayload<std::uint64_t&>);

}  // namespace foundation::core
