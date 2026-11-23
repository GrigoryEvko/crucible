// Tests of CallRef of foundation/core/CallRef.h: a call through a lambda, a
// function and a member function, the result of each form, and an argument
// of a type that cannot move.

#include <foundation/core/CallRef.h>

#include <foundation/core/Report.h>

#include <cstdint>

namespace {

using ::foundation::core::CallRef;

// A proof that cannot copy and cannot move, as the proof of a stage is.
struct StageProof {
    std::uint32_t round = 0;

    explicit constexpr StageProof(std::uint32_t value) noexcept : round{value} {}
    StageProof(StageProof const&) = delete("the test passes the proof by reference only");
    StageProof(StageProof&&) = delete("the test passes the proof by reference only");
    StageProof& operator=(StageProof const&) = delete("the test passes the proof by reference only");
    StageProof& operator=(StageProof&&) = delete("the test passes the proof by reference only");
};

[[nodiscard]] std::uint32_t add_seven(std::uint32_t value) noexcept { return value + 7; }

struct Counter {
    std::uint32_t total = 0;

    [[nodiscard]] std::uint32_t add(std::uint32_t value) noexcept {
        total += value;
        return total;
    }
};

// A lambda that a name holds: the CallRef calls the same object, so its
// state changes.
void test_call_through_a_lambda() {
    std::uint32_t calls = 0;
    auto count = [&calls](std::uint32_t value) noexcept {
        calls += value;
        return calls;
    };
    CallRef<std::uint32_t(std::uint32_t) noexcept> const call_ref{count};
    CRUCIBLE_FATAL_INVARIANT(call_ref(2) == 2);
    CRUCIBLE_FATAL_INVARIANT(call_ref(3) == 5);
    CRUCIBLE_FATAL_INVARIANT(calls == 5);
}

// A function has no object.
void test_call_through_a_function() {
    CallRef<std::uint32_t(std::uint32_t) noexcept> const call_ref =
        CallRef<std::uint32_t(std::uint32_t) noexcept>::of<&add_seven>();
    CRUCIBLE_FATAL_INVARIANT(call_ref(1) == 8);
}

// A member function runs on the object that bind took.
void test_call_through_a_member() {
    Counter counter{};
    CallRef<std::uint32_t(std::uint32_t) noexcept> const call_ref =
        CallRef<std::uint32_t(std::uint32_t) noexcept>::bind<&Counter::add>(counter);
    CRUCIBLE_FATAL_INVARIANT(call_ref(4) == 4);
    CRUCIBLE_FATAL_INVARIANT(call_ref(6) == 10);
    CRUCIBLE_FATAL_INVARIANT(counter.total == 10);
}

// A signature with a void result drops the result of the callable, and an
// argument that cannot move reaches the callable by reference.
void test_void_result_and_proof_argument() {
    std::uint32_t seen = 0;
    auto read_round = [&seen](StageProof const& proof) noexcept {
        seen = proof.round;
        return proof.round + 1;
    };
    CallRef<void(StageProof const&) noexcept> const call_ref{read_round};
    call_ref(StageProof{9});
    CRUCIBLE_FATAL_INVARIANT(seen == 9);
}

// A CallRef is two words: the address of the callable and the thunk.
void test_two_words() { CRUCIBLE_FATAL_INVARIANT(sizeof(CallRef<void() noexcept>) == 2 * sizeof(void*)); }

// The walk of the scope brand reads each class one time.  So it ends on a
// closure over a local class that holds an optional reference of its own
// type, and the CallRef takes the closure.
void test_walk_ends_on_a_cycle() {
    struct NodeBrand final {};
    struct Node {
        ::foundation::core::Option<::foundation::core::Ref<Node, NodeBrand>> next{};
        std::uint32_t value = 0;
    };
    Node node{.next = {}, .value = 4};
    auto read_value = [&node]() noexcept { return node.value; };
    static_assert(::foundation::core::HoldsNoScopeBrand<decltype(read_value)>);
    CallRef<std::uint32_t() noexcept> const call_ref{read_value};
    CRUCIBLE_FATAL_INVARIANT(call_ref() == 4);
}

// The walk has no depth limit.  A chain of nine closures with no scope brand
// holds no brand at any step, so the CallRef takes its last closure.
void test_walk_of_a_deep_chain() {
    std::uint32_t value = 6;
    auto first = [&value]() noexcept { return value; };
    auto second = [&first]() noexcept { return first(); };
    auto third = [&second]() noexcept { return second(); };
    auto fourth = [&third]() noexcept { return third(); };
    auto fifth = [&fourth]() noexcept { return fourth(); };
    auto sixth = [&fifth]() noexcept { return fifth(); };
    auto seventh = [&sixth]() noexcept { return sixth(); };
    auto eighth = [&seventh]() noexcept { return seventh(); };
    auto ninth = [&eighth]() noexcept { return eighth(); };
    static_assert(::foundation::core::HoldsNoScopeBrand<decltype(ninth)>);
    CallRef<std::uint32_t() noexcept> const call_ref{ninth};
    CRUCIBLE_FATAL_INVARIANT(call_ref() == 6);
}

}  // namespace

int main() {
    test_call_through_a_lambda();
    test_call_through_a_function();
    test_call_through_a_member();
    test_void_result_and_proof_argument();
    test_two_words();
    test_walk_ends_on_a_cycle();
    test_walk_of_a_deep_chain();
    return 0;
}
