// Recursion games against the protocol algebra: shadowed loops, a
// Continue inside a branch, unfolds, unguarded loops, and a generated
// family of loops that equal their unfold.

#include "session_subtype_attack.h"

#include <cstddef>
#include <meta>
#include <vector>

namespace test_session_subtype_attack_types {

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
static_assert(s::subtype_mismatch_v<Loop<Select<Send<int, Continue>>>, Exit> == tr::mismatch::loses_termination,
              "a narrower Select that drops the only exit of the loop removes the exit");

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
        for (std::meta::info& branch : arguments)
            branch = replace_continue(branch, with);
    }
    return std::meta::substitute(shape, arguments);
}

consteval std::meta::info body(lcg& random, std::size_t depth) {
    if (depth == 0) return random.below(3) == 0 ? ^^s::End : ^^s::Continue;
    switch (random.below(3)) {
        case 0:
            return std::meta::substitute(send_shape, {^^A, body(random, depth - 1)});
        case 1:
            return std::meta::substitute(recv_shape, {^^B, body(random, depth - 1)});
        default:
            return std::meta::substitute(select_shape,
                                         {std::meta::substitute(send_shape, {^^A, body(random, depth - 1)}),
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

}  // namespace test_session_subtype_attack_types
