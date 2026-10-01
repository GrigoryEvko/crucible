#pragma once

// The shared part of test_session_subtype: the roles, the labels, the
// payload wrappers, the channel ends and the protocols that more than one
// group of checks names, and the generator of the protocols that the laws
// read.  test_session_subtype.cpp lists the source files of the test.

#include <fixy/session/Projection.h>
#include <fixy/session/Subtype.h>

#include <cstddef>
#include <cstdint>
#include <meta>
#include <vector>

namespace test_session_subtype_types {

namespace s = ::fixy::session;
namespace tr = ::foundation::algebra::transition;
namespace tags = ::fixy::tags;
using ::foundation::algebra::lattices::Tolerance;

struct Alice {};
struct Bob {};
struct PingReq {};
struct StopReq {};
struct Req {};
struct Resp {};
struct CloseCmd {};
struct Job {};

// The end of a channel that holds N messages in each direction.
template <std::size_t N>
struct Slots {
    static constexpr std::size_t channel_capacity = N;
};

using s::Continue;
using s::End;
using s::Loop;
using s::Offer;
using s::Recv;
using s::Select;
using s::Send;
using s::Sender;
using s::VendorPinned;
using s::VendorBackend;

using ::fixy::Refined;
using ::fixy::SealedRefined;
using ::fixy::Tagged;

// One step pinned to three vendors.  The relation group checks them, and
// the reason group reads two of them.
using NvSendInt = VendorPinned<VendorBackend::NV, Send<int, End>>;
using AmdSendInt = VendorPinned<VendorBackend::AMD, Send<int, End>>;
using PortableSendInt = VendorPinned<VendorBackend::Portable, Send<int, End>>;

// Two Selects and two Offers in the order of the relation.  The reason
// group derives the strict relation and the equivalence on them, and the
// asynchronous group checks that the asynchronous relation holds them.
using DS1 = Select<Send<PingReq, End>>;
using DS2 = Select<Send<PingReq, End>, Send<StopReq, End>>;
using DO1 = Offer<Recv<PingReq, End>, Recv<StopReq, End>>;
using DO2 = Offer<Recv<PingReq, End>>;

// ── Generated protocols ──────────────────────────────────────────────
//
// A linear congruential generator with a fixed seed builds protocols
// over a small payload alphabet.  `widen` builds a supertype by rules
// the relation must admit: a Send payload rises in the payload order, a
// Recv payload falls, a Select gains a branch, an Offer loses its last
// branch.  Each chain p0 ⩽ p1 ⩽ p2 then checks reflexivity,
// transitivity, closure under duality, the involution of duality, the
// well-formedness of each dual, and that the asynchronous relation
// holds each synchronous pair.
//
// test_session_subtype.cpp checks the laws of each chain.  The pairs of
// the closure under duality are in several source files, because they
// cost the most time: closure part k checks the pairs whose left protocol
// has an index from closure_part_begin(k) to closure_part_begin(k + 1).

struct lcg {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;
    consteval std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    }
    consteval std::size_t below(std::size_t bound) { return next() % bound; }
};

// Payloads in the order the chains move along: a Send payload moves one
// step up, a Recv payload one step down.
consteval std::vector<std::meta::info> payload_ladder() {
    return {^^Refined<::fixy::positive, int>, ^^Refined<::fixy::non_negative, int>, ^^int};
}

consteval std::size_t ladder_index(std::meta::info payload) {
    const std::vector<std::meta::info> ladder = payload_ladder();
    for (std::size_t index = 0; index < ladder.size(); ++index) {
        if (ladder[index] == payload) return index;
    }
    return ladder.size();
}

inline constexpr std::meta::info send_shape = ^^s::Send;
inline constexpr std::meta::info recv_shape = ^^s::Recv;
inline constexpr std::meta::info select_shape = ^^s::Select;
inline constexpr std::meta::info offer_shape = ^^s::Offer;
inline constexpr std::meta::info loop_shape = ^^s::Loop;

