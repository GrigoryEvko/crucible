// Tests of the root doors (the keys of a body of fixy/os/ThreadTasks.h, the
// borrow of a root door of foundation/core/Frame.h): run_joined and run_beside
// mint the keys that a body names, a body that owns its holder by value opens
// its frames with them, and the walk refuses each shape that reaches an owner
// of the brand through a borrow.

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/concurrent/PermissionedChannel.h>
#include <fixy/concurrent/SpscEnds.h>
#include <fixy/concurrent/SpscRing.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

namespace fc = ::foundation::core;
namespace ft = ::fixy::thread;

struct HolderBrand final {};
struct OtherBrand final {};

struct Holder {
    fc::FixedArray<int, 4, HolderBrand> array{};
};

struct Other {
    fc::FixedArray<int, 4, OtherBrand> array{};
};

// A joined body that owns its holder by value gets a key of the brand from the
// door, and opens a frame of the holder with it.
void test_joined_body_owns_its_holder(::fixy::BgLoadCtx const& ctx) {
    Holder const holder{};
    auto body = [moved = holder](fc::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(1).expect("the array holds the element");
            *element = 7;
        });
        return moved.array.at(fc::index<1>());
    };
    fc::Result<fc::Record<int>, ::foundation::os::Errno> const joined =
        ft::run_joined(ctx, ft::ThreadOptions{}, std::move(body));
    CRUCIBLE_FATAL_INVARIANT(joined.is_ok());
    CRUCIBLE_FATAL_INVARIANT(
        joined.match([](fc::Record<int> const& values) noexcept { return values.at(fc::index<0>()); },
                     [](::foundation::os::Errno const&) noexcept { return 0; })
        == 7);
}

// A body takes the context and keys of two brands, and two bodies run at the
// same time, each with its own keys.
void test_context_and_two_brands(::fixy::BgLoadCtx const& ctx) {
    Holder const holder{};
    Other const other{};
    auto first = [moved = holder, kept = other](::fixy::BgLoadCtx const&, fc::LoanKey<HolderBrand>& key,
                                                fc::LoanKey<OtherBrand>& other_key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(0).expect("the array holds the element");
            *element = 3;
        });
        kept.array.with_mut(other_key, [](auto run) noexcept {
            auto element = run.get_mut(0).expect("the array holds the element");
            *element = 4;
        });
        return moved.array.at(fc::index<0>()) + kept.array.at(fc::index<0>());
    };
    auto second = [moved = holder](fc::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(0).expect("the array holds the element");
            *element = 5;
        });
        return moved.array.at(fc::index<0>());
    };
    auto const joined = ft::run_joined(ctx, ft::ThreadOptions{}, std::move(first), std::move(second));
    CRUCIBLE_FATAL_INVARIANT(joined.is_ok());
}

// True when run_joined takes a body of the type that the caller names as an
// lvalue.
template <typename Body>
concept JoinsAnLvalueBody =
    requires(::fixy::BgLoadCtx const& ctx, Body& body) { ft::run_joined(ctx, ft::ThreadOptions{}, body); };

// Two copies of one keyed body are two objects, each with its own holder, so
// each thread opens a frame of its own holder with its own key.  One object
// given two times as an lvalue does not compile: neg_root_door_same_body_twice.
void test_two_copies_of_one_keyed_body(::fixy::BgLoadCtx const& ctx) {
    Holder const holder{};
    auto body = [moved = holder](fc::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(3).expect("the array holds the element");
            *element += 1;
        });
        return moved.array.at(fc::index<3>());
    };
    static_assert(!JoinsAnLvalueBody<decltype(body)>);
    auto const joined = ft::run_joined(ctx, ft::ThreadOptions{}, auto{body}, auto{body});
    CRUCIBLE_FATAL_INVARIANT(joined.match(
        [](fc::Record<int, int> const& values) noexcept {
            return values.at(fc::index<0>()) == 1 && values.at(fc::index<1>()) == 1;
        },
        [](::foundation::os::Errno const&) noexcept { return false; }));
}

