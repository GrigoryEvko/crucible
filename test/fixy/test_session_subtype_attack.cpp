// Attacks on the protocol algebra and on subtyping, through correct use
// of the public surface only: no cast, no reopened namespace outside a
// documented registry, no undefined behaviour.  Each attack either
// fails, and the test pins the failure, or it succeeds, and the test
// pins the success on the known-limitation ledger at the end.  The
// ledger only shrinks: an entry whose attack no longer works fails its
// pin, and the entry goes.
//
// The runtime attacks run two threads over two bounded queues.  A
// watchdog ends a run that makes no progress for a fixed time and
// records a deadlock, so no attack can hang the test.

#include <fixy/session/Handle.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <meta>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;
namespace tags = ::fixy::tags;

namespace {

struct A {};
struct B {};
struct Alice {};
struct Bob {};

using s::Continue;
using s::End;
using s::Loop;
using s::Offer;
using s::Recv;
using s::Select;
using s::Send;
using s::Sender;

// ── A family of anticipations ────────────────────────────────────────
//
// early<K> sends K messages and then receives K.  late<K> receives K
// and then sends K.  early<K> refines late<K> asynchronously only when
// the channel holds K messages in each direction: the subtype sends K
// ahead, and the peer, which speaks the dual of late<K>, sends K before
// it receives.

template <std::size_t N, class Message, class Rest>
struct sends {
    using type = Send<Message, typename sends<N - 1, Message, Rest>::type>;
};
template <class Message, class Rest>
struct sends<0, Message, Rest> {
    using type = Rest;
};
template <std::size_t N, class Message, class Rest>
struct receives {
    using type = Recv<Message, typename receives<N - 1, Message, Rest>::type>;
};
template <class Message, class Rest>
struct receives<0, Message, Rest> {
    using type = Rest;
};

template <std::size_t K>
using early = typename sends<K, A, typename receives<K, B, End>::type>::type;
template <std::size_t K>
using late = typename receives<K, B, typename sends<K, A, End>::type>::type;

template <std::size_t K, std::size_t C>
inline constexpr bool family_verdict_v = s::is_subtype_async_v<early<K>, late<K>, C>;

// The check admits exactly the capacities that hold the anticipation:
// K from 1 to 4, capacity from 0 to 4.
template <std::size_t K, std::size_t... Cs>
consteval bool row_matches_capacity(std::index_sequence<Cs...>) {
    return ((family_verdict_v<K, Cs> == (Cs >= K)) && ...);
}
template <std::size_t... Ks>
consteval bool family_matches_capacity(std::index_sequence<Ks...>) {
    return (row_matches_capacity<Ks + 1>(std::make_index_sequence<5>{}) && ...);
}
static_assert(family_matches_capacity(std::make_index_sequence<4>{}),
              "an off-by-one capacity is refused, one step in either direction");

// ── Scripts from protocol types ──────────────────────────────────────

struct step {
    bool is_send = false;
    int message = 0;
};

consteval int message_id(std::meta::info payload) {
    if (payload == ^^A) return 1;
    if (payload == ^^B) return 2;
    return 0;
}

consteval std::vector<step> script_steps(std::meta::info protocol) {
    std::vector<step> steps;
    std::meta::info current = std::meta::dealias(protocol);
    while (std::meta::has_template_arguments(current)) {
        const std::meta::info shape = std::meta::template_of(current);
        const std::vector<std::meta::info> arguments = std::meta::template_arguments_of(current);
        steps.push_back(step{shape == ^^s::Send, message_id(std::meta::dealias(arguments[0]))});
        current = std::meta::dealias(arguments[1]);
    }
    return steps;
}

template <class P>
inline constexpr std::size_t script_length_v = script_steps(^^P).size();

template <class P>
inline constexpr std::array<step, script_length_v<P>> script_v = [] {
    std::array<step, script_length_v<P>> out{};
    const std::vector<step> steps = script_steps(^^P);
    for (std::size_t index = 0; index < steps.size(); ++index) out[index] = steps[index];
    return out;
}();

// ── A bounded channel and a runner with a watchdog ───────────────────

inline constexpr std::size_t max_capacity = 8;

class bounded_queue {
public:
    explicit bounded_queue(std::size_t capacity) : capacity_{capacity} {}

