// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing `Tagged<T, source::TransportPosture<
// TransportPostureTag::UnreliableMulticast>>` (gossip-fanout
// payload) to a function whose signature demands
// `Tagged<T, source::TransportPosture<TransportPostureTag::Reliable>>`
// (RPC payload).  Despite identical payload type T, the two
// TransportPosture instantiations are UNRELATED types — phantom
// Posture is a template non-type parameter, not a runtime field;
// the type system gives no implicit conversion across postures.
//
// FIXY-V-058 enrichment discipline (Tagged.h source::TransportPosture
// <T>):
//   TransportPosture<T> tags a value's TRANSIT history across the
//   CNT-P transport family (AfXdp / Mtls / Quic / Wireguard).  A
//   Reliable-posture sink (RPC, federation control) refuses an
//   UnreliableMulticast-posture payload (gossip fanout) by
//   construction — preserving the application's posture promise
//   end-to-end through the type system.
//
//   In production, this guards consumers whose correctness depends
//   on posture invariants:
//
//     void deliver_rpc_response(  // Reliable-only
//       Tagged<Payload, source::TransportPosture<Reliable>> p);
//     // deliver_rpc_response(gossip_msg);  // compile-time reject
//     auto rpc_ack = std::move(reliable_payload);
//     deliver_rpc_response(std::move(rpc_ack));  // OK
//
// HS14 — pairs with neg_forge_phase_cross_phase_misprovenance.cpp
// for the 2-fixture floor on V-058 source-tag enrichments:
//   1. ForgePhase cross-phase mis-provenance:   non-type-parameter
//      (char) tag-identity gate (peer fixture).
//   2. TransportPosture mismatch:                non-type-parameter
//      (enum class) tag-identity gate (THIS file).
// Together they pin both new tag families' phantom-non-type-parameter
// discipline at the same level Tagged-with-class-type tags get via
// neg_tagged_unrelated_source_mismatch.cpp.
//
// [GCC-WRAPPER-TEXT] — TransportPosture<Reliable> ≠
// TransportPosture<UnreliableMulticast>.

#include <crucible/safety/Tagged.h>

#include <cstdint>
#include <utility>

namespace {
    using ::crucible::safety::source::TransportPostureTag;

    // Reliable-only consumer — accepts payloads that crossed a
    // delivery-guaranteed transport.  In production this is e.g.
    // cntp/Raft.h's apply_committed_entry.
    [[maybe_unused]] void deliver_rpc_response(
        ::crucible::safety::Tagged<std::uint64_t,
                                   ::crucible::safety::source::TransportPosture<
                                       TransportPostureTag::Reliable>>
            /*payload_hash*/)
    {
        // body irrelevant — the call-site type-check IS the test.
    }
}

// Anchor: legitimate Reliable-posture call so the file is self-
// contained.  This call compiles.
[[maybe_unused]] static void anchor_reliable_call() {
    auto rpc = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::TransportPosture<
            TransportPostureTag::Reliable>,
        std::uint64_t>(0xFEEDFACEULL);
    deliver_rpc_response(std::move(rpc));
}

// VIOLATION: UnreliableMulticast-posture payload handed to a
// Reliable-only consumer.  Distinct template instantiations of
// TransportPosture — no implicit conversion.  GCC rejects with
// "cannot convert ... TransportPosture<(TransportPostureTag)3> ...
// TransportPosture<(TransportPostureTag)2>" or similar typed-
// argument mismatch.
[[maybe_unused]] static void offending_gossip_into_reliable_slot() {
    auto gossip = ::crucible::safety::mint_tagged<
        ::crucible::safety::source::TransportPosture<
            TransportPostureTag::UnreliableMulticast>,
        std::uint64_t>(0xFEEDFACEULL);
    deliver_rpc_response(std::move(gossip));  // ERROR: posture mismatch
}

int main() { return 0; }
