#pragma once

// The Linux kernel caps a thread name at TASK_COMM_LEN, 16 bytes counting the
// terminator, and truncates anything longer without an error.  Bounding the
// name type keeps an over-long name from ever reaching the kernel.
//
// Naming writes to the calling thread's entry under /proc, so the mint takes an
// init-phase context.  Code that holds no context cannot name a thread.
//
// Old spelling: include/crucible/safety/_ThreadName.h.

#include <foundation/Platform.h>
#include <foundation/diag/RowHash.h>
#include <foundation/effects/Ctx.h>

#include <pthread.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace fixy {

template <std::size_t N>
struct ThreadNameLiteral {
    // N counts the terminator of the source literal, so the kernel's cap of 15
    // visible characters is a bound of 16 here.
    static_assert(N >= 1, "ThreadNameLiteral: degenerate empty literal");
    static_assert(N <= 16, "thread name exceeds TASK_COMM_LEN (15 visible chars + "
                           "NUL); the Linux kernel would SILENTLY truncate it — shorten the name.");

    std::array<char, N> data{};

    consteval ThreadNameLiteral(const char (&literal)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) {
            data[index] = literal[index];
        }
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return data.data(); }

    static constexpr std::size_t visible_length = N - 1;
};

template <ThreadNameLiteral Name>
class ThreadNamed;

// The right arm goes through the named capability lift rather than reading the
// row inline, because the lift folds in the guard that keeps a row from being
// named on a type that has none.
template <typename Ctx>
concept CtxIsInitPhase =
    std::same_as<std::remove_cvref_t<Ctx>, ::foundation::effects::Init>
    || ::foundation::effects::CtxOwnsCapability<std::remove_cvref_t<Ctx>, ::foundation::effects::Effect::Init>;

// §XXI carve-out: cx=alloc — naming a thread is a kernel side effect.
template <ThreadNameLiteral Name, typename Ctx>
    requires CtxIsInitPhase<Ctx>
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
// ownership of the claim cannot.  The constructor is user-provided, so the type
// is neither trivially copyable nor an implicit-lifetime type, and neither
// std::bit_cast nor std::start_lifetime_as builds one.
template <ThreadNameLiteral Name>
class [[nodiscard]] ThreadNamed {
public:
    static constexpr ThreadNameLiteral name = Name;
    using row_discipline = ThreadNamed;
    using row_payload = ::foundation::diag::row_payloads<>;

    [[nodiscard]] static constexpr const char* c_str() noexcept { return Name.c_str(); }
    [[nodiscard]] static constexpr std::size_t visible_length() noexcept { return Name.visible_length; }

    ThreadNamed(const ThreadNamed&) = delete("a thread-name witness is bound to the thread that minted it; a copy "
                                             "could reach a thread that carries no such name");
    ThreadNamed(ThreadNamed&&) = delete("a thread-name witness is bound to the thread that minted it; a move "
                                        "could carry it to a thread that carries no such name");
    ThreadNamed& operator=(const ThreadNamed&) = delete("a thread-name witness is not assignable");
    ThreadNamed& operator=(ThreadNamed&&) = delete("a thread-name witness is not assignable");
    ~ThreadNamed() = default;

private:
    constexpr ThreadNamed() noexcept {}

    template <ThreadNameLiteral FriendName, typename FriendCtx>
        requires CtxIsInitPhase<FriendCtx>
    friend ThreadNamed<FriendName> mint_thread_name(FriendCtx const&) noexcept;
};

namespace detail::thread_name_extract {

template <typename T>
inline constexpr bool is_thread_named_v = false;
template <ThreadNameLiteral Name>
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
// Not constexpr: the body performs a kernel side effect.
// §XXI carve-out: cx=alloc — naming a thread is a kernel side effect.
template <ThreadNameLiteral Name, typename Ctx>
    requires CtxIsInitPhase<Ctx>
[[nodiscard]] inline ThreadNamed<Name> mint_thread_name(Ctx const&) noexcept {
    // ERANGE is the only documented failure and the name type excludes it, so
    // there is nothing to report and nothing to throw.
    (void)::pthread_setname_np(::pthread_self(), Name.c_str());
    return ThreadNamed<Name>{};
}

static_assert(sizeof(ThreadNamed<"x">) == 1, "ThreadNamed must be an empty witness");
static_assert(!std::is_default_constructible_v<ThreadNamed<"x">> && !std::is_copy_constructible_v<ThreadNamed<"x">>
                  && !std::is_move_constructible_v<ThreadNamed<"x">>,
              "a thread-name witness comes only from mint_thread_name and never leaves the frame that holds it");
// GCC reports a class whose copy and move are all deleted as trivially
// copyable, so the trait is no witness here.  std::bit_cast still fails,
// because it returns its result by value and the move is deleted, and
// neg_os_thread_named_bit_cast pins that.  std::start_lifetime_as needs an
// implicit-lifetime type, and this class is none.
static_assert(!std::is_implicit_lifetime_v<ThreadNamed<"x">> && !std::is_aggregate_v<ThreadNamed<"x">>,
              "std::start_lifetime_as and aggregate initialization must not build a thread-name witness");
static_assert(ThreadNameLiteral<2>{"x"}.visible_length == 1);
static_assert(ThreadNameLiteral<16>{"123456789012345"}.visible_length == 15);

namespace detail::thread_name_invariants {

using namespace ::fixy::detail::thread_name_extract;

static_assert(!std::is_same_v<ThreadNamed<"a">, ThreadNamed<"b">>);
static_assert(std::is_same_v<ThreadNamed<"a">, ThreadNamed<"a">>);

static_assert(IsThreadNamed<ThreadNamed<"crucible-bg">>);
static_assert(!IsThreadNamed<int>);

static_assert(ThreadNamed<"crucible-fg">::visible_length() == 11);
static_assert(std::string_view{ThreadNamed<"crucible-fg">::c_str()} == "crucible-fg");

static_assert(CtxIsInitPhase<::foundation::effects::Init>);
static_assert(!CtxIsInitPhase<::foundation::effects::Bg>);
static_assert(!CtxIsInitPhase<::foundation::effects::Test>);

}  // namespace detail::thread_name_invariants

}  // namespace fixy
