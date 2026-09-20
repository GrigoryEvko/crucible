// What fixy/session/Handle.h claims, checked.
//
// The compile-time half asserts the Stepping contract for every head
// specialization, the linearity of a handle, the size the two
// abandonment policies produce, and the exact type a step lands on.
// The runtime half walks a protocol end to end and then forks a child
// to prove that the Enforced policy's destructor really aborts on an
// abandoned handle and that the Off policy's really does not.  The fork
// is what makes the policy decision testable: nothing short of running
// the destructor can distinguish a policy that checks from a comment
// that says it does.

#include <fixy/session/Handle.h>

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <type_traits>
#include <utility>

namespace s = fixy::session;

namespace {

struct Ping {
    int value = 0;
};
struct Pong {
    int value = 0;
};
struct Stop {};

// A resource with a stable address is admitted by reference.  Deriving
// from foundation::Pinned is exactly what SessionResource asks for.
struct PinnedWire : ::foundation::Pinned<PinnedWire> {
    int last_sent = 0;
};

struct ValueWire {
    int last_sent = 0;
};

// ── The Stepping contract, per head ──────────────────────────────────

using AtEnd = s::SessionHandle<s::End, ValueWire>;
using AtSend = s::SessionHandle<s::Send<Ping, s::End>, ValueWire>;
using AtRecv = s::SessionHandle<s::Recv<Pong, s::End>, ValueWire>;
using AtSelect = s::SessionHandle<s::Select<s::Send<Ping, s::End>, s::Send<Stop, s::End>>, ValueWire>;
using AtOffer = s::SessionHandle<s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>, ValueWire>;

static_assert(s::SteppingGraded<AtEnd>);
static_assert(s::SteppingGraded<AtSend>);
static_assert(s::SteppingGraded<AtRecv>);
static_assert(s::SteppingGraded<AtSelect>);
static_assert(s::SteppingGraded<AtOffer>);

// The modality is the one Modality.h reserved for session handles, and
// it is the same for every head — the protocol advances, the modality
// does not.
static_assert(AtSend::modality == ::foundation::algebra::ModalityKind::Stepping);
static_assert(AtEnd::modality == AtOffer::modality);

// is_terminal() agrees with the protocol trait.  A handle answering
// otherwise would be destroyed without complaint at a position where
// the protocol still owes a message; the concept rejects it, and these
// two lines say which way each head answers.
static_assert(AtEnd::is_terminal());
static_assert(!AtSend::is_terminal());

// The name forwarder cannot lie: SteppingGraded compares it against the
// tree's one spelling of the protocol's name.
static_assert(AtSend::protocol_name() == s::type_display_name_v<s::Send<Ping, s::End>>);
static_assert(AtSend::protocol_name().find("Send") != std::string_view::npos);

// Linearity.  Two handles at one protocol position would each believe
// they owe the next step.
static_assert(!std::is_copy_constructible_v<AtSend>);
static_assert(!std::is_copy_assignable_v<AtSend>);
static_assert(std::is_move_constructible_v<AtSend>);

// A call site cannot build a handle directly: every value constructor
// is private and befriends only the factory and the handle family, so
// the well-formedness gate and the Loop unroll cannot be walked around.
static_assert(!std::is_constructible_v<AtSend, ValueWire>);

// ── What the policy costs ────────────────────────────────────────────
//
// This is the decision A14.1 made, stated as two sizes.  Under Off the
// tracker is empty, [[no_unique_address]] collapses it, and a handle
// costs exactly its Resource.  Under Enforced it carries a flag and a
// source_location, and a test binary pays for them deliberately.

using OffHandle = s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::check::Off>;
using EnforcedHandle = s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::check::Enforced>;

static_assert(sizeof(OffHandle) == sizeof(ValueWire));
static_assert(sizeof(EnforcedHandle) > sizeof(ValueWire));

// The policy is IN THE TYPE.  These two are different types, so a
// Debug object and a Release object that disagree about the layout
// cannot silently share one definition — the mismatch is a diagnostic
// at the boundary instead.
static_assert(!std::is_same_v<OffHandle, EnforcedHandle>);
static_assert(
    std::is_same_v<AtSend, s::SessionHandle<s::Send<Ping, s::End>, ValueWire, void, s::DefaultAbandonmentPolicy>>);

// A test target compiles with -UNDEBUG, so the default here is the
// checking policy.  Asserting on the policy rather than on NDEBUG is
// the point of naming it: this line keeps meaning if the default is
// ever re-decided.
static_assert(s::default_policy_checks_abandonment);
static_assert(std::is_same_v<s::DefaultAbandonmentPolicy, s::check::Enforced>);

// ── Where a step lands ───────────────────────────────────────────────

using Once = s::Send<Ping, s::Recv<Pong, s::End>>;
using Looping = s::Loop<s::Send<Ping, s::Continue>>;

// Minting a Loop unrolls one iteration: the handle sits at the body's
// head and carries the Loop as its context, which is what binds the
// Continue inside it.
using LoopHead = decltype(s::mint_session_handle<Looping, ValueWire>(std::declval<ValueWire>()));
static_assert(std::is_same_v<typename LoopHead::protocol, s::Send<Ping, s::Continue>>);
static_assert(std::is_same_v<typename LoopHead::loop_ctx, Looping>);

// A non-Loop head passes through with a void context.
using OnceHead = decltype(s::mint_session_handle<Once, ValueWire>(std::declval<ValueWire>()));
static_assert(std::is_same_v<typename OnceHead::protocol, Once>);
static_assert(std::is_same_v<typename OnceHead::loop_ctx, void>);

// A reference to a Pinned resource is admitted; the handle stores the
// reference and the caller keeps the object alive.
static_assert(s::SessionResource<PinnedWire&>);
static_assert(s::SessionResource<ValueWire>);
static_assert(!s::SessionResource<ValueWire&>);
static_assert(!s::SessionResource<PinnedWire&&>);

// The mint's gate is a concept, so a downstream requires-expression can
// ask whether a handle is mintable and get an answer instead of a hard
// error at instantiation.  Each rejection is paired with the nearest
// protocol that IS accepted, so a negative fixture is not the only thing
// standing between a broken gate and a green build: a gate that grew too
// broad breaks the accepting line here instead.
static_assert(s::WellFormedRunnableProtocol<Once>);
static_assert(!s::WellFormedRunnableProtocol<s::Continue>);
static_assert(s::WellFormedRunnableProtocol<s::Loop<s::Send<Ping, s::Continue>>>);
static_assert(!s::WellFormedRunnableProtocol<s::Send<Ping, s::Continue>>);
static_assert(s::WellFormedRunnableProtocol<s::Send<Ping, s::Select<s::Send<Stop, s::End>>>>);
static_assert(!s::WellFormedRunnableProtocol<s::Send<Ping, s::Select<>>>);

// The detach roster admits its own tags and nothing else.
static_assert(s::DetachReason<s::detach_reason::TestInstrumentation>);
static_assert(!s::DetachReason<s::detach_reason::tag_base>);
static_assert(!s::DetachReason<int>);

// ── Runtime: walking a protocol ──────────────────────────────────────

[[nodiscard]] int walk_once() {
    auto handle = s::mint_session_handle<Once, ValueWire>(ValueWire{});

    auto after_send = std::move(handle).send(Ping{3}, [](ValueWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
    static_assert(std::is_same_v<typename decltype(after_send)::protocol, s::Recv<Pong, s::End>>);

    auto [pong, at_end] = std::move(after_send).recv([](ValueWire& w) noexcept { return Pong{w.last_sent}; });
    if (pong.value != 3) {
        std::fprintf(stderr, "the value the transport wrote did not travel with the resource\n");
        return 1;
    }

    // close() is reachable only at End, and it is what hands the
    // resource back.  The value it returns is the one every step
    // carried, which is how a caller recovers its channel after the
    // protocol has run out.
    const ValueWire returned = std::move(at_end).close();
    if (returned.last_sent != 3) {
        std::fprintf(stderr, "close returned a resource that lost the protocol's effect\n");
        return 1;
    }
    return 0;
}

// The Pinned-reference branch of SessionResource, walked rather than
// asserted.  The frozen tree admitted this Resource shape and then
// could not mint one: the factory took `Resource r` and passed
// `std::move(r)`, which for Resource = Wire& is an rvalue that cannot
// bind back to Wire&, so the branch the concept's longest comment
// exists to explain failed at the first call.  Forwarding instead of
// moving is what makes this function compile, so this function is the
// regression test for that repair.
[[nodiscard]] int walk_pinned_reference() {
    PinnedWire wire{};
    const PinnedWire* const address_before = &wire;

    auto handle = s::mint_session_handle<s::Send<Ping, s::End>, PinnedWire&>(wire);
    auto at_end = std::move(handle).send(Ping{11}, [](PinnedWire& w, Ping&& p) noexcept { w.last_sent = p.value; });
    if (wire.last_sent != 11) {
        std::fprintf(stderr, "the transport did not reach the referenced resource\n");
        return 1;
    }

    // close() hands back the reference, not a copy — a Pinned resource
    // cannot be copied or moved, which is why the concept admits it by
    // reference in the first place.
    PinnedWire& returned = std::move(at_end).close();
    if (&returned != address_before) {
        std::fprintf(stderr, "close returned a different object than the one the handle borrowed\n");
        return 1;
    }
    return 0;
}

[[nodiscard]] int walk_choice() {
    using Choice = s::Select<s::Send<Ping, s::End>, s::Send<Stop, s::End>>;
    auto handle = s::mint_session_handle<Choice, ValueWire>(ValueWire{});

    // select<I> tells the peer which branch was taken; select_local<I>
    // deliberately does not, and the two names are what keep that
    // difference visible at the call site.
    std::size_t signalled = 99;
    auto chosen =
        std::move(handle).select<1>([&signalled](ValueWire&, std::size_t label) noexcept { signalled = label; });
    if (signalled != 1) {
        std::fprintf(stderr, "select did not signal the branch index\n");
        return 1;
    }
    static_assert(std::is_same_v<typename decltype(chosen)::protocol, s::Send<Stop, s::End>>);

    auto at_end = std::move(chosen).send(Stop{}, [](ValueWire&, Stop&&) noexcept {});
    (void)std::move(at_end).close();
    return 0;
}

[[nodiscard]] int walk_offer() {
    using Served = s::Offer<s::Recv<Ping, s::End>, s::Recv<Stop, s::End>>;
    auto handle = s::mint_session_handle<Served, ValueWire>(ValueWire{});

    // The handler runs once, for the branch the peer's label names.
    const int taken = std::move(handle).branch([](ValueWire&) noexcept -> std::size_t { return 0; },
                                               [](auto branch_handle) {
                                                   using B = typename decltype(branch_handle)::protocol;
                                                   auto [msg, at_end] =
                                                       std::move(branch_handle).recv([](ValueWire&) noexcept {
                                                           return typename decltype(branch_handle)::message_type{};
                                                       });
                                                   (void)msg;
                                                   (void)std::move(at_end).close();
                                                   return std::is_same_v<B, s::Recv<Ping, s::End>> ? 0 : 1;
                                               });
    if (taken != 0) {
        std::fprintf(stderr, "branch entered the wrong arm\n");
        return 1;
    }
    return 0;
}

// ── Runtime: the policy actually runs ────────────────────────────────
//
// Builds a handle at a non-terminal position and drops it.  Under
// Enforced the destructor prints and aborts; under Off it does nothing.
// The caller runs this in a child process and reads the exit status.

template <typename Policy>
[[noreturn]] void abandon_a_handle() {
    {
        auto handle = s::mint_session_handle<Once, ValueWire, Policy>(ValueWire{});
        (void)handle;
    }
    // Reached only when the policy does not check.
    std::_Exit(0);
}

// Runs `body` in a child and reports whether the child died on a
// signal, which is how std::abort() exits.
template <typename Body>
[[nodiscard]] bool child_aborted(Body body) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        std::fprintf(stderr, "fork failed\n");
        std::_Exit(2);
    }
    if (pid == 0) {
        body();
        std::_Exit(0);
    }
    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        std::fprintf(stderr, "waitpid failed\n");
        std::_Exit(2);
    }
    return WIFSIGNALED(status) != 0;
}