    bool try_push(int value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= capacity_) return false;
        slots_[head % max_capacity] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(int& value) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        value = slots_[tail % max_capacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool is_empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

private:
    std::array<int, max_capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
    std::size_t capacity_;
};

enum class outcome : std::uint8_t { completed, deadlocked, wrong_message, orphan };

constexpr std::string_view outcome_name(outcome value) {
    switch (value) {
        case outcome::completed:
            return "completed";
        case outcome::deadlocked:
            return "deadlocked";
        case outcome::wrong_message:
            return "received a wrong message";
        case outcome::orphan:
            return "left a message in a buffer";
        default:
            break;
    }
    return "unknown";
}

// The state of one side of a run.
enum class side : int { running, waiting, finished };

// Runs the two scripts against each other.  A run is a deadlock when
// each side that has not finished waits, and no side makes progress for
// `stall_ticks`: a waiting side waits for the other side to act, and
// the other side waits too.  The watchdog then raises the stop flag,
// each thread leaves its wait, and both joins return.  A run with no
// progress for `hard_ticks` while a side still runs is a fault of this
// harness, and the test aborts with a diagnostic.
outcome run_pair(std::span<const step> left, std::span<const step> right, std::size_t capacity) {
    bounded_queue left_to_right{capacity};
    bounded_queue right_to_left{capacity};
    std::atomic<std::uint64_t> progress{0};
    std::atomic<bool> stop{false};
    std::atomic<int> finished{0};
    std::atomic<bool> wrong{false};
    std::array<std::atomic<side>, 2> sides{};
    const auto play = [&](std::span<const step> script, bounded_queue& out, bounded_queue& in,
                          std::atomic<side>& state) {
        for (const step& action : script) {
            state.store(side::waiting, std::memory_order_release);
            if (action.is_send) {
                while (!out.try_push(action.message)) {
                    if (stop.load(std::memory_order_acquire)) return;
                    std::this_thread::yield();
                }
            } else {
                int value = 0;
                while (!in.try_pop(value)) {
                    if (stop.load(std::memory_order_acquire)) return;
                    std::this_thread::yield();
                }
                if (value != action.message) wrong.store(true, std::memory_order_release);
            }
            state.store(side::running, std::memory_order_release);
            progress.fetch_add(1, std::memory_order_acq_rel);
        }
        state.store(side::finished, std::memory_order_release);
        finished.fetch_add(1, std::memory_order_acq_rel);
    };
    bool is_deadlocked = false;
    {
        std::jthread first{[&] { play(left, left_to_right, right_to_left, sides[0]); }};
        std::jthread second{[&] { play(right, right_to_left, left_to_right, sides[1]); }};
        constexpr auto tick = std::chrono::milliseconds{5};
        constexpr int stall_ticks = 40;
        constexpr int hard_ticks = 2000;
        std::uint64_t last = progress.load(std::memory_order_acquire);
        int quiet = 0;
        while (finished.load(std::memory_order_acquire) < 2) {
            std::this_thread::sleep_for(tick);
            const std::uint64_t now = progress.load(std::memory_order_acquire);
            quiet = now == last ? quiet + 1 : 0;
            last = now;
            const bool is_blocked = sides[0].load(std::memory_order_acquire) != side::running
                                    && sides[1].load(std::memory_order_acquire) != side::running;
            if (is_blocked && quiet >= stall_ticks) {
                is_deadlocked = true;
                stop.store(true, std::memory_order_release);
                break;
            }
            if (quiet >= hard_ticks) {
                std::fprintf(stderr, "test_session_subtype_attack: a run made no progress for %d ticks while a side "
                             "still ran; the harness is wrong\n", hard_ticks);
                std::abort();
            }
        }
    }
    if (is_deadlocked) return outcome::deadlocked;
    if (wrong.load(std::memory_order_acquire)) return outcome::wrong_message;
    if (!left_to_right.is_empty() || !right_to_left.is_empty()) return outcome::orphan;
    return outcome::completed;
}

template <class Sub, class Super>
outcome run_against_dual(std::size_t capacity) {
    return run_pair(script_v<Sub>, script_v<s::dual_of_t<Super>>, capacity);
}

int failures = 0;

void expect(bool condition, std::string_view what) {
    if (condition) return;
    std::fprintf(stderr, "test_session_subtype_attack: %.*s\n", static_cast<int>(what.size()), what.data());
    ++failures;
}

// The check and the runtime agree on each member of the family, for
// every capacity from 1 to 4: the check admits exactly the runs that
// complete.
template <std::size_t K>
void family_agrees_with_runtime() {
    for (std::size_t capacity = 1; capacity <= 4; ++capacity) {
        const outcome observed = run_against_dual<early<K>, late<K>>(capacity);
        const bool admitted = capacity >= K;
        const bool completed = observed == outcome::completed;
        if (admitted != completed) {
            std::fprintf(stderr, "family K=%zu capacity=%zu: the check %s, the run %.*s\n", K, capacity,
                         admitted ? "admits" : "refuses", static_cast<int>(outcome_name(observed).size()),
                         outcome_name(observed).data());
            ++failures;
        }
    }
}

// ── Recursion games ──────────────────────────────────────────────────

// A nested Loop shadows the outer one, so its Continue binds the inner
// loop.  One send followed by receives for ever is not a loop of
// send-receive pairs.
using Shadowed = Loop<Send<int, Loop<Recv<int, Continue>>>>;
using Alternating = Loop<Send<int, Recv<int, Continue>>>;
static_assert(!s::is_subtype_sync_v<Shadowed, Alternating> && !s::is_subtype_sync_v<Alternating, Shadowed>);
static_assert(s::equivalent_sync_v<Shadowed, Send<int, Loop<Recv<int, Continue>>>>,
              "an outer loop that never loops is its body");
static_assert(s::equivalent_sync_v<Loop<Loop<Send<int, Continue>>>, Loop<Send<int, Continue>>>);

// A Continue inside a branch.
using Exit = Loop<Select<Send<int, Continue>, End>>;
static_assert(s::equivalent_sync_v<Exit, Select<Send<int, Exit>, End>>, "one unfold of the loop");
static_assert(s::is_subtype_sync_v<Loop<Select<Send<int, Continue>>>, Exit>);

// A loop against the same loop unfolded three times.
static_assert(s::equivalent_sync_v<Loop<Send<A, Send<A, Send<A, Continue>>>>, Loop<Send<A, Continue>>>);

// A loop is not a finite prefix of itself.
static_assert(!s::is_subtype_sync_v<Loop<Send<A, Continue>>, Send<A, Send<A, End>>>);

// The unguarded loops are refused before any relation reads them.
static_assert(!s::is_well_formed_v<Loop<Continue>>);
static_assert(!s::is_well_formed_v<Loop<s::VendorPinned<s::VendorBackend::NV, Continue>>>);
static_assert(!s::is_well_formed_v<Loop<Send<int, Loop<Continue>>>>);
static_assert(s::subtype_mismatch_v<Loop<Continue>, Loop<Continue>> == tr::mismatch::ill_formed);

// A generated family: each loop body B, with Continue at the leaves,
// refines its one-step unfold and the unfold refines it.

constexpr std::meta::info send_shape = ^^s::Send;
constexpr std::meta::info recv_shape = ^^s::Recv;
constexpr std::meta::info select_shape = ^^s::Select;
constexpr std::meta::info loop_shape = ^^s::Loop;

consteval std::meta::info replace_continue(std::meta::info type, std::meta::info with) {
    const std::meta::info plain = std::meta::dealias(type);
    if (plain == ^^s::Continue) return with;
    if (!std::meta::has_template_arguments(plain)) return plain;
    const std::meta::info shape = std::meta::template_of(plain);
    if (shape == loop_shape) return plain;
    std::vector<std::meta::info> arguments = std::meta::template_arguments_of(plain);
    if (shape == send_shape || shape == recv_shape) {
        arguments[1] = replace_continue(arguments[1], with);
    } else {
        for (std::meta::info& branch : arguments) branch = replace_continue(branch, with);
    }
    return std::meta::substitute(shape, arguments);
}

struct lcg {
    std::uint64_t state = 0x2545f4914f6cdd1dULL;
    consteval std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    }
    consteval std::size_t below(std::size_t bound) { return next() % bound; }
};

consteval std::meta::info body(lcg& random, std::size_t depth) {
    if (depth == 0) return random.below(3) == 0 ? ^^s::End : ^^s::Continue;
    switch (random.below(3)) {
        case 0:
            return std::meta::substitute(send_shape, {^^A, body(random, depth - 1)});
        case 1:
            return std::meta::substitute(recv_shape, {^^B, body(random, depth - 1)});
        default:
            return std::meta::substitute(select_shape, {std::meta::substitute(send_shape, {^^A, body(random, depth - 1)}),
                                                        std::meta::substitute(send_shape, {^^B, body(random, depth - 1)})});
    }
}

consteval bool equivalent(std::meta::info left, std::meta::info right) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::equivalent_sync_v, {left, right}));
}

