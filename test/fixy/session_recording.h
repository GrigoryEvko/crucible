#pragma once

// The shared part of test_session_recording: the roles of the events,
// the queues, the transports, and the checks that
// test_session_recording_decorators.cpp holds.

#include <fixy/session/Delegate.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Recording.h>

#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>

namespace test_session_recording_types {

namespace s = ::fixy::session;

inline int fail(const char* what) {
    std::fprintf(stderr, "test_session_recording: %s\n", what);
    return 1;
}

// The context of each session of the test.  No payload here carries an
// effect row, so the background context admits every protocol.
using BgCtx = ::foundation::effects::detail::ctx_witnesses::BgWitness;
[[nodiscard]] inline BgCtx bg_ctx() noexcept { return BgCtx{::foundation::effects::testing::bg()}; }

inline constexpr s::RoleTagId kSelf{11};
inline constexpr s::RoleTagId kPeer{22};

// ── The wire ────────────────────────────────────────────────────────

struct Mailbox {
    std::deque<std::uint64_t> slots;
};

struct Port {
    Mailbox* in = nullptr;
    Mailbox* out = nullptr;
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

// A write tries, and the queue has no bound, so each try takes the value.
// A read polls: the slot when one is queued, and no value otherwise.
inline constexpr auto push_label = [](Port& port, std::size_t label) noexcept {
    port.out->slots.push_back(label);
    return true;
};
inline constexpr auto push_int = [](Port& port, int& value) noexcept {
    port.out->slots.push_back(static_cast<std::uint64_t>(value));
    return true;
};
inline constexpr auto pop_label = [](Port& port) noexcept -> std::optional<std::size_t> {
    if (port.in->slots.empty()) return std::nullopt;
    const std::uint64_t slot = port.in->slots.front();
    port.in->slots.pop_front();
    return slot;
};
inline constexpr auto pop_int = [](Port& port) noexcept {
    return pop_label(port).transform([](std::size_t slot) noexcept { return static_cast<int>(slot); });
};

// The roles and the labels of a keyed choice.
struct Carol {};
struct Yes {};
struct No {};

// test_session_recording_decorators.cpp: the recorder outside a crash
// transport and outside a checkpoint session.  Each returns 0 when its
// claims hold.
int check_crash_recording();
int check_checkpoint_recording();
int check_keyed_checkpoint();

}  // namespace test_session_recording_types