// run_beside mints the keys of its body.  The continuation gets no key.
void test_beside_body_owns_its_holder(::fixy::BgLoadCtx const& ctx) {
    Holder const holder{};
    auto body = [moved = holder](fc::LoanKey<HolderBrand>& key) mutable noexcept {
        moved.array.with_mut(key, [](auto run) noexcept {
            auto element = run.get_mut(2).expect("the array holds the element");
            *element = 9;
        });
    };
    CRUCIBLE_FATAL_INVARIANT(ft::run_beside(ctx, ft::ThreadOptions{}, std::move(body), [] noexcept {}).is_ok());
}

// A body with no key reads a holder of the caller through a borrow: the door
// mints no key, so the walk does not apply.
void test_keyless_body_reads(::fixy::BgLoadCtx const& ctx) {
    Holder const holder{};
    auto read = [&holder]() noexcept { return holder.array.at(fc::index<0>()); };
    CRUCIBLE_FATAL_INVARIANT(ft::run_joined(ctx, ft::ThreadOptions{}, std::move(read)).is_ok());
}

// The brands of the trailing keys, after no parameter and after one.
using KeyedBody = decltype([](fc::LoanKey<HolderBrand>&, fc::LoanKey<OtherBrand>&) noexcept {});
using ContextBody = decltype([](::fixy::BgLoadCtx const&, fc::LoanKey<HolderBrand>&) noexcept {});
using GenericBody = decltype([](auto&) noexcept {});
static_assert(std::is_same_v<fc::root_key_brands_t<KeyedBody, 0>, fc::KeyBrands<HolderBrand, OtherBrand>>);
static_assert(std::is_same_v<fc::root_key_brands_t<ContextBody, 1>, fc::KeyBrands<HolderBrand>>);
static_assert(!fc::NamesRootKeys<ContextBody, 0>);
static_assert(!fc::NamesRootKeys<GenericBody, 0>);

// The walk, on a shape that the closure check of a door refuses first: a value
// that holds a Ref of a holder.  The walk refuses it too.  An SPSC ring takes a
// trivially copyable payload (RingValue), and no borrow of the base is one, so
// no ring of base types carries a borrow of a holder.
template <class HolderRefType>
struct HolderRef {
    HolderRefType holder;
};
using HeldRef = HolderRef<fc::Ref<Holder, HolderBrand>>;
static_assert(!fc::BorrowsNoKeyOwner<HeldRef, HolderBrand>);
static_assert(fc::BorrowsNoKeyOwner<HeldRef, OtherBrand>);

static_assert(!::fixy::concurrent::RingValue<HeldRef>);

// A holder by value is admitted, and so is the consumer end of a channel of
// holders: the channel states synchronized{}, so the walk reads its payload as
// held (the borrow of a root door).
static_assert(fc::BorrowsNoKeyOwner<Holder, HolderBrand>);
struct ChannelTag final {};
struct ChannelBrand final {};
using HolderWire =
    ::fixy::concurrent::PermissionedChannel<::fixy::concurrent::ChannelForm::wire, Holder, 8, ChannelTag, ChannelBrand>;
static_assert(fc::BorrowsNoKeyOwner<HolderWire::ConsumerEnd, HolderBrand>);
static_assert(fc::BorrowsNoKeyOwner<HolderRef<fc::Ref<HolderWire::ConsumerEnd, ChannelBrand>>, HolderBrand>);
// The consumer handle of a frame holds the same end, on a named scope brand.
static_assert(fc::BorrowsNoKeyOwner<HolderWire::consumer_handle<ChannelBrand>, HolderBrand>);

}  // namespace

int main() {
    ::fixy::BgLoadCtx const ctx{::foundation::effects::testing::bg()};
    test_joined_body_owns_its_holder(ctx);
    test_context_and_two_brands(ctx);
    test_two_copies_of_one_keyed_body(ctx);
    test_beside_body_owns_its_holder(ctx);
    test_keyless_body_reads(ctx);
    return 0;
}
