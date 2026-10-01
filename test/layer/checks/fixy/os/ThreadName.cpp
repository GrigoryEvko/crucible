// The compile-time checks of fixy/os/ThreadName.h.

#include <fixy/os/ThreadName.h>

namespace fixy {

static_assert(sizeof(ThreadNamed<"x">) == 1, "ThreadNamed must be an empty witness");
static_assert(!std::is_default_constructible_v<ThreadNamed<"x">> && !std::is_copy_constructible_v<ThreadNamed<"x">>
                  && !std::is_move_constructible_v<ThreadNamed<"x">>,
              "a thread-name witness comes only from mint_thread_name and never leaves the frame that holds it");
// GCC reports a class whose copies and moves are all deleted as trivially
// copyable, and the constraints of std::bit_cast then admit it.  The seal
// member makes the class not trivially copyable, so std::bit_cast refuses
// it at its constraint, and neg_os_thread_named_bit_cast pins that.
// std::start_lifetime_as needs an implicit-lifetime type, and this class
// is none.
static_assert(!std::is_trivially_copyable_v<ThreadNamed<"x">>,
              "std::bit_cast must not build a thread-name witness from bytes");
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
