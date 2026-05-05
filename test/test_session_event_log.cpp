// Runtime + compile-time harness for safety/SessionEventLog.h and
// safety/RecordingSessionHandle.h (task #404, SAFEINT-B15 from
// misc/24_04_2026_safety_integration.md §15).
//
// Coverage:
//   * Compile-time: SessionEvent layout invariants, strong-ID type
//     safety, default_schema_hash distinguishes types, SessionEventLog
//     is Pinned, OrderedAppendOnly contract pulled in via Mutation.h.
//   * Runtime: SessionEventLog records + iterates; AtomicMonotonic
//     step counter is monotonic; manual record_now stamps the session.
//   * Runtime: RecordingSessionHandle wraps Send/Recv/Select/Offer/End
//     and records each operation in order.  Worked example: a small
//     request/reply protocol driven through both sides with a single
//     event log capturing the full bilateral trace.
//   * Worked: replay-determinism property — driving the same protocol
//     twice produces step_id-shifted but otherwise identical event
//     sequences.

#include <crucible/bridges/RecordingSessionHandle.h>
#include <crucible/sessions/SessionEventLog.h>

#include <cstring>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace {

using namespace crucible::safety::proto;

// ── Compile-time witnesses ─────────────────────────────────────────

static_assert(sizeof(SessionEvent) == 56);
static_assert(std::is_trivially_copyable_v<SessionEvent>);
static_assert(session_op_name(SessionOp::Stop) == std::string_view{"Stop"});
static_assert(session_op_name(SessionOp::Checkpoint_Base) ==
              std::string_view{"Checkpoint_Base"});
static_assert(session_op_name(SessionOp::Checkpoint_Rollback) ==
              std::string_view{"Checkpoint_Rollback"});

static_assert(default_schema_hash<int>          != default_schema_hash<long>);
static_assert(default_schema_hash<int>          == default_schema_hash<int>);

static_assert(!std::is_copy_constructible_v<SessionEventLog>);
static_assert(!std::is_move_constructible_v<SessionEventLog>);

// Strong-ID guards: distinct types not interconvertible.
static_assert(!std::is_convertible_v<SessionTagId, RoleTagId>);
static_assert(!std::is_convertible_v<RoleTagId,    SchemaHash>);
static_assert(!std::is_convertible_v<StepId,       PayloadHash>);

// ── Test: SessionEventLog basic record + iterate ───────────────────

int run_log_basic() {
    SessionEventLog log{SessionTagId{99}};
    if (!log.empty())                     return 1;
    if (log.session().value != 99u)       return 2;

    log.record_now(SessionEvent{
        .from_role = RoleTagId{1},
        .to_role   = RoleTagId{2},
        .op        = SessionOp::Send,
    });
    log.record_now(SessionEvent{
        .from_role = RoleTagId{2},
        .to_role   = RoleTagId{1},
        .op        = SessionOp::Recv,
    });
    log.record_now(SessionEvent{
        .from_role = RoleTagId{1},
        .to_role   = RoleTagId{2},
        .op        = SessionOp::Close,
    });

    if (log.size() != 3)                                  return 3;
    if (log[0].op != SessionOp::Send)                     return 4;
    if (log[1].op != SessionOp::Recv)                     return 5;
    if (log[2].op != SessionOp::Close)                    return 6;

    // Step IDs strictly increasing.
    if (!(log[0].step_id.value < log[1].step_id.value))   return 7;
    if (!(log[1].step_id.value < log[2].step_id.value))   return 8;

    // Session ID stamped automatically by record_now.
    if (log[0].session.value != 99u)                       return 9;
    if (log[1].session.value != 99u)                       return 10;
    if (log[2].session.value != 99u)                       return 11;

    return 0;
}

// ── Test: monotonic step counter ───────────────────────────────────

int run_step_counter_monotone() {
    SessionEventLog log{};
    StepId prev{};
    for (int i = 0; i < 100; ++i) {
        StepId s = log.next_step();
        if (!(prev.value < s.value)) return 1;
        prev = s;
    }
    return 0;
}

