// The compile-time checks of foundation/Closure.h.

#include <foundation/Closure.h>

#include <foundation/ByteSeal.h>
#include <foundation/Linear.h>
#include <foundation/core/Array.h>
#include <foundation/core/Atomic.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Record.h>
#include <foundation/core/Ref.h>
#include <foundation/core/Region.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>

namespace foundation::closure {

namespace detail::closure_checks {

using ::foundation::core::Atomic;
using ::foundation::core::TextView;
using ::foundation::core::View;

// The policies of the doors of the census: a fork with sibling bodies, the
// job of a stage that the caller waits for, an owned thread, an arm of a
// match, and a walk.
inline constexpr DoorPolicy fork_policy{Crossing::ConcurrentThreads, QttGrade::One};
inline constexpr DoorPolicy stage_policy{Crossing::SoleThread, QttGrade::One};
inline constexpr DoorPolicy owned_thread_policy{Crossing::Escapes, QttGrade::One};
inline constexpr DoorPolicy arm_policy{Crossing::SameThread, QttGrade::AtMostOne};
inline constexpr DoorPolicy walk_policy{Crossing::SameThread, QttGrade::Omega};

// Callable classes with one field of each kind.  A class that a source
// writes shows its fields on each compiler.
struct CountsInto {
    int& count;
    void operator()() const noexcept { ++count; }
};
struct AddsToCell {
    Atomic<int>& cell;
    void operator()() const noexcept { static_cast<void>(cell.fetch_add_acq_rel(1)); }
};
struct ReadsLimit {
    int const& limit;
    [[nodiscard]] int operator()() const noexcept { return limit; }
};
struct OwnsCount {
    int count = 0;
    [[nodiscard]] int operator()() const noexcept { return count; }
};
struct HoldsView {
    View<int const> values;
    [[nodiscard]] std::size_t operator()() const noexcept { return values.size(); }
};
class[[= ::foundation::linear::linear{}]] Token {
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

public:
    constexpr Token() noexcept = default;
    constexpr Token(Token&&) noexcept = default;
    constexpr Token& operator=(Token&&) noexcept { return *this; }
    Token(Token const&) = delete("a token has one owner");
    Token& operator=(Token const&) = delete("a token has one owner");
    constexpr ~Token() = default;
};
struct OwnsToken {
    Token token{};
    void operator()() && noexcept {}
};
using BgContext = ::foundation::effects::ExecCtx<::foundation::effects::Bg, ::foundation::effects::Row<>>;
struct NamesScalar {
    int& scalar;
};
struct HoldsScalarName {
    NamesScalar named;
    void operator()() const noexcept {}
};
struct HoldsContext {
    BgContext const& ctx;
    void operator()() const noexcept {}
};
struct HoldsOnlyABitField {
    unsigned : 8;
    void operator()() const noexcept {}
};

// The facts: the kind and the target of each field.
consteval bool reads_the_facts() {
    const ClosureFacts counts = closure_facts(^^CountsInto);
    const ClosureFacts reads = closure_facts(^^ReadsLimit const&);
    const ClosureFacts owns = closure_facts(^^OwnsCount);
    const CaptureFact count = *counts.begin();
    const CaptureFact limit = *reads.begin();
    const CaptureFact owned = *owns.begin();
    return counts.is_readable() && counts.size() == 1 && count.kind == CaptureKind::BorrowMut
        && (count.target == ^^int)&&limit.kind == CaptureKind::BorrowConst
        && (limit.target == ^^int)&&owned.kind == CaptureKind::OwnValue && (owned.target == ^^int)&&!count.is_context
        && (*closure_facts(^^HoldsContext).begin()).is_context && !closure_facts(^^HoldsOnlyABitField).is_readable();
}
static_assert(reads_the_facts());

// The rules of a door on another thread.
static_assert(check_closure(^^CountsInto, fork_policy, "fork").reason() == Refusal::NotSynchronized);
static_assert(check_closure(^^CountsInto, stage_policy, "stage").is_admitted(),
              "the caller of a stage waits, so the body is the one user of the int");
static_assert(check_closure(^^AddsToCell, fork_policy, "fork").is_admitted());
static_assert(check_closure(^^ReadsLimit, fork_policy, "fork").is_admitted());
static_assert(check_closure(^^OwnsCount, fork_policy, "fork").is_admitted());
static_assert(check_closure(^^OwnsCount, owned_thread_policy, "owned thread").is_admitted());
static_assert(check_closure(^^HoldsView, fork_policy, "fork").is_admitted());

// Rule 8: beside the caller, each borrow is Synchronized, also a borrow to
// const, because the caller can write the object while the body reads it.
// A value is SendableBeside, so a value that refers to plain ints refuses.
inline constexpr DoorPolicy beside_policy{Crossing::BesideCaller, QttGrade::One};
static_assert(check_closure(^^ReadsLimit, beside_policy, "beside").reason() == Refusal::NotSynchronized);
static_assert(check_closure(^^CountsInto, beside_policy, "beside").reason() == Refusal::NotSynchronized);
static_assert(check_closure(^^AddsToCell, beside_policy, "beside").is_admitted());
static_assert(check_closure(^^OwnsCount, beside_policy, "beside").is_admitted());
static_assert(check_closure(^^HoldsView, beside_policy, "beside").reason() == Refusal::NotSendableBeside);
static_assert(check_closure(^^HoldsScalarName, beside_policy, "beside").reason() == Refusal::NotSendableBeside);
static_assert(check_closure(^^HoldsView, fork_policy, "fork").is_admitted(),
              "a fork joins before the caller runs on, so a view of const ints is Sendable there");
static_assert(check_closure(^^HoldsContext, beside_policy, "beside").reason() == Refusal::CapturedContext);

// Rule 9: a door that lends exclusive runs asks the facts of rule 8, although
// its caller waits.  A borrow of a plain object refuses, by value, by
// reference, by pointer and inside each class that can hold one, also a
// captured closure.  A value with no borrow and a borrow of an Atomic pass.
// The same classes pass a door of concurrent bodies that lends no run.
struct HoldsLimit {
    int limit = 0;
    // A closure that captures the pointer to its const object.
    [[nodiscard]] constexpr auto reads_this() const noexcept {
        return [this] { return limit; };
    }
};
using ReadsThroughThis = decltype(HoldsLimit{}.reads_this());
struct HeldBrand final {};
struct HoldsRef {
    ::foundation::core::Ref<int const, HeldBrand> value;
    [[nodiscard]] int operator()() const noexcept { return *value; }
};
struct HoldsOptionOfView {
    ::foundation::core::Option<View<int const>> values;
    void operator()() const noexcept {}
};
struct HoldsRecordOfView {
    ::foundation::core::Record<int, View<int const>> values;
    void operator()() const noexcept {}
};
struct HoldsArrayOfViews {
    ::foundation::core::FixedArray<View<int const>, 2> values;
    void operator()() const noexcept {}
};
struct HoldsCallable {
    HoldsView read;
    void operator()() const noexcept { static_cast<void>(read()); }
};
struct ReadsCallable {
    HoldsView const& read;
    void operator()() const noexcept { static_cast<void>(read()); }
};
struct ReadsOwner {
    ::foundation::core::FixedArray<int, 2> const& values;
    [[nodiscard]] std::size_t operator()() const noexcept { return values.view().size(); }
};
struct HoldsCellView {
    View<Atomic<int>> cells;
    [[nodiscard]] std::size_t operator()() const noexcept { return cells.size(); }
};
struct OwnsCounts {
    ::foundation::core::FixedArray<int, 2> counts{};
    ::foundation::core::Record<int, float> pair{};
    void operator()() const noexcept {}
};
inline constexpr DoorPolicy loop_policy{Crossing::ConcurrentThreads, QttGrade::Omega, true, true, RunLoan::Exclusive};
inline constexpr DoorPolicy copies_policy{Crossing::ConcurrentThreads, QttGrade::Omega, true};

template <class Body>
consteval Refusal refusal_at_loop() {
    return check_closure(^^Body, loop_policy, "loop").reason();
}
static_assert(refusal_at_loop<HoldsView>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsScalarName>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsRef>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsOptionOfView>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsRecordOfView>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsArrayOfViews>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<HoldsCallable>() == Refusal::NotSendableBeside);
static_assert(refusal_at_loop<ReadsCallable>() == Refusal::NotSynchronized);
static_assert(refusal_at_loop<ReadsLimit>() == Refusal::NotSynchronized);
static_assert(refusal_at_loop<ReadsThroughThis>()
              == (closure_facts(^^ReadsThroughThis).is_readable() ? Refusal::NotSynchronized : Refusal::Unreadable));
static_assert(refusal_at_loop<ReadsOwner>() == Refusal::NotSynchronized);
static_assert(refusal_at_loop<CountsInto>() == Refusal::NotSynchronized);
static_assert(refusal_at_loop<HoldsContext>() == Refusal::CapturedContext);
static_assert(refusal_at_loop<OwnsToken>() == Refusal::LinearGrade);
static_assert(refusal_at_loop<AddsToCell>() == Refusal::None);
static_assert(refusal_at_loop<HoldsCellView>() == Refusal::None);
static_assert(refusal_at_loop<OwnsCount>() == Refusal::None);
static_assert(refusal_at_loop<OwnsCounts>() == Refusal::None);
static_assert(check_closure(^^HoldsView, copies_policy, "copies").is_admitted());
static_assert(check_closure(^^HoldsCallable, copies_policy, "copies").is_admitted());
static_assert(check_closure(^^ReadsCallable, copies_policy, "copies").is_admitted());
static_assert(check_closure(^^ReadsOwner, copies_policy, "copies").is_admitted());
static_assert(check_closure(^^ReadsThroughThis, copies_policy, "copies").is_admitted()
              == closure_facts(^^ReadsThroughThis).is_readable());

// A door that asks no transfer fact, as the raw probe does, still refuses a
// captured context, a borrow that escapes and a linear body that it can call
// more than one time.
inline constexpr DoorPolicy raw_policy{Crossing::ConcurrentThreads, QttGrade::One, false, false};
inline constexpr DoorPolicy raw_escape_policy{Crossing::Escapes, QttGrade::One, false, false};
static_assert(check_closure(^^CountsInto, raw_policy, "raw").is_admitted());
static_assert(check_closure(^^HoldsContext, raw_policy, "raw").reason() == Refusal::CapturedContext);
static_assert(check_closure(^^ReadsLimit, raw_escape_policy, "raw").reason() == Refusal::Borrow);
static_assert(check_closure(^^HoldsView, owned_thread_policy, "owned thread").reason() == Refusal::Borrow);
static_assert(check_closure(^^HoldsScalarName, owned_thread_policy, "owned thread").reason() == Refusal::Borrow);

// The escape rule reads an owner of the base by its type arguments
// (holds_borrow_ of foundation/core/Choice.h).  A body that owns a Box of an
// int by value owns the object of the Box, so it escapes.  A body that owns a
// Box of a View holds a borrow, and the rule refuses it.
struct OwnsBox {
    ::foundation::core::Box<int> held;
    void operator()() && noexcept {}
};
struct OwnsBoxOfView {
    ::foundation::core::Box<View<int const>> held;
    void operator()() && noexcept {}
};
static_assert(check_closure(^^OwnsBox, owned_thread_policy, "owned thread").is_admitted());
static_assert(check_closure(^^OwnsBoxOfView, owned_thread_policy, "owned thread").reason() == Refusal::Borrow);
static_assert(check_closure(^^HoldsScalarName, stage_policy, "stage").reason() == Refusal::NotSendable);
static_assert(check_closure(^^HoldsScalarName, fork_policy, "fork").reason() == Refusal::NotSendable);
static_assert(check_closure(^^ReadsLimit, owned_thread_policy, "owned thread").reason() == Refusal::Borrow);
static_assert(check_closure(^^HoldsContext, stage_policy, "stage").reason() == Refusal::CapturedContext);
static_assert(check_closure(^^HoldsOnlyABitField, stage_policy, "stage").reason() == Refusal::Unreadable);

// The rules of a door on the calling thread: only the call grade.
static_assert(check_closure(^^CountsInto, arm_policy, "arm").is_admitted());
static_assert(check_closure(^^HoldsContext, walk_policy, "walk").is_admitted());
static_assert(check_closure(^^HoldsOnlyABitField, walk_policy, "walk").is_admitted());
static_assert(check_closure(^^OwnsToken, stage_policy, "stage").is_admitted());
static_assert(check_closure(^^OwnsToken, arm_policy, "arm").reason() == Refusal::LinearGrade);
static_assert(check_closure(^^OwnsToken, walk_policy, "walk").reason() == Refusal::LinearGrade);

// A lambda.  A lambda with no capture holds no field, and each door admits
// it.  A lambda with a capture of an int by copy is admitted at a stage
// exactly when the compiler shows its field, and refused as unreadable when
// it does not, so the pin holds on each compiler.
inline constexpr auto captures_nothing = [] { return 1; };
inline constexpr auto copies_a_seed = [seed = 3] { return seed; };
static_assert(check_closure(^^decltype(captures_nothing), fork_policy, "fork").is_admitted());
static_assert(check_closure(^^decltype(copies_a_seed), stage_policy, "stage").reason()
              == (closure_facts(^^decltype(copies_a_seed)).is_readable() ? Refusal::None : Refusal::Unreadable));

// The message names the door, the field, its type, the cause and the first
// part that refuses.
consteval bool starts_with(ClosureVerdict const& verdict, TextView expected) {
    std::size_t index = 0;
    for (char const letter : expected) {
        if (index >= verdict.size() || verdict.data()[index] != letter) return false;
        ++index;
    }
    return true;
}
consteval bool holds_text(ClosureVerdict const& verdict, TextView expected) {
    std::size_t size = 0;
    for (char const letter : expected) {
        static_cast<void>(letter);
        ++size;
    }
    for (std::size_t start = 0; start + size <= verdict.size(); ++start) {
        std::size_t index = 0;
        bool is_match = true;
        for (char const letter : expected) {
            if (verdict.data()[start + index] != letter) {
                is_match = false;
                break;
            }
            ++index;
        }
        if (is_match) return true;
    }
    return false;
}
static_assert(starts_with(check_closure(^^CountsInto, fork_policy, "fork"),
                          "fork: the body captures `count` (field count, type int&).  The bodies or copies"));
static_assert(holds_text(check_closure(^^CountsInto, fork_policy, "fork"), "The first part that refuses it: int"));
static_assert(holds_text(check_closure(^^HoldsView, owned_thread_policy, "owned thread"), "a borrow dangles"));
static_assert(starts_with(check_closure(^^OwnsToken, arm_policy, "arm"), "arm: the body owns a linear value"));
static_assert(holds_text(check_closure(^^OwnsToken, arm_policy, "arm"), "This door can skip the call"));
static_assert(check_closure(^^AddsToCell, fork_policy, "fork").size() == 0, "a pass builds no message");

// The message of a door that lends exclusive runs names the capture and the
// door of the inputs.
static_assert(starts_with(check_closure(^^HoldsView, loop_policy, "loop"),
                          "loop: the body captures `values` (field values, type "));
static_assert(holds_text(check_closure(^^HoldsView, loop_policy, "loop"),
                         "The value can refer to a run that a second body of this door writes"));
static_assert(holds_text(check_closure(^^HoldsView, loop_policy, "loop"), "Give the run to the door in reads()"));
static_assert(starts_with(check_closure(^^ReadsLimit, loop_policy, "loop"),
                          "loop: the body captures `limit` (field limit, type const int&).  The object of this borrow "
                          "can lie in a run that a second body of this door writes"));
static_assert(holds_text(check_closure(^^ReadsLimit, loop_policy, "loop"), "Give the run to the door in reads()"));
static_assert(holds_text(check_closure(^^ReadsCallable, loop_policy, "loop"), "The first part that refuses it: "));

}  // namespace detail::closure_checks

}  // namespace foundation::closure
