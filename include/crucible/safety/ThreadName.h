#pragma once

// The Linux kernel caps a thread name at TASK_COMM_LEN, 16 bytes counting the
// terminator, and truncates anything longer without an error.  Bounding the
// name type keeps an over-long name from ever reaching the kernel.
//
// Naming writes to the calling thread's entry under /proc, so the mint takes an
// init-phase context.  Code that holds no context cannot name a thread.

#include <crucible/Platform.h>
#include <crucible/effects/_ExecCtx.h>

#include <pthread.h>

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace crucible::safety {

template <std::size_t N>
struct ThreadNameLiteral {
    // N counts the terminator of the source literal, so the kernel's cap of 15
    // visible characters is a bound of 16 here.
    static_assert(N >= 1, "ThreadNameLiteral: degenerate empty literal");
    static_assert(N <= 16, "thread name exceeds TASK_COMM_LEN (15 visible chars + "
                           "NUL); the Linux kernel would SILENTLY truncate it — shorten the name.");

    char data[N]{};

    consteval ThreadNameLiteral(const char (&literal)[N]) noexcept {
        for (std::size_t index = 0; index < N; ++index) {
            data[index] = literal[index];
        }
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept { return data; }

    static constexpr std::size_t visible_length = N - 1;
};

template <ThreadNameLiteral Name>
class ThreadNamed;

// The right arm goes through the named capability lift rather than reading the
// row inline, because the lift folds in the guard that keeps a row from being
// named on a type that has none.
template <typename Ctx>
concept CtxIsInitPhase =
    std::same_as<std::remove_cvref_t<Ctx>, ::crucible::effects::Init>
    || ::crucible::effects::CtxOwnsCapability<std::remove_cvref_t<Ctx>, ::crucible::effects::Effect::Init>;

// Declared here and defined below the class, because the class names it
// as its sole friend.
// §XXI carve-out: cx=alloc — naming a thread is a kernel side effect.
template <ThreadNameLiteral Name, typename Ctx>
    requires CtxIsInitPhase<Ctx>
[[nodiscard]] inline ThreadNamed<Name> mint_thread_name(Ctx const&) noexcept;

// The witness carries the name in its type, so a consumer can demand proof that
// a thread was named without reading /proc back.  Its one constructor is
// private, mint_thread_name is the sole friend, and the witness can be neither
// copied nor moved, so it stays in the frame of the thread that it names.
template <ThreadNameLiteral Name>
class [[nodiscard]] ThreadNamed {
public:
    static constexpr ThreadNameLiteral name = Name;

    [[nodiscard]] static constexpr const char* c_str() noexcept { return Name.c_str(); }
    [[nodiscard]] static constexpr std::size_t visible_length() noexcept { return Name.visible_length; }

    ThreadNamed(const ThreadNamed&) = delete("a thread-name witness is bound to the thread that minted it");
    ThreadNamed(ThreadNamed&&) = delete("a thread-name witness is bound to the thread that minted it");
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
static_assert(!std::is_default_constructible_v<ThreadNamed<"x">> && !std::is_move_constructible_v<ThreadNamed<"x">>
                  && !std::is_implicit_lifetime_v<ThreadNamed<"x">> && !std::is_aggregate_v<ThreadNamed<"x">>,
              "a thread-name witness comes only from mint_thread_name");
static_assert(ThreadNameLiteral<2>{"x"}.visible_length == 1);
static_assert(ThreadNameLiteral<16>{"123456789012345"}.visible_length == 15);

namespace detail::thread_name_self_test {

using namespace ::crucible::safety::detail::thread_name_extract;

static_assert(!std::is_same_v<ThreadNamed<"a">, ThreadNamed<"b">>);
static_assert(std::is_same_v<ThreadNamed<"a">, ThreadNamed<"a">>);

static_assert(IsThreadNamed<ThreadNamed<"crucible-bg">>);
static_assert(!IsThreadNamed<int>);

static_assert(ThreadNamed<"crucible-fg">::visible_length() == 11);
static_assert(std::string_view{ThreadNamed<"crucible-fg">::c_str()} == "crucible-fg");

static_assert(CtxIsInitPhase<::crucible::effects::Init>);
static_assert(!CtxIsInitPhase<::crucible::effects::Bg>);
static_assert(!CtxIsInitPhase<::crucible::effects::Test>);

// Running this renames the calling thread.
inline void runtime_smoke_test() {
    auto init = ::crucible::effects::testing::init();
    auto witness = mint_thread_name<"crux-smoke">(init);
    // Both facts live in the witness type, not in the value, so they hold by
    // construction: c_str and visible_length read the Name template argument.
    // Stating them to the optimizer would inform nothing, and an invariant
    // would check them only where NDEBUG is absent, so they are asserted at
    // compile time, where they hold in every preset at no runtime cost.
    static_assert(std::string_view{decltype(witness)::c_str()} == "crux-smoke");
    static_assert(decltype(witness)::visible_length() == 10);
    static_assert(IsThreadNamed<decltype(witness)>);
}

}  // namespace detail::thread_name_self_test

}  // namespace crucible::safety