// ── Test: manual record (no auto-stamp) preserves caller's step ───

int run_manual_record_preserves_stepid() {
    SessionEventLog log{SessionTagId{7}};
    SessionEvent ev{};
    ev.step_id   = StepId{42};
    ev.session   = SessionTagId{7};
    ev.op        = SessionOp::Send;
    log.record(ev);
    if (log[0].step_id.value != 42u)  return 1;
    if (log[0].session.value != 7u)   return 2;
    return 0;
}

// ── Test: drain consumes the log, leaves empty ─────────────────────

int run_drain_yields_storage() {
    SessionEventLog log{SessionTagId{1}};
    log.record_now(SessionEvent{.op = SessionOp::Send});
    log.record_now(SessionEvent{.op = SessionOp::Recv});
    log.record_now(SessionEvent{.op = SessionOp::Close});

    auto drained = std::move(log).drain();
    if (drained.size() != 3)                       return 1;
    if (drained[0].op != SessionOp::Send)          return 2;
    if (drained[2].op != SessionOp::Close)         return 3;
    return 0;
}

// ── Worked example: a request/reply protocol with both sides
//    recorded into the SAME log ─────────────────────────────────────

struct ReqMsg { int n; };
struct RepMsg { int n; };

struct WireBuf { std::deque<int>* bytes = nullptr; };

constexpr RoleTagId kClient{1};
constexpr RoleTagId kServer{2};

int run_request_reply_recorded() {
    std::deque<int> wire;
    WireBuf c_wire{&wire};
    WireBuf s_wire{&wire};

    using ClientProto = Send<ReqMsg, Recv<RepMsg, End>>;
    // ServerProto = dual_of_t<ClientProto> — implicit, used inside
    // mint_channel; not named here to keep -Werror=unused-local-typedefs happy.

    SessionEventLog log{SessionTagId{2026}};

    auto [client_bare, server_bare] =
        mint_channel<ClientProto>(std::move(c_wire), std::move(s_wire));

    auto client = mint_recording_session(std::move(client_bare), log, kClient, kServer);
    auto server = mint_recording_session(std::move(server_bare), log, kServer, kClient);

    // Client sends request.
    auto client_recv = std::move(client).send(
        ReqMsg{42},
        [](WireBuf& w, ReqMsg m) noexcept { w.bytes->push_back(m.n); });

    // Server recvs request, computes reply, sends.
    auto [req, server_send] = std::move(server).recv(
        [](WireBuf& w) noexcept -> ReqMsg {
            ReqMsg m{w.bytes->front()};
            w.bytes->pop_front();
            return m;
        });

    auto server_end = std::move(server_send).send(
        RepMsg{req.n * 2},
        [](WireBuf& w, RepMsg m) noexcept { w.bytes->push_back(m.n); });

    // Client recvs reply, both close.
    auto [rep, client_end] = std::move(client_recv).recv(
        [](WireBuf& w) noexcept -> RepMsg {
            RepMsg m{w.bytes->front()};
            w.bytes->pop_front();
            return m;
        });

    if (rep.n != 84) return 1;

    auto c_buf = std::move(client_end).close();
    auto s_buf = std::move(server_end).close();
    (void)c_buf; (void)s_buf;

    // Verify the bilateral log: 6 events in step-monotonic order:
    //   client.Send (req)
    //   server.Recv (req)
    //   server.Send (rep)
    //   client.Recv (rep)
    //   client.Close
    //   server.Close
    if (log.size() != 6)                                       return 2;

    // Check op + role on each entry.  Order-of-record is the order in
    // which the operations executed in the test driver above; that's
    // the ground truth.
    if (log[0].op != SessionOp::Send  || log[0].from_role != kClient) return 3;
    if (log[1].op != SessionOp::Recv  || log[1].to_role   != kServer) return 4;
    if (log[2].op != SessionOp::Send  || log[2].from_role != kServer) return 5;
    if (log[3].op != SessionOp::Recv  || log[3].to_role   != kClient) return 6;
    if (log[4].op != SessionOp::Close || log[4].from_role != kClient) return 7;
    if (log[5].op != SessionOp::Close || log[5].from_role != kServer) return 8;

    // Schema hashes capture the payload type so a deserialiser knows
    // what each event referred to.
    if (log[0].payload_schema != default_schema_hash<ReqMsg>) return 9;
    if (log[1].payload_schema != default_schema_hash<ReqMsg>) return 10;
    if (log[2].payload_schema != default_schema_hash<RepMsg>) return 11;
    if (log[3].payload_schema != default_schema_hash<RepMsg>) return 12;

    // Step-id monotonicity across both sides.
    for (std::size_t i = 1; i < log.size(); ++i) {
        if (!(log[i - 1].step_id.value < log[i].step_id.value)) return 13;
    }

    return 0;
}

