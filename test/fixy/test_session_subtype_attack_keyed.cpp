// The wire word of a keyed branch is its label: a permuted keyed Select
// on a word wire, and a keyed Send step against a wider Offer on two
// threads.

#include "session_subtype_attack.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

namespace test_session_subtype_attack_types {

// ── The wire word of a keyed branch is its label ─────────────────────
//
// A PeerMsg names a label key.  select<I>() sends the label word of the
// key of branch I, and the Offer of the peer dispatches on the word.  So
// a Select that names the same labels in another order takes, at run
// time, the branch of the label that it sent.  The relation pairs the
// branches of a keyed choice by label, so it admits the permutation that
// the run below routes, in both directions and in both relations.

// L1 continues with a value, so the branch that a handle enters shows in
// its type.
using Projected = Select<Send<s::PeerMsg<Bob, L0, int>, End>, Send<s::PeerMsg<Bob, L1, int>, Send<int, End>>>;
using Permuted = Select<Send<s::PeerMsg<Bob, L1, int>, Send<int, End>>, Send<s::PeerMsg<Bob, L0, int>, End>>;
static_assert(s::is_subtype_sync_v<Permuted, Projected> && s::is_subtype_sync_v<Projected, Permuted>);
static_assert(s::is_subtype_async_v<Permuted, Projected, ring<4>>
              && s::CompatibleServer<Permuted, s::dual_of_t<Projected>>);

// A keyed Select drops labels in any position, and a keyed Offer adds
// them in any position.  A label that the supertype does not send is
// refused, and so is a keyed choice against a positional one.
using SendsL1 = Select<Send<s::PeerMsg<Bob, L1, int>, Send<int, End>>>;
static_assert(s::is_subtype_sync_v<SendsL1, Projected>);
static_assert(s::subtype_mismatch_v<Projected, SendsL1> == tr::mismatch::label_set);
static_assert(s::is_subtype_sync_v<s::dual_of_t<Projected>, s::dual_of_t<SendsL1>>);
static_assert(s::subtype_mismatch_v<s::dual_of_t<SendsL1>, s::dual_of_t<Projected>> == tr::mismatch::label_set);
static_assert(s::subtype_mismatch_v<Select<Send<A, End>, Send<B, End>>, Projected> == tr::mismatch::label_discipline);
static_assert(s::branch_wire_word_v<Permuted, 0> == s::branch_wire_word_v<Projected, 1>
              && s::branch_wire_word_v<Permuted, 1> == s::branch_wire_word_v<Projected, 0>);

// One slot for the label word, and a queue of values in the order of the
// writes.
struct WordWire {
    std::size_t word = 0;
    std::array<int, 4> values{};
    std::size_t written = 0;
    std::size_t read = 0;
};
struct PickerEnd {
    WordWire* wire = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct OffererEnd {
    WordWire* wire = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// Returns the label that the picker sent and the label that the offerer
// received, when the picker speaks Permuted and the offerer speaks the
// dual of Projected.  A keyed message is its label word and then its
// value, so each side enters its branch at the value step of its label.
// L1 then continues with one more value, and L0 ends.
[[nodiscard]] std::pair<int, int> labels_on_a_word_wire() {
    WordWire wire{};
    const auto push_value = [](PickerEnd& end, int& value) noexcept {
        end.wire->values[end.wire->written++] = value;
        return true;
    };
    const auto pop_value = [](OffererEnd& end) noexcept -> std::optional<int> {
        if (end.wire->read == end.wire->written) return std::nullopt;
        return end.wire->values[end.wire->read++];
    };
    auto picker = s::mint_session_handle<Permuted>(PickerEnd{&wire});
    auto chosen = std::move(picker).template select<0>([](PickerEnd& end, std::size_t word) noexcept {
        end.wire->word = word;
        return true;
    });
    static_assert(std::is_same_v<typename decltype(chosen)::protocol, Send<int, Send<int, End>>>,
                  "branch 0 of Permuted is L1, and the select stands at the value step of L1");
    auto picker_done = std::move(chosen).send(4, push_value).send(5, push_value);
    (void)std::move(picker_done).close();

    // The picker wrote the word and the two values before the offerer
    // reads them, so each poll finds its value.
    int received = -1;
    auto offerer = s::mint_session_handle<s::dual_of_t<Projected>>(OffererEnd{&wire});
    std::move(offerer).branch([](OffererEnd& end) noexcept -> std::optional<std::size_t> { return end.wire->word; },
                              [&received, &pop_value](auto handle) noexcept {
                                  using Head = typename decltype(handle)::protocol;
                                  if constexpr (std::is_same_v<Head, Recv<int, Recv<int, End>>>) {
                                      auto [label_value, then] = std::move(handle).recv(pop_value);
                                      auto [value, done] = std::move(then).recv(pop_value);
                                      received = label_value == 4 && value == 5 ? 1 : -1;
                                      (void)std::move(done).close();
                                  } else {
                                      auto [label_value, done] = std::move(handle).recv(pop_value);
                                      static_cast<void>(label_value);
                                      received = 0;
                                      (void)std::move(done).close();
                                  }
                              });
    return {1, received};
}

// ── A keyed step against a wider Offer, on two threads ──────────────
//
// A keyed Send is the Select of its one branch, so it refines a Select
// that names more labels, and its peer can hold the dual of that Select.
// The step puts the label word on the wire, and the Offer of the peer
// enters the branch of that word.  The value of the label and then the
// value of the continuation follow.  The two endpoints run on two threads
// over one slot for the word and a queue for the values, each read by a
// poll.

namespace {

using SendsL1Step = Send<s::PeerMsg<Bob, L1, int>, Send<int, End>>;
static_assert(s::is_subtype_sync_v<SendsL1Step, Projected> && s::equivalent_sync_v<SendsL1Step, SendsL1>);
static_assert(s::step_wire_word_v<SendsL1Step> == s::branch_wire_word_v<Projected, 1>,
              "the keyed step sends the word of its branch in the wider Select");

// One writer and one reader.  The writer stores a value, and then
// publishes the count of values with a release store.
struct SharedWire {
    std::atomic<std::uint64_t> word{0};
    std::atomic<bool> has_word{false};
    std::array<std::atomic<int>, 4> values{};
    std::atomic<std::size_t> written{0};
};
struct StepEnd {
    SharedWire* wire = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};
struct WideEnd {
    SharedWire* wire = nullptr;
    std::size_t read = 0;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

}  // namespace

[[nodiscard]] int keyed_step_meets_wider_offer() {
    SharedWire wire{};
    int received = -1;
    {
        std::jthread offerer_thread{[&wire, &received] {
            const auto pop_value = [](WideEnd& end) noexcept -> std::optional<int> {
                if (end.read == end.wire->written.load(std::memory_order_acquire)) return std::nullopt;
                return end.wire->values[end.read++].load(std::memory_order_relaxed);
            };
            auto offerer = s::mint_session_handle<s::dual_of_t<Projected>>(WideEnd{&wire});
            std::move(offerer).branch(
                [](WideEnd& end) noexcept -> std::optional<std::size_t> {
                    if (!end.wire->has_word.load(std::memory_order_acquire)) return std::nullopt;
                    return end.wire->word.load(std::memory_order_relaxed);
                },
                [&received, &pop_value](auto handle) noexcept {
                    if constexpr (std::is_same_v<typename decltype(handle)::protocol, Recv<int, Recv<int, End>>>) {
                        auto [label_value, then] = std::move(handle).recv(pop_value);
                        auto [value, done] = std::move(then).recv(pop_value);
                        received = label_value == 8 ? value : -2;
                        (void)std::move(done).close();
                    } else {
                        auto [label_value, done] = std::move(handle).recv(pop_value);
                        static_cast<void>(label_value);
                        received = 0;
                        (void)std::move(done).close();
                    }
                });
        }};
        std::jthread stepper_thread{[&wire] {
            const auto push_value = [](StepEnd& end, int& value) noexcept {
                const std::size_t slot = end.wire->written.load(std::memory_order_relaxed);
                end.wire->values[slot].store(value, std::memory_order_relaxed);
                end.wire->written.store(slot + 1, std::memory_order_release);
                return true;
            };
            auto stepper = s::mint_session_handle<SendsL1Step>(StepEnd{&wire});
            auto label_value = std::move(stepper).send([](StepEnd& end, std::size_t word) noexcept {
                end.wire->word.store(word, std::memory_order_relaxed);
                end.wire->has_word.store(true, std::memory_order_release);
                return true;
            });
            auto done = std::move(label_value).send(8, push_value).send(9, push_value);
            (void)std::move(done).close();
        }};
    }
    return received;
}

}  // namespace test_session_subtype_attack_types