consteval std::meta::info generate(lcg& random, std::size_t depth, std::size_t loops, bool is_guarded) {
    const std::vector<std::meta::info> ladder = payload_ladder();
    if (depth == 0) {
        if (loops > 0 && is_guarded && random.below(2) == 0) return ^^s::Continue;
        return ^^s::End;
    }
    switch (random.below(6)) {
        case 0:
            return std::meta::substitute(
                send_shape, {ladder[random.below(ladder.size())], generate(random, depth - 1, loops, true)});
        case 1:
            return std::meta::substitute(
                recv_shape, {ladder[random.below(ladder.size())], generate(random, depth - 1, loops, true)});
        case 2: {
            std::vector<std::meta::info> branches;
            const std::size_t count = 1 + random.below(2);
            for (std::size_t index = 0; index < count; ++index)
                branches.push_back(generate(random, depth - 1, loops, true));
            return std::meta::substitute(select_shape, branches);
        }
        case 3: {
            std::vector<std::meta::info> branches;
            const std::size_t count = 1 + random.below(2);
            for (std::size_t index = 0; index < count; ++index)
                branches.push_back(generate(random, depth - 1, loops, true));
            return std::meta::substitute(offer_shape, branches);
        }
        case 4:
            return std::meta::substitute(
                loop_shape, {std::meta::substitute(send_shape, {^^int, generate(random, depth - 1, loops + 1, true)})});
        default:
            return generate(random, 0, loops, is_guarded);
    }
}

// A supertype: every rule here is one the relation admits.
consteval std::meta::info widen(std::meta::info type) {
    const std::meta::info plain = std::meta::dealias(type);
    if (!std::meta::has_template_arguments(plain)) return plain;
    const std::meta::info shape = std::meta::template_of(plain);
    std::vector<std::meta::info> arguments = std::meta::template_arguments_of(plain);
    const std::vector<std::meta::info> ladder = payload_ladder();
    if (shape == send_shape) {
        const std::size_t index = ladder_index(arguments[0]);
        if (index + 1 < ladder.size()) arguments[0] = ladder[index + 1];
        arguments[1] = widen(arguments[1]);
        return std::meta::substitute(send_shape, arguments);
    }
    if (shape == recv_shape) {
        const std::size_t index = ladder_index(arguments[0]);
        if (index > 0 && index < ladder.size()) arguments[0] = ladder[index - 1];
        arguments[1] = widen(arguments[1]);
        return std::meta::substitute(recv_shape, arguments);
    }
    if (shape == select_shape) {
        // The supertype gains a copy of the last branch.  A copy adds no
        // exit that the subtype lacks, so the pair keeps its exits.
        for (std::meta::info& branch : arguments)
            branch = widen(branch);
        arguments.push_back(arguments.back());
        return std::meta::substitute(select_shape, arguments);
    }
    if (shape == offer_shape) {
        if (arguments.size() > 1) arguments.pop_back();
        for (std::meta::info& branch : arguments)
            branch = widen(branch);
        return std::meta::substitute(offer_shape, arguments);
    }
    if (shape == loop_shape) return std::meta::substitute(loop_shape, {widen(arguments[0])});
    return plain;
}

consteval bool sync(std::meta::info sub, std::meta::info super) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_sync_v, {sub, super}));
}
// The refinement without its exit condition: the walk holds, or it holds
// up to exits.  This part is closed under duality.
consteval bool safe(std::meta::info sub, std::meta::info super) {
    return sync(sub, super)
        || std::meta::extract<tr::mismatch>(std::meta::substitute(^^s::subtype_mismatch_v, {sub, super}))
               == tr::mismatch::loses_termination;
}
consteval bool async_at_one(std::meta::info sub, std::meta::info super) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::is_subtype_async_v, {sub, super, ^^Slots<1>}));
}
consteval std::meta::info dual(std::meta::info type) {
    return std::meta::dealias(std::meta::substitute(^^s::dual_of_t, {type}));
}
consteval bool well_formed(std::meta::info type) {
    return std::meta::extract<bool>(std::meta::substitute(^^s::is_well_formed_v, {type}));
}