// ── Worked: replay determinism property ────────────────────────────
//
// Driving the same protocol twice with identical inputs produces
// identical event sequences (modulo step_id offset).  This is the
// essence of bit-exact replay: the audit log fully captures the
// protocol shape.

int run_replay_determinism_property() {
    using P = Loop<Select<Send<int, Continue>, End>>;

    auto drive = [](SessionEventLog& log) {
        struct R { int sentinel = 0; };
        auto bare = mint_session_handle<P>(R{});
        auto rec  = mint_recording_session(std::move(bare), log, kClient, kServer);
        // pick<0> twice (Send branch), then pick<1> (End).
        auto h1 = std::move(rec).template select_local<0>();
        auto h2 = std::move(h1).send(11, [](R&, int) noexcept {});
        auto h3 = std::move(h2).template select_local<0>();
        auto h4 = std::move(h3).send(22, [](R&, int) noexcept {});
        auto h5 = std::move(h4).template select_local<1>();
        (void)std::move(h5).close();
    };

    SessionEventLog log_a{SessionTagId{1}};
    SessionEventLog log_b{SessionTagId{1}};
    drive(log_a);
    drive(log_b);

    if (log_a.size() != log_b.size())  return 1;
    for (std::size_t i = 0; i < log_a.size(); ++i) {
        // Step ids are per-log-instance; compare everything else.
        if (log_a[i].op             != log_b[i].op)             return 10 + int(i);
        if (log_a[i].branch_index   != log_b[i].branch_index)   return 20 + int(i);
        if (log_a[i].from_role      != log_b[i].from_role)      return 30 + int(i);
        if (log_a[i].to_role        != log_b[i].to_role)        return 40 + int(i);
        if (log_a[i].payload_schema != log_b[i].payload_schema) return 50 + int(i);
    }
    // Expected sequence:
    //   Select(0) — branch index 0 (Send)
    //   Send       — payload_schema = hash<int>
    //   Select(0)
    //   Send
    //   Select(1) — branch index 1 (End)
    //   Close
    if (log_a.size() != 6)                                          return 100;
    if (log_a[0].op != SessionOp::Select || log_a[0].branch_index != 0) return 101;
    if (log_a[1].op != SessionOp::Send)                             return 102;
    if (log_a[4].op != SessionOp::Select || log_a[4].branch_index != 1) return 103;
    if (log_a[5].op != SessionOp::Close)                            return 104;
    return 0;
}

// ── Worked: payload hashing opt-in via specialisation ──────────────
//
// Demonstrates the per-type hash override path.  Real production code
// for replay-strict audits supplies its own hasher; default is the
// PayloadHash{0} sentinel.

struct HashedPayload { int n; };

}  // anonymous namespace

// Specialise default_payload_hash_fn for HashedPayload — must be in
// the framework's namespace per the trait's declaration.
namespace crucible::safety::proto {

template <>
inline constexpr auto default_payload_hash_fn<HashedPayload> =
    [](const HashedPayload& p) noexcept -> PayloadHash {
        // Trivial hasher; real code uses Philox or FNV-1a.
        return PayloadHash{
            static_cast<uint64_t>(static_cast<int64_t>(p.n)) *
            uint64_t{0x9E3779B97F4A7C15ULL}};
    };

}  // namespace crucible::safety::proto