consteval std::size_t unfold_laws_holding() {
    lcg random{};
    std::size_t holding = 0;
    for (std::size_t round = 0; round < 12; ++round) {
        // A guard first, so the loop is well-formed.
        const std::meta::info guarded = std::meta::substitute(send_shape, {^^A, body(random, 3)});
        const std::meta::info loop = std::meta::substitute(loop_shape, {guarded});
        const std::meta::info unfolded = replace_continue(guarded, loop);
        const std::meta::info nested = std::meta::substitute(loop_shape, {loop});
        if (equivalent(loop, unfolded) && equivalent(loop, nested)) ++holding;
    }
    return holding;
}
static_assert(unfold_laws_holding() == 12, "a loop equals its unfold and a loop wrapped in a loop");

// ── Generated anticipations ──────────────────────────────────────────
//
// Each case takes a finite protocol and mutates it: it swaps two
// adjacent actions, or it changes the message of one action.  A swap of
// a receive and a later send is the move that asynchronous subtyping
// admits.  Every other swap and every message change must be refused.
// The check answers for capacities 1 to 4, and main runs each admitted
// pair against the dual of the supertype on a channel of that capacity.
// A run that deadlocks, receives a wrong message or leaves a message in
// a buffer is an unsound answer.  The seed is fixed.