struct law_counts {
    std::size_t chains = 0;
    std::size_t reflexive = 0;
    std::size_t widened = 0;
    std::size_t transitive = 0;
    std::size_t dual_closed = 0;
    std::size_t involutive = 0;
    std::size_t dual_well_formed = 0;
    std::size_t async_contains_sync = 0;
    std::size_t pair_closure = 0;
    std::size_t pairs = 0;
};

inline constexpr std::size_t generated_chains = 16;

// The protocols of one generated chain: p1 widens p0, and p2 widens p1.
struct generated_chain {
    std::meta::info p0;
    std::meta::info p1;
    std::meta::info p2;
};

// The chains, in the order of the fixed seed.
consteval std::vector<generated_chain> generate_chains() {
    lcg random{};
    std::vector<generated_chain> chains;
    for (std::size_t chain = 0; chain < generated_chains; ++chain) {
        const std::meta::info p0 = generate(random, 4, 0, true);
        const std::meta::info p1 = widen(p0);
        chains.push_back(generated_chain{p0, p1, widen(p1)});
    }
    return chains;
}

// The laws of each chain.  The two pair counts stay zero.
consteval law_counts check_chain_laws() {
    law_counts counts{};
    for (const generated_chain& chain : generate_chains()) {
        const std::meta::info p0 = chain.p0;
        const std::meta::info p1 = chain.p1;
        const std::meta::info p2 = chain.p2;
        ++counts.chains;
        if (sync(p0, p0) && sync(p1, p1) && sync(p2, p2)) ++counts.reflexive;
        if (sync(p0, p1) && sync(p1, p2)) ++counts.widened;
        if (!sync(p0, p1) || !sync(p1, p2) || sync(p0, p2)) ++counts.transitive;
        if (safe(dual(p1), dual(p0)) && safe(dual(p2), dual(p1)) && safe(dual(p2), dual(p0))) ++counts.dual_closed;
        if (dual(dual(p0)) == p0 && dual(dual(p2)) == p2) ++counts.involutive;
        if (well_formed(p0) && well_formed(dual(p0)) && well_formed(dual(p2))) ++counts.dual_well_formed;
        if (async_at_one(p0, p1) && async_at_one(p1, p2) && async_at_one(p0, p0)) ++counts.async_contains_sync;
    }
    return counts;
}

// The closure pairs read p0 and p2 of each chain, in the order of the
// seed, against each other.
inline constexpr std::size_t generated_protocols = 2 * generated_chains;
inline constexpr std::size_t closure_parts = 3;

// The index of the first left protocol of closure part `part`.  Part
// closure_parts gives the end of the last part.
consteval std::size_t closure_part_begin(std::size_t part) { return part * generated_protocols / closure_parts; }

// The closure under duality of the relation up to exits, on each pair
// whose left protocol has an index from closure_part_begin(part) to
// closure_part_begin(part + 1).  The chain counts stay zero.
// Complexity: generated_protocols pairs for each left protocol.
consteval law_counts check_pair_closure(std::size_t part) {
    std::vector<std::meta::info> seen;
    for (const generated_chain& chain : generate_chains()) {
        seen.push_back(chain.p0);
        seen.push_back(chain.p2);
    }
    law_counts counts{};
    for (std::size_t index = closure_part_begin(part); index < closure_part_begin(part + 1); ++index) {
        const std::meta::info left = seen[index];
        for (const std::meta::info right : seen) {
            ++counts.pairs;
            if (safe(left, right) == safe(dual(right), dual(left))) ++counts.pair_closure;
        }
    }
    return counts;
}

// The counts of each closure part, which main adds up.
law_counts closure_part_0();
law_counts closure_part_1();
law_counts closure_part_2();

}  // namespace test_session_subtype_types