namespace {

int run_payload_hash_opt_in() {
    SessionEventLog log{SessionTagId{1}};

    using P = Send<HashedPayload, End>;
    struct R {};
    auto bare = mint_session_handle<P>(R{});
    auto rec  = mint_recording_session(std::move(bare), log, kClient, kServer);
    auto end  = std::move(rec).send(HashedPayload{7},
                                    [](R&, HashedPayload) noexcept {});
    (void)std::move(end).close();

    if (log.size() != 2)                                            return 1;
    // payload_hash for the HashedPayload Send is the user-specified
    // value — non-zero, deterministic.
    if (log[0].payload_hash.value == 0u)                            return 2;
    const uint64_t expected =
        static_cast<uint64_t>(int64_t{7}) * uint64_t{0x9E3779B97F4A7C15ULL};
    if (log[0].payload_hash.value != expected)                      return 3;
    // Close's payload hash remains the sentinel.
    if (log[1].payload_hash.value != 0u)                            return 4;
    return 0;
}

// ── Worked: Offer + branch() recording through the wrapper ─────────

int run_offer_branch_recorded() {
    using OfferProto  = Offer<Recv<int, End>, End>;
    using SelectProto = dual_of_t<OfferProto>;

    SessionEventLog log{SessionTagId{1}};

    std::deque<int> wire;
    WireBuf c_wire{&wire};
    WireBuf s_wire{&wire};

    auto [sel_bare, off_bare] =
        mint_channel<SelectProto>(std::move(c_wire), std::move(s_wire));

    auto sel = mint_recording_session(std::move(sel_bare), log, kClient, kServer);
    auto off = mint_recording_session(std::move(off_bare), log, kServer, kClient);

    // Client picks branch 0 (Send<int, End> on its side, Recv<int, End> on
    // server's side).  Sends 99, then closes.  Server branches on 0,
    // recvs, closes.
    auto sel_send = std::move(sel).template select<0>(
        [](WireBuf& w, std::size_t i) noexcept {
            w.bytes->push_back(static_cast<int>(i));
        });
    auto sel_end = std::move(sel_send).send(
        99, [](WireBuf& w, int v) noexcept { w.bytes->push_back(v); });

    int recv_value = 0;
    std::move(off).branch(
        [](WireBuf& w) noexcept -> std::size_t {
            std::size_t i = static_cast<std::size_t>(w.bytes->front());
            w.bytes->pop_front();
            return i;
        },
        [&recv_value](auto handle) {
            using H = decltype(handle);
            if constexpr (std::is_same_v<typename H::protocol,
                                          Recv<int, End>>) {
                auto [v, end_h] = std::move(handle).recv(
                    [](WireBuf& w) noexcept -> int {
                        int x = w.bytes->front(); w.bytes->pop_front();
                        return x;
                    });
                recv_value = v;
                (void)std::move(end_h).close();
            } else {
                (void)std::move(handle).close();
            }
        });

    (void)std::move(sel_end).close();

    if (recv_value != 99) return 1;

    // Expected event sequence in driver order:
    //   client.Select(0)
    //   client.Send (int)
    //   server.Offer(0)    — branch dispatched
    //   server.Recv (int)
    //   server.Close
    //   client.Close
    if (log.size() != 6)                                            return 2;
    if (log[0].op != SessionOp::Select || log[0].branch_index != 0) return 3;
    if (log[1].op != SessionOp::Send)                               return 4;
    if (log[2].op != SessionOp::Offer  || log[2].branch_index != 0) return 5;
    if (log[3].op != SessionOp::Recv)                               return 6;
    if (log[4].op != SessionOp::Close)                              return 7;
    if (log[5].op != SessionOp::Close)                              return 8;
    return 0;
}

// ── Worked: Stop event replay round-trip ───────────────────────────

int run_stop_event_replay_roundtrip() {
    SessionEventLog log{SessionTagId{303}};
    constexpr RecoveryPathHash kRecovery{0xBADC0FFEE0DDF00DULL};

    log.append_event(SessionEvent::stop(
        kServer, kClient, kClient,
        StopReasonKind::PeerCrashed,
        kRecovery));

    if (log.size() != 1)                                      return 1;

    auto replay = log.replay_iter();
    auto it = replay.begin();
    if (it == replay.end())                                   return 2;

    const SessionEvent replayed = *it;
    ++it;
    if (it != replay.end())                                   return 3;

    if (std::memcmp(&replayed, &log[0], sizeof(SessionEvent)) != 0) return 4;
    if (replayed.session.value != 303u)                       return 5;
    if (replayed.op != SessionOp::Stop)                       return 6;
    if (replayed.from_role != kServer)                        return 7;
    if (replayed.to_role != kClient)                          return 8;
    if (replayed.stop_peer_tag() != kClient)                  return 9;
    if (replayed.stop_reason_kind() != StopReasonKind::PeerCrashed) return 10;
    if (replayed.stop_recovery_path_hash() != kRecovery)      return 11;
    return 0;
}

// ── Worked: RecordingSessionHandle<Stop> emits Stop, not Close ─────

int run_recording_stop_close_recorded() {
    struct StopResource { int sentinel = 0; };

    SessionEventLog log{SessionTagId{404}};
    auto bare = mint_session_handle<Stop>(StopResource{17});
    auto rec = mint_recording_session(std::move(bare), log, kServer, kClient);

    StopResource resource = std::move(rec).close(
        StopReasonKind::PeerCrashed,
        RecoveryPathHash{0x123456789ABCDEF0ULL});

    if (resource.sentinel != 17)                              return 1;
    if (log.size() != 1)                                      return 2;
    if (log[0].op != SessionOp::Stop)                         return 3;
    if (log[0].from_role != kServer)                          return 4;
    if (log[0].to_role != kClient)                            return 5;
    if (log[0].stop_peer_tag() != kClient)                    return 6;
    if (log[0].stop_reason_kind() != StopReasonKind::PeerCrashed) return 7;
    if (log[0].stop_recovery_path_hash().value !=
        0x123456789ABCDEF0ULL)                                return 8;
    return 0;
}

// ── Worked: Checkpoint base/rollback event replay round-trip ───────

int run_checkpoint_event_replay_roundtrip() {
    SessionEventLog log{SessionTagId{505}};
    constexpr CheckpointId kBaseId{11};
    constexpr CheckpointId kRollbackId{12};
    constexpr auto kBaseHash =
        crucible::ContentHash::from_raw(0x1010101010101010ULL);
    constexpr auto kRollbackHash =
        crucible::ContentHash::from_raw(0x2020202020202020ULL);

    log.append_event(SessionEvent::checkpoint_base(
        kClient, kServer, kBaseId, kBaseHash));
    log.append_event(SessionEvent::checkpoint_rollback(
        kClient, kServer, kRollbackId, kRollbackHash));

    if (log.size() != 2)                                            return 1;

    auto replay = log.replay_iter();
    auto it = replay.begin();
    if (it == replay.end())                                         return 2;
    const SessionEvent base = *it++;
    if (it == replay.end())                                         return 3;
    const SessionEvent rollback = *it++;
    if (it != replay.end())                                         return 4;

    if (std::memcmp(&base, &log[0], sizeof(SessionEvent)) != 0)      return 5;
    if (std::memcmp(&rollback, &log[1], sizeof(SessionEvent)) != 0)  return 6;

    if (base.op != SessionOp::Checkpoint_Base)                      return 7;
    if (base.checkpoint_id() != kBaseId)                            return 8;
    if (base.checkpoint_choice() != CheckpointChoice::Base)          return 9;
    if (base.checkpoint_saved_state_content_hash() != kBaseHash)     return 10;

    if (rollback.op != SessionOp::Checkpoint_Rollback)              return 11;
    if (rollback.checkpoint_id() != kRollbackId)                    return 12;
    if (rollback.checkpoint_choice() != CheckpointChoice::Rollback)  return 13;
    if (rollback.checkpoint_saved_state_content_hash() != kRollbackHash) {
        return 14;
    }

    return 0;
}

// ── Worked: RecordingSessionHandle<CheckpointedSession> events ─────

int run_recording_checkpoint_paths_recorded() {
    struct CheckpointResource { int sentinel = 0; };
    using CkptProto = CheckpointedSession<End, End>;
    constexpr auto kBaseHash =
        crucible::ContentHash::from_raw(0xABCD000000000001ULL);
    constexpr auto kRollbackHash =
        crucible::ContentHash::from_raw(0xABCD000000000002ULL);

    SessionEventLog base_log{SessionTagId{606}};
    auto base_bare = mint_session_handle<CkptProto>(CheckpointResource{31});
    auto base_rec = mint_recording_session(
        std::move(base_bare), base_log, kClient, kServer);
    auto base_end = std::move(base_rec).base(CheckpointId{91}, kBaseHash);
    auto base_resource = std::move(base_end).close();

    if (base_resource.sentinel != 31)                               return 1;
    if (base_log.size() != 2)                                       return 2;
    if (base_log[0].op != SessionOp::Checkpoint_Base)               return 3;
    if (base_log[0].checkpoint_id() != CheckpointId{91})            return 4;
    if (base_log[0].checkpoint_choice() != CheckpointChoice::Base)  return 5;
    if (base_log[0].checkpoint_saved_state_content_hash() != kBaseHash) {
        return 6;
    }
    if (base_log[1].op != SessionOp::Close)                         return 7;

    SessionEventLog rollback_log{SessionTagId{607}};
    auto rollback_bare = mint_session_handle<CkptProto>(
        CheckpointResource{41});
    auto rollback_rec = mint_recording_session(
        std::move(rollback_bare), rollback_log, kClient, kServer);
    auto rollback_end = std::move(rollback_rec).rollback(
        CheckpointId{92}, kRollbackHash);
    auto rollback_resource = std::move(rollback_end).close();

    if (rollback_resource.sentinel != 41)                           return 8;
    if (rollback_log.size() != 2)                                   return 9;
    if (rollback_log[0].op != SessionOp::Checkpoint_Rollback)       return 10;
    if (rollback_log[0].checkpoint_id() != CheckpointId{92})        return 11;
    if (rollback_log[0].checkpoint_choice() !=
        CheckpointChoice::Rollback)                                 return 12;
    if (rollback_log[0].checkpoint_saved_state_content_hash() !=
        kRollbackHash)                                              return 13;
    if (rollback_log[1].op != SessionOp::Close)                     return 14;

    return 0;
}

}  // anonymous namespace