inline constexpr std::size_t max_script = 8;
inline constexpr std::size_t generated_count = 40;
inline constexpr std::size_t checked_capacity = 4;

struct generated_case {
    std::array<step, max_script> sub{};
    std::size_t sub_length = 0;
    std::array<step, max_script> peer{};
    std::size_t peer_length = 0;
    std::uint8_t admitted = 0;
    bool is_synchronous = false;
};

// True when the check admits the case at this capacity, 1 to 4.
constexpr bool admits_at(const generated_case& item, std::size_t capacity) {
    return ((static_cast<unsigned>(item.admitted) >> (capacity - 1)) & 1U) != 0;
}

consteval std::meta::info protocol_of(const std::vector<step>& actions) {
    std::meta::info protocol = ^^s::End;
    for (std::size_t index = actions.size(); index-- > 0;) {
        const std::meta::info message = actions[index].message == 1 ? ^^A : ^^B;
        protocol = std::meta::substitute(actions[index].is_send ? send_shape : recv_shape, {message, protocol});
    }
    return protocol;
}

consteval generated_case make_case(lcg& random) {
    std::vector<step> super_actions;
    const std::size_t length = 2 + random.below(5);
    for (std::size_t index = 0; index < length; ++index) {
        super_actions.push_back(step{random.below(2) == 0, 1 + static_cast<int>(random.below(2))});
    }
    std::vector<step> sub_actions = super_actions;
    const std::size_t mutations = 1 + random.below(3);
    for (std::size_t round = 0; round < mutations; ++round) {
        const std::size_t at = random.below(sub_actions.size() - 1);
        if (random.below(10) == 0) {
            sub_actions[at].message = 3 - sub_actions[at].message;
        } else {
            const step moved = sub_actions[at];
            sub_actions[at] = sub_actions[at + 1];
            sub_actions[at + 1] = moved;
        }
    }
    const std::meta::info sub = protocol_of(sub_actions);
    const std::meta::info super = protocol_of(super_actions);
    const std::vector<step> peer = script_steps(std::meta::dealias(std::meta::substitute(^^s::dual_of_t, {super})));
    generated_case out{};
    for (std::size_t index = 0; index < sub_actions.size(); ++index) out.sub[index] = sub_actions[index];
    out.sub_length = sub_actions.size();
    for (std::size_t index = 0; index < peer.size(); ++index) out.peer[index] = peer[index];
    out.peer_length = peer.size();
    for (std::size_t capacity = 1; capacity <= checked_capacity; ++capacity) {
        const bool admits = std::meta::extract<bool>(
            std::meta::substitute(^^s::is_subtype_async_v, {sub, super, std::meta::reflect_constant(capacity)}));
        if (admits) out.admitted = static_cast<std::uint8_t>(out.admitted | (1U << (capacity - 1)));
    }
    out.is_synchronous = std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_sync_v, {sub, super}));
    return out;
}

consteval std::vector<generated_case> make_cases() {
    lcg random{0x9e3779b97f4a7c15ULL};
    std::vector<generated_case> cases;
    for (std::size_t index = 0; index < generated_count; ++index) cases.push_back(make_case(random));
    return cases;
}

inline constexpr std::span<const generated_case> generated_cases = std::define_static_array(make_cases());

consteval std::size_t admitted_pairs() {
    std::size_t count = 0;
    for (const generated_case& item : generated_cases) count += static_cast<std::size_t>(std::popcount(item.admitted));
    return count;
}
consteval std::size_t refused_cases() {
    std::size_t count = 0;
    for (const generated_case& item : generated_cases) count += item.admitted == 0 ? 1 : 0;
    return count;
}
static_assert(generated_cases.size() == generated_count);
static_assert(admitted_pairs() > 0 && refused_cases() > 0,
              "the family must hold admitted pairs and refused cases, or it proves nothing about either");

