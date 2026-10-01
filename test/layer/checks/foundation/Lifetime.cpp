// The compile-time checks of foundation/Lifetime.h.

#include <foundation/Lifetime.h>

namespace foundation::lifetime {

namespace detail::lifetime_self_test {

struct DerivesProof : ProofShape {};
struct HoldsReference {
    int& target;
};
struct Plain {
    int value;
    double weight[4];
};
struct Nested {
    Plain inner[2];
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

// A closure with captures is implicit-lifetime, and the walk cannot read
// what it holds, so the walk refuses it alone, as a member, as an array
// element and as a base.  A closure with no capture holds nothing.
inline constexpr auto carries_a_count = [count = 7ULL] { return count; };
inline constexpr auto carries_nothing = [] { return 7ULL; };
using CountCarrier = std::remove_const_t<decltype(carries_a_count)>;
struct HoldsCountCarrier {
    CountCarrier carrier;
};
struct DerivesCountCarrier : CountCarrier {};
static_assert(std::is_implicit_lifetime_v<CountCarrier> && !ImplicitLifetimeThroughout<CountCarrier>,
              "a closure with captures is implicit-lifetime, and the walk must refuse it");
static_assert(!ImplicitLifetimeThroughout<HoldsCountCarrier> && !ImplicitLifetimeThroughout<CountCarrier[2]>
                  && !ImplicitLifetimeThroughout<DerivesCountCarrier>,
              "the walk refuses the closure as a member, as an array element and as a base");
static_assert(ImplicitLifetimeThroughout<std::remove_const_t<decltype(carries_nothing)>>);

// The element of the span is const when the storage or T is const, and
// volatile storage is refused.
template <typename Storage, typename T>
concept can_start_as = requires(Storage* storage) {
    { start_as_array<T>(storage, 1) } -> std::same_as<std::span<element_for_storage_t<Storage, T>>>;
};
static_assert(can_start_as<unsigned char, Plain> && can_start_as<void, Plain>);
static_assert(can_start_as<const unsigned char, Plain> && can_start_as<unsigned char, const Plain>);
static_assert(std::is_same_v<element_for_storage_t<const unsigned char, Plain>, const Plain>);
static_assert(std::is_same_v<element_for_storage_t<unsigned char, const Plain>, const Plain>);
static_assert(!can_start_as<volatile unsigned char, Plain> && !can_start_as<const volatile void, Plain>,
              "volatile storage is refused, and a caller adds volatile to data()");
static_assert(!can_start_as<unsigned char, HoldsProof> && !can_start_as<const void, ProofShape[1]>,
              "the checked start refuses a proof subobject through every storage qualifier");

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
