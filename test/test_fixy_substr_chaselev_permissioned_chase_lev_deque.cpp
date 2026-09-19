// Restating the header's own identity assertions in a translation unit is what
// puts them under the project's warnings-as-errors flags.
//
// The two sides of this deque have deliberately different permission shapes.
// The owner side is linear: owner() consumes the owner Permission and hands
// back a bare handle, which is sound because push and pop at the bottom are
// single-owner operations. The thief side is fractional: thief() takes no
// Permission and returns an optional, since the pool can be held exclusively.
// The thief pool's root is minted inside the deque's constructor, so a caller
// mints only the owner Permission.

#include <crucible/fixy/Substr.h>

#include <crucible/concurrent/ChaseLevDeque.h>
#include <crucible/concurrent/PermissionedChaseLevDeque.h>
#include <crucible/permissions/_Permission.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>

namespace fsubstr = ::crucible::fixy::substr;
namespace cc = ::crucible::concurrent;
namespace cs = ::crucible::safety;

namespace probes {

// A tag local to this translation unit, so the generic specialization produces
// a fresh Whole, Owner and Thief triple rather than sharing one with another
// test.
struct V047TestUserTag {};

}  // namespace probes

using TestDeque = fsubstr::chaselev::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>;

static_assert(std::is_same_v<TestDeque, cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>>,
              "fixy::substr::chaselev::PermissionedChaseLevDeque must alias the substrate.");

static_assert(fsubstr::chaselev::DequeValue<int>);
static_assert(fsubstr::chaselev::DequeValue<int> == cc::DequeValue<int>);
// The non-trivial destructor is the whole point of this type: it must fail the
// concept through both spellings.
struct NonDequeValue {
    ~NonDequeValue() {}
};
static_assert(!fsubstr::chaselev::DequeValue<NonDequeValue>);
static_assert(!cc::DequeValue<NonDequeValue>);

static_assert(std::is_same_v<fsubstr::chaselev::deque_tag::Whole<probes::V047TestUserTag>,
                             cc::deque_tag::Whole<probes::V047TestUserTag>>);
static_assert(std::is_same_v<fsubstr::chaselev::deque_tag::Owner<probes::V047TestUserTag>,
                             cc::deque_tag::Owner<probes::V047TestUserTag>>);
static_assert(std::is_same_v<fsubstr::chaselev::deque_tag::Thief<probes::V047TestUserTag>,
                             cc::deque_tag::Thief<probes::V047TestUserTag>>);

static_assert(std::is_same_v<typename TestDeque::value_type, int>);
static_assert(std::is_same_v<typename TestDeque::user_tag, probes::V047TestUserTag>);
static_assert(
    std::is_same_v<typename TestDeque::whole_tag, fsubstr::chaselev::deque_tag::Whole<probes::V047TestUserTag>>);
static_assert(
    std::is_same_v<typename TestDeque::owner_tag, fsubstr::chaselev::deque_tag::Owner<probes::V047TestUserTag>>);
static_assert(
    std::is_same_v<typename TestDeque::thief_tag, fsubstr::chaselev::deque_tag::Thief<probes::V047TestUserTag>>);

static_assert(TestDeque::deque_capacity == 64);

static_assert(fsubstr::chaselev::ChaseLevSessionSurface<TestDeque>);

// The tag tree carries an empty permission row, so the context-free root mint
// is the right one here. Nothing is transferred over the wire, and the
// fractional thief state lives in the deque rather than in the tokens.
static void test_runtime_construct_and_owner() {
    TestDeque deque{};
    auto owner_perm = cs::mint_permission_root<TestDeque::owner_tag>();
    auto owner = deque.owner(std::move(owner_perm));
    // The handle existing at all is the evidence that the linear Permission
    // was consumed.
    if (owner.size_approx() != 0) std::abort();
    if (!owner.empty_approx()) std::abort();
}

// Push and pop at the bottom make the owner's own view a stack, so the order
// coming back out is the reverse of the order going in.
static void test_runtime_owner_push_pop_lifo() {
    TestDeque deque{};
    auto owner = deque.owner(cs::mint_permission_root<TestDeque::owner_tag>());

    constexpr int N = 16;  // comfortably under the capacity of 64
    for (int i = 0; i < N; ++i) {
        if (!owner.try_push(i * 100 + 7)) std::abort();
    }
    for (int i = N - 1; i >= 0; --i) {
        std::optional<int> r = owner.try_pop();
        if (!r) std::abort();
        if (*r != i * 100 + 7) std::abort();
    }
    if (owner.try_pop()) std::abort();
}

// Stealing happens at the top, so a thief sees the same items oldest first,
// the opposite order from the owner.
static void test_runtime_owner_push_thief_steal_fifo() {
    TestDeque deque{};
    auto owner = deque.owner(cs::mint_permission_root<TestDeque::owner_tag>());

    constexpr int N = 8;
    for (int i = 0; i < N; ++i) {
        if (!owner.try_push(i * 1000 + 13)) std::abort();
    }

    auto thief_opt = deque.thief();
    if (!thief_opt) std::abort();
    auto thief = std::move(*thief_opt);

    // A lost compare-exchange returns an empty optional even when the deque
    // holds items, so a single failed steal proves nothing and the retry is
    // part of the contract rather than a workaround.
    for (int i = 0; i < N; ++i) {
        std::optional<int> r;
        for (int retry = 0; retry < 16 && !r; ++retry) {
            r = thief.try_steal();
        }
        if (!r) std::abort();
        if (*r != i * 1000 + 13) std::abort();
    }
}

static void test_runtime_capacity_constant() {
    static_assert(TestDeque::deque_capacity == 64);
    volatile std::size_t cap = TestDeque::deque_capacity;
    if (cap != 64) std::abort();
}

static void test_runtime_substrate_identity() {
    static_assert(std::is_same_v<TestDeque, cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>>);
    TestDeque deque{};
    cc::PermissionedChaseLevDeque<int, 64, probes::V047TestUserTag>* via_sub = &deque;
    // The implicit pointer conversion compiles only if the alias names the
    // substrate type rather than a distinct wrapper around it.
    TestDeque* via_fixy = via_sub;
    if (via_fixy != via_sub) std::abort();
}

static void test_runtime_protocol_aliases_unchanged() {
    using FixyOwner = fsubstr::chaselev::OwnerProto<int>;
    using FixyThief = fsubstr::chaselev::ThiefProto<int, TestDeque::thief_tag>;
    using SubsOwner = ::crucible::safety::proto::chaselev_session::OwnerProto<int>;
    using SubsThief = ::crucible::safety::proto::chaselev_session::ThiefProto<int, TestDeque::thief_tag>;
    static_assert(std::is_same_v<FixyOwner, SubsOwner>);
    static_assert(std::is_same_v<FixyThief, SubsThief>);
}

int main() {
    test_runtime_construct_and_owner();
    test_runtime_owner_push_pop_lifo();
    test_runtime_owner_push_thief_steal_fifo();
    test_runtime_capacity_constant();
    test_runtime_substrate_identity();
    test_runtime_protocol_aliases_unchanged();
    std::printf("test_fixy_substr_chaselev_permissioned_chase_lev_deque: "
                "6/6 runtime witnesses passed\n");
    return 0;
}
