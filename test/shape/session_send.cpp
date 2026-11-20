// The probe of the steady loop of a session send on a local handle.
//
// test/shape/check_session_send.py reads the disassembly of this object in a
// build at -O2 or -O3, and it checks the innermost loop of the function below.
// The function moves a handle into a local, sends each item through it, and
// moves it back, as a real caller that keeps its handle on its own stack.  A
// step can call the cold note of a holder change (fixy/session/Stepping.h).
// No call can reach the local, so the steady loop must keep the handle and
// the item in registers: no load of a field of the handle, no read of the
// thread slot, and no call.
//
// The function takes its operands as parameters, so the optimizer cannot fold
// them into the code.  It is [[gnu::used]] in an unnamed namespace, so the
// compiler emits it and its name stays in this unit.

#include <fixy/session/Entry.h>
#include <fixy/session/Handle.h>
#include <fixy/session/Mint.h>

#include <foundation/effects/Ctx.h>

#include <cstdint>
#include <utility>

namespace {

namespace s = ::fixy::session;

using Item = std::uint64_t;

// A value Resource, so the handle owns its channel and a send writes the
// state of the handle.
struct ProbeChannel {
    Item last = 0;
};

// The trying write: it always has room.
constexpr auto write_item = [](ProbeChannel& channel, Item& value) noexcept {
    channel.last = value;
    return true;
};

using SendLoop = s::Loop<s::Send<Item, s::Continue>>;
using LoopHandle =
    decltype(s::mint_session<SendLoop>(::foundation::effects::testing::foreground(), std::declval<ProbeChannel>()));

[[gnu::used]] void send_on_a_local_handle(LoopHandle& handle, Item& item, std::uint32_t count) noexcept {
    LoopHandle local = std::move(handle);
    Item value = item;
    for (std::uint32_t sent = 0; sent < count; ++sent) {
        local = std::move(local).send(++value, write_item);
    }
    item = value;
    handle = std::move(local);
}

// The form of a pin: the handle of the caller goes into a pin, and the body
// of the pin sends each item through the pinned handle.  A step under the
// pinned policy has no holder check, so the steady loop reads no memory, not
// even the slot of the thread, and calls nothing.  The count of the loop is a
// parameter, so the function keeps its loop.
[[gnu::used]] void send_through_a_pin(LoopHandle& handle, Item& item, std::uint32_t count) noexcept {
    handle = s::HandleFactory::pin(::foundation::effects::testing::foreground(), std::move(handle),
                                   [&item, count](auto pinned) noexcept {
                                       Item value = item;
                                       for (std::uint32_t sent = 0; sent < count; ++sent) {
                                           pinned = std::move(pinned).send(++value, write_item);
                                       }
                                       item = value;
                                       return pinned;
                                   });
}

// The control of the check: the loop steps the handle and the item of the
// caller in place.  The cold note can write each value that the caller can
// reach through a pointer, so this loop keeps the handle in memory, and the
// check must find that.  A check that finds nothing here cannot see the
// defect that it guards.
[[gnu::used]] void send_in_caller_memory(LoopHandle& handle, Item& item, std::uint32_t count) noexcept {
    for (std::uint32_t sent = 0; sent < count; ++sent) {
        handle = std::move(handle).send(++item, write_item);
    }
}

}  // namespace
