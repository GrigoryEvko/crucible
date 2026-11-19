// The compile-time checks of foundation/ThreadLocalRef.h.

#include <foundation/ThreadLocalRef.h>

#include <foundation/Quarantine.h>

namespace foundation {

namespace detail::thread_local_ref_self_test {

struct CounterTag {};
struct AccumulatorTag {};
struct OtherTag {};

using IntCounter = ThreadLocalRef<CounterTag, int>;
using IntAccumulator = ThreadLocalRef<AccumulatorTag, int>;
using DoubleOther = ThreadLocalRef<OtherTag, double>;

// The handle is an empty tag: it holds nothing, so it costs one byte and
// collapses to none as a member under [[no_unique_address]].
static_assert(sizeof(IntCounter) == 1 && sizeof(DoubleOther) == 1);
static_assert(std::is_empty_v<IntCounter> && std::is_trivially_copyable_v<IntCounter>);

inline constexpr IntCounter c_default{};
inline constexpr IntCounter c_copy = c_default;

static_assert(std::is_same_v<IntCounter::tag_type, CounterTag>);
static_assert(std::is_same_v<IntCounter::value_type, int>);
static_assert(std::is_same_v<DoubleOther::tag_type, OtherTag>);
static_assert(std::is_same_v<DoubleOther::value_type, double>);

static_assert(!std::is_same_v<IntCounter, IntAccumulator>);
static_assert(!std::is_same_v<IntCounter, DoubleOther>);

static_assert(std::is_copy_constructible_v<IntCounter> && std::is_copy_assignable_v<IntCounter>);
static_assert(std::is_move_constructible_v<IntCounter> && std::is_move_assignable_v<IntCounter>);

static_assert(IntCounter::value_type_name() == "int");
static_assert(DoubleOther::value_type_name() == "double");

// A value fits a cell of thread_local_cell_bytes bytes, and it is trivially
// copyable, trivially destructible and trivially default constructible.
template <typename T>
concept NamesACell = requires { typename ThreadLocalRef<CounterTag, T>; };
struct EightBytes {
    unsigned int low;
    unsigned int high;
};
struct SixteenBytes {
    unsigned long long low;
    unsigned long long high;
};
struct CountsCopies {
    int value = 0;
    CountsCopies() = default;
    CountsCopies(CountsCopies const& other) noexcept : value{other.value + 1} {}
};
static_assert(NamesACell<int> && NamesACell<double> && NamesACell<unsigned long long> && NamesACell<EightBytes>);
static_assert(!NamesACell<SixteenBytes>);
static_assert(!NamesACell<CountsCopies>);

// An enumeration, a bool and a floating-point value are zero bytes at T{}.
enum class Shade : unsigned char {
    dark,
    light
};
struct Scalars {
    Shade shade;
    bool is_set;
    unsigned short count;
    float weight;
};
static_assert(NamesACell<Scalars> && NamesACell<Shade> && NamesACell<bool> && NamesACell<float>);
static_assert(sizeof(detail::ThreadLocalTable) == thread_local_cell_count * thread_local_cell_bytes);

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the walk of a value of a cell reads raw pointers and pointers to members")

// A cell starts as zero bytes, so a value whose T{} is not zero bytes has no
// cell: a pointer to a member at each depth of the walk.  A null pointer to a
// data member is all ones on the Itanium ABI.
struct Holder {
    int field;
};
struct HoldsMemberPointer {
    int Holder::* member;
};
struct BaseHoldsMemberPointer : HoldsMemberPointer {};
struct NestsMemberPointer {
    HoldsMemberPointer inner;
};
struct NestsMemberPointerArray {
    int Holder::* members[1];
};
static_assert(!NamesACell<int Holder::*>);
static_assert(!NamesACell<HoldsMemberPointer>);
static_assert(!NamesACell<BaseHoldsMemberPointer>);
static_assert(!NamesACell<NestsMemberPointer>);
static_assert(!NamesACell<NestsMemberPointerArray>);
static_assert(!NamesACell<HoldsMemberPointer const>);

// The null pointer is zero bytes, so a raw pointer has a cell.
struct HoldsPointer {
    Holder const* holder;
};
static_assert(NamesACell<HoldsPointer> && NamesACell<Holder const*>);

CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

}  // namespace detail::thread_local_ref_self_test

}  // namespace foundation
