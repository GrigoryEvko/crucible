// The compile-time checks of foundation/Lifetime.h.

#include <foundation/Lifetime.h>

#include <foundation/core/Array.h>

namespace foundation::lifetime {

namespace detail::lifetime_self_test {

struct DerivesProof : ProofShape {};
struct HoldsReference {
    int& target;
};
// A FixedArray holds a C array of its elements, so the walk reads an array
// extent in each member below.
struct Plain {
    int value;
    ::foundation::core::FixedArray<double, 4> weight;
};
struct Nested {
    ::foundation::core::FixedArray<Plain, 2> inner;
    unsigned char tail;
};

static_assert(ImplicitLifetimeThroughout<int> && ImplicitLifetimeThroughout<const int>);
static_assert(ImplicitLifetimeThroughout<Plain> && ImplicitLifetimeThroughout<Nested>);
static_assert(ImplicitLifetimeThroughout<Plain[3]> && ImplicitLifetimeThroughout<int*>);
static_assert(!ImplicitLifetimeThroughout<ProofShape>, "a proof type is not implicit-lifetime");
static_assert(!ImplicitLifetimeThroughout<ProofShape[1]>,
              "an array of proofs is an implicit-lifetime type, and the walk must refuse it");
static_assert(std::is_implicit_lifetime_v<HoldsProof> && !ImplicitLifetimeThroughout<HoldsProof>,
              "an aggregate that holds a proof is implicit-lifetime, and the walk must refuse it");
static_assert(!ImplicitLifetimeThroughout<DerivesProof>, "a base class is a subobject");
// A union with a proof member.  The union is local to the function, so no
// namespace of foundation holds a union with a proof member, and the union
// audit of test/fixy/test_forgeable_proofs.cpp needs no exception.
[[nodiscard]] consteval bool refuses_a_union_member() {
    union ProofOrByte {
        unsigned char byte;
        ProofShape proof;
    };
    return !ImplicitLifetimeThroughout<ProofOrByte>;
}
static_assert(refuses_a_union_member(), "a union member is a subobject");
static_assert(!ImplicitLifetimeThroughout<HoldsReference>, "no lifetime start binds a reference member");

// A marked class is implicit-lifetime, so the marker alone refuses it, and
// the walk carries the refusal through a member, an array, a base and a
// union member.
struct[[= ::foundation::lifetime::no_start_over_bytes{}]] MarkedCount {
    unsigned long long count = 0;
};
struct HoldsMarked {
    MarkedCount count;
};
struct DerivesMarked : MarkedCount {};
static_assert(std::is_implicit_lifetime_v<MarkedCount> && !ImplicitLifetimeThroughout<MarkedCount>,
              "the marker refuses a class that is implicit-lifetime");
static_assert(!ImplicitLifetimeThroughout<HoldsMarked> && !ImplicitLifetimeThroughout<MarkedCount[2]>
                  && !ImplicitLifetimeThroughout<DerivesMarked>,
              "the marker refuses the class as a member, as an array element and as a base");
[[nodiscard]] consteval bool refuses_a_marked_union_member() {
    union MarkedOrByte {
        unsigned char byte;
        MarkedCount count;
    };
    return !ImplicitLifetimeThroughout<MarkedOrByte>;
}
static_assert(refuses_a_marked_union_member(), "the marker refuses the class as a union member");

// A closure with captures is implicit-lifetime, and its captures are its
// members, so the walk reads them.  A closure that captures a marked class
// is refused alone, as a member, as an array element and as a base, and a
// closure that captures a plain count is not.  A closure with no capture
// holds nothing.
inline constexpr auto carries_a_count = [count = 7ULL] { return count; };
inline constexpr auto carries_nothing = [] { return 7ULL; };
inline constexpr auto carries_a_marked_count = [count = MarkedCount{}] { return sizeof(count); };
using CountCarrier = std::remove_const_t<decltype(carries_a_count)>;
using MarkedCarrier = std::remove_const_t<decltype(carries_a_marked_count)>;
struct HoldsMarkedCarrier {
    MarkedCarrier carrier;
};
struct DerivesMarkedCarrier : MarkedCarrier {};
static_assert(std::is_implicit_lifetime_v<MarkedCarrier> && !ImplicitLifetimeThroughout<MarkedCarrier>,
              "a closure that captures a marked class is implicit-lifetime, and the walk must refuse it");
static_assert(!ImplicitLifetimeThroughout<HoldsMarkedCarrier> && !ImplicitLifetimeThroughout<MarkedCarrier[2]>
                  && !ImplicitLifetimeThroughout<DerivesMarkedCarrier>,
              "the walk refuses the closure as a member, as an array element and as a base");
static_assert(ImplicitLifetimeThroughout<CountCarrier>, "a closure that captures a plain count holds a plain count");
static_assert(ImplicitLifetimeThroughout<std::remove_const_t<decltype(carries_nothing)>>);

// The element of a start over bytes is const when the storage or T is const.
// The check file of foundation/core/Array.h holds the checks of the start.
static_assert(std::is_same_v<element_for_storage_t<const unsigned char, Plain>, const Plain>);
static_assert(std::is_same_v<element_for_storage_t<unsigned char, const Plain>, const Plain>);

// A sealed class keeps a trivial copy, a trivial destructor and its size,
// and the two byte routes refuse it.  A class that holds only a seal stays
// empty, and a class that holds a sealed member is sealed.
class SealedView {
    const int* target_ = nullptr;
    [[no_unique_address]] byte_seal seal_{};

public:
    constexpr SealedView() noexcept = default;
};
class SealedWitness {
    [[no_unique_address]] byte_seal seal_{};

public:
    constexpr SealedWitness() noexcept {}
    SealedWitness(const SealedWitness&) = delete;
    SealedWitness& operator=(const SealedWitness&) = delete;
};
struct HoldsSealedView {
    SealedView view;
};
static_assert(!std::is_trivially_copyable_v<byte_seal> && !ImplicitLifetimeThroughout<byte_seal>);
static_assert(!std::is_trivially_copyable_v<SealedView> && !ImplicitLifetimeThroughout<SealedView>,
              "std::bit_cast and the checked lifetime start refuse a sealed class");
static_assert(std::is_trivially_copy_constructible_v<SealedView> && std::is_trivially_move_constructible_v<SealedView>
                  && std::is_trivially_destructible_v<SealedView>,
              "a sealed class keeps the trivial copy that passes it in registers");
static_assert(sizeof(SealedView) == sizeof(const int*), "the seal adds no byte");
static_assert(std::is_empty_v<SealedWitness> && !std::is_trivially_copyable_v<SealedWitness>,
              "a pinned class that holds only a seal stays empty, and the trait stops calling it trivially copyable");
static_assert(!std::is_trivially_copyable_v<HoldsSealedView> && !ImplicitLifetimeThroughout<HoldsSealedView>,
              "a class that holds a sealed member is sealed through it");

}  // namespace detail::lifetime_self_test

}  // namespace foundation::lifetime