// The check is monotone in the capacity: a pair it admits at capacity C
// it admits at each capacity above C.
consteval bool admitted_is_monotone() {
    for (const generated_case& item : generated_cases) {
        for (std::size_t capacity = 1; capacity < checked_capacity; ++capacity) {
            if (admits_at(item, capacity) && !admits_at(item, capacity + 1)) return false;
        }
    }
    return true;
}
static_assert(admitted_is_monotone());

struct generated_tally {
    std::size_t sound_runs = 0;
    std::size_t unsound_runs = 0;
    std::size_t refused_but_completes = 0;
};

// Runs every admitted pair.  A case that the check refuses at every
// capacity is run one time at the largest capacity, and a run that then
// completes counts as a pair the bounded check cannot prove.
generated_tally run_generated() {
    generated_tally tally{};
    for (std::size_t index = 0; index < generated_cases.size(); ++index) {
        const generated_case& item = generated_cases[index];
        const std::span<const step> sub{item.sub.data(), item.sub_length};
        const std::span<const step> peer{item.peer.data(), item.peer_length};
        for (std::size_t capacity = 1; capacity <= checked_capacity; ++capacity) {
            if (!admits_at(item, capacity)) continue;
            const outcome observed = run_pair(sub, peer, capacity);
            if (observed == outcome::completed) {
                ++tally.sound_runs;
            } else {
                ++tally.unsound_runs;
                std::fprintf(stderr, "generated case %zu at capacity %zu: the check admits, the run %.*s\n", index,
                             capacity, static_cast<int>(outcome_name(observed).size()), outcome_name(observed).data());
            }
        }
        if (item.is_synchronous && run_pair(sub, peer, 1) != outcome::completed) {
            ++tally.unsound_runs;
            std::fprintf(stderr, "generated case %zu: the synchronous relation admits, the run fails\n", index);
        }
        if (item.admitted == 0 && run_pair(sub, peer, checked_capacity) == outcome::completed) {
            ++tally.refused_but_completes;
        }
    }
    return tally;
}

// ── Registration attacks ─────────────────────────────────────────────
//
// A combinator whose dual is not an involution, whose variance does not
// flip, or whose variance flips with each side backwards, is refused
// where a query first meets it.  The negative fixtures
// neg_sess_incoherent_registration_* are those attacks.  The backwards
// pair compiled before the repair, and subtyping then put a bare int on
// the wire where the peer receives a positive int.  Here,
// a coherent combinator registered by a different header works at once
// and at every depth, and one that the fold does not know is refused.

template <class T, class K>
struct Offload {};
template <class T, class K>
struct Onload {};

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::combinator offload{
    .shape = ^^::Offload,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::output,
    .dual = ^^::Onload,
    .payload_variance = ::foundation::algebra::transition::variance::covariant};
inline constexpr ::foundation::algebra::transition::combinator onload{
    .shape = ^^::Onload,
    .kind = ::foundation::algebra::transition::shape_kind::step,
    .direction = ::foundation::algebra::transition::polarity::input,
    .dual = ^^::Offload,
    .payload_variance = ::foundation::algebra::transition::variance::contravariant};
}  // namespace fixy::session::combinators