[[nodiscard]] int policy_runs() {
    // The abandonment diagnostic is the child's stderr, and a passing
    // run prints it once.  Saying so here keeps a reader from taking
    // the banner for a failure.
    std::fprintf(stderr, "[expected] the abandonment banner below comes from the child process "
                         "that proves check::Enforced aborts\n");

    if (!child_aborted(abandon_a_handle<s::check::Enforced>)) {
        std::fprintf(stderr, "check::Enforced did not abort on an abandoned handle — the policy is named but "
                             "does nothing\n");
        return 1;
    }
    if (child_aborted(abandon_a_handle<s::check::Off>)) {
        std::fprintf(stderr, "check::Off aborted — the unchecked policy is not unchecked\n");
        return 1;
    }

    // Detaching with a reason tag retires the handle without advancing
    // it, and the check must then stay quiet under the checking policy.
    const bool detach_aborted = child_aborted([] {
        auto handle = s::mint_session_handle<Once, ValueWire, s::check::Enforced>(ValueWire{});
        std::move(handle).detach(s::detach_reason::TestInstrumentation{});
    });
    if (detach_aborted) {
        std::fprintf(stderr, "a detached handle still aborted — detach does not mark the handle consumed\n");
        return 1;
    }

    // Moving marks the SOURCE consumed.  Without that, every step
    // through a protocol would leave a moved-from handle behind whose
    // destructor reports an abandonment that did not happen.
    const bool move_aborted = child_aborted([] {
        auto handle = s::mint_session_handle<Once, ValueWire, s::check::Enforced>(ValueWire{});
        auto moved = std::move(handle);
        std::move(moved).detach(s::detach_reason::TestInstrumentation{});
    });
    if (move_aborted) {
        std::fprintf(stderr, "a moved-from handle aborted — the move did not mark the source consumed\n");
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    if (const int rc = walk_once(); rc != 0) return rc;
    if (const int rc = walk_pinned_reference(); rc != 0) return rc;
    if (const int rc = walk_choice(); rc != 0) return rc;
    if (const int rc = walk_offer(); rc != 0) return rc;
    if (const int rc = policy_runs(); rc != 0) return rc;
    return 0;
}
