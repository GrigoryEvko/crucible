#pragma once

// The witness that the calling thread carries a name.  The name is a
// ThreadName of fixy/os/ThreadTasks.h, the one type of the name of a thread:
// a literal of 1 to 15 characters, the bound of TASK_COMM_LEN, so no name
// that the kernel would cut reaches the kernel.
//
// Naming writes to the calling thread's entry under /proc, so the mint takes an
// init-phase context.  Code that holds no context cannot name a thread.

#include <fixy/os/ThreadTasks.h>
#include <foundation/Lifetime.h>
#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <type_traits>

namespace fixy {

template <::fixy::thread::ThreadName Name>
    requires(Name.is_named())
class ThreadNamed;

// The right arm goes through the named capability lift rather than reading the
// row inline, because the lift folds in the guard that keeps a row from being
// named on a type that has none.
template <typename Ctx>
concept CtxIsInitPhase =
    std::same_as<std::remove_cvref_t<Ctx>, ::foundation::effects::Init>
    || ::foundation::effects::CtxOwnsCapability<std::remove_cvref_t<Ctx>, ::foundation::effects::Effect::Init>;

template <::fixy::thread::ThreadName Name, typename Ctx>
    requires(Name.is_named() && CtxIsInitPhase<Ctx>)
[[nodiscard]] inline ThreadNamed<Name> mint_thread_name(Ctx const&) noexcept;

// The witness carries the name in its type, so a consumer can demand proof that
// a thread was named without reading /proc back.
//
// The witness is a proof, so it is built in one place.  Its one constructor is
// private, and mint_thread_name, which calls pthread_setname_np first, is the
// sole friend.  The witness is also neither copyable nor movable.  The mint
// names the calling thread, and the witness then stays in the frame of that
// thread: a copy or a move into another thread would claim a name that the
// other thread does not carry.  A reference can still leave the frame, but the
// ownership of the claim cannot.  The seal member makes the type not trivially
// copyable, and the user-provided constructor makes it no implicit-lifetime
// type, so neither std::bit_cast nor std::start_lifetime_as builds one.
template <::fixy::thread::ThreadName Name>
    requires(Name.is_named())
class [[nodiscard]] ThreadNamed {
public:
    static constexpr ::fixy::thread::ThreadName name = Name;
    using row_discipline = ThreadNamed;
    using row_payload = ::foundation::diag::row_payloads<>;

    [[nodiscard]] static constexpr const char* c_str() noexcept { return name.c_str(); }
    [[nodiscard]] static constexpr std::size_t visible_length() noexcept { return name.visible_length(); }

    ThreadNamed(const ThreadNamed&) = delete("a thread-name witness is bound to the thread that minted it; a copy "
                                             "could reach a thread that carries no such name");
    ThreadNamed(ThreadNamed&&) = delete("a thread-name witness is bound to the thread that minted it; a move "
                                        "could carry it to a thread that carries no such name");
    ThreadNamed& operator=(const ThreadNamed&) = delete("a thread-name witness is not assignable");
    ThreadNamed& operator=(ThreadNamed&&) = delete("a thread-name witness is not assignable");
    ~ThreadNamed() = default;

private:
    [[no_unique_address]] ::foundation::lifetime::byte_seal seal_{};

    constexpr ThreadNamed() noexcept {}

    template <::fixy::thread::ThreadName FriendName, typename FriendCtx>
        requires(FriendName.is_named() && CtxIsInitPhase<FriendCtx>)
    friend ThreadNamed<FriendName> mint_thread_name(FriendCtx const&) noexcept;
};

namespace detail::thread_name_extract {

template <typename T>
inline constexpr bool is_thread_named_v = false;
template <::fixy::thread::ThreadName Name>
inline constexpr bool is_thread_named_v<ThreadNamed<Name>> = true;

}  // namespace detail::thread_name_extract

template <typename T>
concept IsThreadNamed = detail::thread_name_extract::is_thread_named_v<std::remove_cvref_t<T>>;

// The context is read for its type alone and is not consumed, which is the
// shape §XXI fixes for every ctx-bound mint.  The authority is real without
// being one-shot: an Init context is reachable only through
// mint_context, whose passkey only the initialization owner and the test
// witness can build.  A phase token that the
// first naming call consumed would end the phase it exists to describe.
//
// Not constexpr: the body performs a kernel side effect.  The thread door of
// fixy/os/ThreadTasks.h gives the name, and the name type excludes the one
// failure of the call.
template <::fixy::thread::ThreadName Name, typename Ctx>
    requires(Name.is_named() && CtxIsInitPhase<Ctx>)
[[nodiscard]] inline ThreadNamed<Name> mint_thread_name(Ctx const&) noexcept {
    ::fixy::thread::detail::name_calling_thread(Name);
    return ThreadNamed<Name>{};
}

}  // namespace fixy