namespace {

static_assert(std::is_same_v<s::dual_of_t<Send<int, Offload<A, End>>>, Recv<int, Onload<A, End>>>);
static_assert(s::is_well_formed_v<Loop<Offload<A, Continue>>>);
static_assert(s::is_subtype_sync_v<Offload<::fixy::Refined<::fixy::positive, int>, End>, Offload<int, End>>);
static_assert(!s::is_subtype_sync_v<Offload<int, End>, Send<int, End>>, "a new combinator is its own shape");

// ── Empty choices ────────────────────────────────────────────────────
//
// Under the branch rule an empty Select refines every Select, and a
// substitute of that type never sends.  Each route to one is refused:
// at the top, below a step, inside a loop, as a note with no branch, and
// as a choice of crash branches only.

static_assert(!s::is_subtype_sync_v<Select<>, Select<Send<A, End>>>);
static_assert(!s::is_subtype_async_v<Select<>, Select<Send<A, End>>, 4>);
static_assert(!s::is_subtype_sync_v<Send<A, Select<>>, Send<A, Select<Send<B, End>>>>);
static_assert(!s::is_subtype_sync_v<Loop<Select<Send<A, Continue>, Select<>>>, Loop<Select<Send<A, Continue>>>>);
static_assert(!s::is_subtype_sync_v<Offer<Sender<Bob>>, Offer<Sender<Bob>>>);
static_assert(!s::is_subtype_sync_v<Offer<Recv<s::Crash<Bob>, End>>, Offer<Recv<s::Crash<Bob>, End>>>);
static_assert(s::is_subtype_sync_v<Select<Send<A, End>>, Select<Send<A, End>, Send<B, End>>>,
              "a choice of one branch is well-formed");

// ── Labels ───────────────────────────────────────────────────────────
//
// Two branches that name one label are refused, also through two
// aliases of the label.  Two labels that are distinct types with the
// same name are two labels: identity is by type, never by spelling.

struct L0 {};
struct L1 {};
using L0Again = L0;
namespace first {
struct Hello {};
}  // namespace first
namespace second {
struct Hello {};
}  // namespace second

static_assert(!s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, L0, int>, End>, Send<s::PeerMsg<Bob, L0Again, int>, End>>>,
              "one label through two aliases");
static_assert(std::meta::identifier_of(^^first::Hello) == std::meta::identifier_of(^^second::Hello));
static_assert(s::is_well_formed_v<Select<Send<s::PeerMsg<Bob, first::Hello, int>, End>,
                                         Send<s::PeerMsg<Bob, second::Hello, int>, End>>>,
              "two labels with one name are two labels");
static_assert(!s::is_subtype_sync_v<Select<Send<s::PeerMsg<Bob, first::Hello, int>, End>>,
                                    Select<Send<s::PeerMsg<Bob, second::Hello, int>, End>>>,
              "a label is not replaced by a label with the same name");
// A label inside a payload is data, not a label.  The branch names no
// label, so it cannot clash, and its position is still its wire label.
static_assert(s::is_well_formed_v<Select<Send<std::pair<s::PeerMsg<Bob, L0, int>, int>, End>,
                                         Send<s::PeerMsg<Bob, L0, int>, End>>>);

// Crash branches pair by payload.  A crash branch hidden under a wrapper
// or a loop at the head of a branch is still a crash branch.
using BobCrash = Recv<s::Crash<Bob>, End>;
using AliceCrash = Recv<s::Crash<Alice>, End>;
static_assert(!s::is_well_formed_v<Offer<s::VendorPinned<s::VendorBackend::NV, BobCrash>, Recv<A, End>>>,
              "a wrapped crash branch before a message branch");
static_assert(!s::is_well_formed_v<Offer<Loop<Recv<s::Crash<Bob>, Recv<A, Continue>>>, Recv<A, End>>>,
              "a crash branch under a loop before a message branch");
static_assert(!s::is_subtype_sync_v<Offer<Recv<A, End>, AliceCrash>, Offer<Recv<A, End>, BobCrash>>,
              "a crash branch for one peer does not stand for a crash branch for another");
static_assert(s::is_subtype_sync_v<Offer<Recv<A, End>, Recv<B, End>, BobCrash>, Offer<Recv<A, End>, BobCrash>>);

// A subtype cannot drop an exit branch that stands before another
// branch: the positions shift, and the pair at the old position differs.
static_assert(!s::is_subtype_sync_v<Loop<Select<Send<A, Continue>>>, Loop<Select<Send<B, End>, Send<A, Continue>>>>);

// ── The wire label of a branch is its position ───────────────────────
//
// select<I>() sends I, and the Offer of the peer dispatches on I.  So a
// Select that names the same labels in another order is another
// protocol, and the relation refuses it.  The run below shows the cost
// of an acceptance: the two ends take branches with different labels,
// and no check on the wire notices, because a label is a type.

using Projected = Select<Send<s::PeerMsg<Bob, L0, int>, End>, Send<s::PeerMsg<Bob, L1, int>, End>>;
using Permuted = Select<Send<s::PeerMsg<Bob, L1, int>, End>, Send<s::PeerMsg<Bob, L0, int>, End>>;
static_assert(!s::is_subtype_sync_v<Permuted, Projected> && !s::is_subtype_async_v<Permuted, Projected, 4>);

struct IndexWire {
    std::size_t index = 0;
};
struct PickerEnd {
    IndexWire* wire = nullptr;
};
struct OffererEnd {
    IndexWire* wire = nullptr;
};

template <class Message>
inline constexpr int label_number_v = std::is_same_v<typename Message::label, L0> ? 0 : 1;

// Returns the label that the picker sent and the label that the offerer
// received, when the picker speaks Permuted and the offerer speaks the
// dual of Projected.
[[nodiscard]] std::pair<int, int> labels_on_an_index_wire() {
    IndexWire wire{};
    auto picker = s::mint_session_handle<Permuted>(PickerEnd{&wire});
    auto chosen =
        std::move(picker).template select<0>([](PickerEnd& end, std::size_t index) noexcept { end.wire->index = index; });
    using Sent = typename decltype(chosen)::message_type;
    auto picker_done = std::move(chosen).send(Sent{}, [](PickerEnd&, Sent&&) noexcept {});
    (void)std::move(picker_done).close();

    int received = -1;
    auto offerer = s::mint_session_handle<s::dual_of_t<Projected>>(OffererEnd{&wire});
    std::move(offerer).branch([](OffererEnd& end) noexcept { return end.wire->index; },
                              [&received](auto handle) noexcept {
                                  using Got = typename decltype(handle)::message_type;
                                  received = label_number_v<Got>;
                                  auto [message, done] = std::move(handle).recv([](OffererEnd&) noexcept { return Got{}; });
                                  (void)message;
                                  (void)std::move(done).close();
                              });
    return {label_number_v<Sent>, received};
}

// ── Fuel ─────────────────────────────────────────────────────────────
//
// A pair from the differential corpus whose bounded search, without
// fuel, exceeds the constexpr operation limit of the build.  With fuel
// the check answers "not proven" well inside the limit.  An answer that
// fuel cuts short is a refusal, never an acceptance, because each rule of
// the search is a conjunction or a disjunction of its sub-results, and
// an exhausted sub-search is false.

struct Nat {};
struct Bool {};
using HardSub = Loop<Offer<
    Select<Offer<Loop<Recv<Nat, End>>, Recv<Bool, Continue>>, Send<Nat, Send<Bool, Continue>>>,
    Select<Offer<End, Continue, Send<Bool, Continue>>, Continue, Send<Bool, Continue>>,
    Send<Bool, Recv<Bool, Recv<Nat, Continue>>>>>;
using HardSuper = Loop<Offer<
    Select<Offer<Loop<Recv<Bool, End>>, Recv<Bool, Continue>>, Send<Nat, Send<Bool, Continue>>>,
    Select<Offer<End, Continue, Send<Bool, Continue>>, Continue, Send<Bool, Continue>>,
    Send<Bool, Recv<Bool, Recv<Nat, Continue>>>>>;
static_assert(!s::is_subtype_async_v<HardSub, HardSuper, 3> && !s::is_subtype_async_v<HardSuper, HardSub, 3>);

// ── The known-limitation ledger ──────────────────────────────────────
//
// Each entry is an attack that succeeds, with the condition it breaks.
// The pin after each entry asserts that the attack still succeeds, so a
// repair fails the pin, and the entry and its pin go together.

struct limitation {
    std::string_view attack;
    std::string_view breaks;
};

inline constexpr limitation known_limitations[] = {
    {"duality drops the Sender note of an Offer, so a server speaking Select is compatible, as a server, with "
     "a client whose Offer names the wrong role; the client side of the same check refuses",
     "closure under duality holds only where duality is an involution, so compatibility is not symmetric "
     "(Padovani and Zavattaro, TOPLAS 2026, page 3)"},
    {"the capacity of the asynchronous check is a number the caller states, and nothing ties it to the "
     "channel the session runs on; a check at capacity 4 admits a pair that deadlocks on a channel of "
     "capacity 1",
     "the bound of the check must be the capacity of the channel (misc/session_types_literature.md rule 6)"},
    {"composition with a Continue suffix turns every End of a loop into a loop-back; the result is "
     "well-formed and can never end",
     "fair termination (Padovani and Zavattaro, TOPLAS 2026): no relation here preserves it"},
    {"subtyping removes the only exit branch of a loop: a loop that never ends refines a loop that can end",
     "fair subtyping (Padovani and Zavattaro, TOPLAS 2026), which refuses a subtype that loses termination"},
    {"a payload registration added after a query that read the same payload is not seen by the answers "
     "already computed, and nothing refuses the late registration",
     "the registration contract of foundation/algebra/Transition.h, which the build cannot enforce"},
};
static_assert(std::size(known_limitations) == 5, "the ledger only shrinks: lower this count when an entry goes");

// The implication chain was an entry here.  The implication relation of
// fixy/Refined.h is transitive.  The payload order now joins the two ends
// of a chain, and the order stays one way.
using Narrow = ::fixy::Refined<::fixy::in_range<5, 9>, int>;
using Middle = ::fixy::Refined<::fixy::bounded_above<9>, int>;
using Wide = ::fixy::Refined<::fixy::bounded_above<20>, int>;
static_assert(s::is_subtype_sync_v<Send<Narrow, End>, Send<Middle, End>>
              && s::is_subtype_sync_v<Send<Middle, End>, Send<Wide, End>>
              && s::is_subtype_sync_v<Send<Narrow, End>, Send<Wide, End>>);
static_assert(!s::is_subtype_sync_v<Send<Wide, End>, Send<Narrow, End>>
              && s::is_subtype_sync_v<Recv<Wide, End>, Recv<Narrow, End>>
              && !s::is_subtype_sync_v<Recv<Narrow, End>, Recv<Wide, End>>);

// Pin 1: the Sender note and compatibility.
using NotedClient = Offer<Sender<Bob>, Recv<A, End>>;
using PlainServer = Select<Send<A, End>>;
static_assert(s::CompatibleServer<PlainServer, NotedClient> && !s::CompatibleClient<NotedClient, PlainServer>);

// Pin 2: the capacity is the caller's word.  The runtime half is in main.
static_assert(s::is_subtype_async_v<early<4>, late<4>, 4>);

// Pin 3: composition with Continue removes every End.
using WithExit = Loop<Select<Send<A, Continue>, End>>;
using NoExit = s::compose_t<WithExit, Continue>;
static_assert(std::is_same_v<NoExit, Loop<Select<Send<A, Continue>, Continue>>> && s::is_well_formed_v<NoExit>);

// Pin 4: the exit is lost under subtyping.
static_assert(s::is_subtype_sync_v<Loop<Select<Send<A, Continue>>>, Loop<Select<Send<A, Continue>, Send<B, End>>>>);

// Pin 5: a late payload registration.  The query below reads the payload
// Late before any rule names it, so it answers that the send is
// well-formed.  A rule registered after it is not seen by that answer.
template <class T>
struct Late {};
static_assert(s::is_well_formed_v<Send<Late<int>, End>>);

}  // namespace