int main() {
    if (int rc = run_log_basic();                       rc != 0) return rc;
    if (int rc = run_step_counter_monotone();           rc != 0) return 100 + rc;
    if (int rc = run_manual_record_preserves_stepid();  rc != 0) return 200 + rc;
    if (int rc = run_drain_yields_storage();            rc != 0) return 300 + rc;
    if (int rc = run_request_reply_recorded();          rc != 0) return 400 + rc;
    if (int rc = run_replay_determinism_property();     rc != 0) return 500 + rc;
    if (int rc = run_payload_hash_opt_in();             rc != 0) return 600 + rc;
    if (int rc = run_offer_branch_recorded();           rc != 0) return 700 + rc;
    if (int rc = run_stop_event_replay_roundtrip();     rc != 0) return 800 + rc;
    if (int rc = run_recording_stop_close_recorded();   rc != 0) return 900 + rc;
    if (int rc = run_checkpoint_event_replay_roundtrip(); rc != 0) return 1000 + rc;
    if (int rc = run_recording_checkpoint_paths_recorded(); rc != 0) return 1100 + rc;

    std::puts("session_event_log: log primitive + RecordingSessionHandle "
              "wrapper + bilateral capture + replay-determinism + Offer "
              "branch capture + Stop + Checkpoint replay OK");
    return 0;
}