namespace fixy::session::combinators {
inline constexpr ::foundation::algebra::transition::payload_rule late_rule{
    .shape = ^^::Late, .is_sendable = false, .is_label = false};
}  // namespace fixy::session::combinators

namespace {
static_assert(s::is_well_formed_v<Send<Late<int>, End>>, "the stale answer the ledger entry names");
}  // namespace

int main() {
    // The runtime half of the capacity family: the check and the runs agree.
    family_agrees_with_runtime<1>();
    family_agrees_with_runtime<2>();
    family_agrees_with_runtime<3>();
    family_agrees_with_runtime<4>();

    // Every pair the synchronous relation holds runs to completion at
    // capacity 1, which is the claim that it needs no anticipation.
    expect(run_against_dual<late<3>, late<3>>(1) == outcome::completed, "a dual pair completes at capacity 1");
    expect(run_against_dual<early<2>, early<2>>(1) == outcome::completed,
           "a dual pair of anticipations completes at capacity 1");

    // Pin 2, runtime half: the pair the check admits at capacity 4
    // deadlocks on a channel of capacity 1, and the watchdog ends it.
    expect(run_against_dual<early<4>, late<4>>(1) == outcome::deadlocked,
           "the pinned deadlock of ledger entry 2 did not occur, and the entry can be stale");

    // A permuted Select on an index wire: the two ends disagree on the
    // label, which is why the relation refuses the permutation.
    const auto [sent, received] = labels_on_an_index_wire();
    expect(sent == 1 && received == 0,
           "a permuted Select on an index wire did not mis-route, so the refusal of a permutation is untested");

    // The generated family: every admitted pair runs to completion.
    const generated_tally tally = run_generated();
    expect(tally.unsound_runs == 0, "the asynchronous check admitted a pair that does not complete");
    expect(tally.sound_runs == admitted_pairs(), "a generated admitted pair did not run");
    std::printf("test_session_subtype_attack: %zu admitted runs completed, %zu unsound, %zu refused cases that "
                "complete at capacity %zu\n",
                tally.sound_runs, tally.unsound_runs, tally.refused_but_completes, checked_capacity);

    // A subtype with a narrower payload puts the same bytes on the wire
    // as the supertype payload, because each axiom keeps the
    // representation.  Checked on a value, not only on the size.
    const ::fixy::Refined<::fixy::positive, int> refined = ::fixy::mint_refined<::fixy::positive>(42);
    const int wire = std::bit_cast<int>(refined);
    expect(wire == 42, "a refined payload does not carry its value as the bare payload");
    const ::fixy::Tagged<int, tags::source::Sanitized> tagged = ::fixy::mint_tagged<tags::source::Sanitized>(7);
    expect(std::bit_cast<int>(tagged) == 7, "a tagged payload does not carry its value as the bare payload");

    return failures == 0 ? 0 : 1;
}
