#pragma once

#include <crucible/sessions/SessionCheckpoint.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::checkpoint {

using ::crucible::safety::proto::CheckpointedSession;

using ::crucible::safety::proto::is_checkpointed_session;
using ::crucible::safety::proto::is_checkpointed_session_v;

using ::crucible::safety::proto::checkpoint_base;
using ::crucible::safety::proto::checkpoint_base_t;
using ::crucible::safety::proto::checkpoint_rollback;
using ::crucible::safety::proto::checkpoint_rollback_t;

using ::crucible::safety::proto::Checkpointed;

using ::crucible::safety::proto::assert_checkpointed_matches;

}  // namespace crucible::fixy::sess::checkpoint

namespace crucible::fixy::sess::checkpoint::v061_self_test {

namespace proto = ::crucible::safety::proto;

struct Request {};
struct Response {};
struct Error {};

using CommitPath = proto::Send<Request, proto::Recv<Response, proto::End>>;
using RollbackPath = proto::Send<Request, proto::Recv<Error, proto::End>>;
using CkptSession = CheckpointedSession<CommitPath, RollbackPath>;

using PlainProto = proto::Send<Request, proto::End>;

static_assert(
    std::is_same_v<CheckpointedSession<CommitPath, RollbackPath>, proto::CheckpointedSession<CommitPath, RollbackPath>>,
    "CheckpointedSession must reach identically through fixy::. "
    "A failure means the first using-decl is broken or the substrate "
    "type moved.");

static_assert(std::is_same_v<typename CkptSession::base, CommitPath>);
static_assert(std::is_same_v<typename CkptSession::rollback, RollbackPath>);

static_assert(is_checkpointed_session_v<CkptSession> == proto::is_checkpointed_session_v<CkptSession>,
              "is_checkpointed_session_v must reach identically through fixy::");
static_assert(is_checkpointed_session_v<CkptSession>, "CkptSession IS a CheckpointedSession — trait must return true.");
static_assert(!is_checkpointed_session_v<PlainProto>, "Send<T, K> is NOT a CheckpointedSession — trait must reject.");
static_assert(!is_checkpointed_session_v<proto::End>, "End is NOT a CheckpointedSession — trait must reject.");

static_assert(is_checkpointed_session<CkptSession>::value);
static_assert(!is_checkpointed_session<PlainProto>::value);

static_assert(std::is_same_v<checkpoint_base_t<CkptSession>, CommitPath>,
              "checkpoint_base_t<CkptSession> must equal CommitPath.");
static_assert(std::is_same_v<checkpoint_rollback_t<CkptSession>, RollbackPath>,
              "checkpoint_rollback_t<CkptSession> must equal RollbackPath.");

static_assert(std::is_same_v<typename checkpoint_base<CkptSession>::type, CommitPath>);
static_assert(std::is_same_v<typename checkpoint_rollback<CkptSession>::type, RollbackPath>);

static_assert(std::is_same_v<checkpoint_base_t<CkptSession>, proto::checkpoint_base_t<CkptSession>>,
              "checkpoint_base_t must reach identically through fixy::");
static_assert(std::is_same_v<checkpoint_rollback_t<CkptSession>, proto::checkpoint_rollback_t<CkptSession>>,
              "checkpoint_rollback_t must reach identically through fixy::");

template <typename P>
    requires Checkpointed<P>
consteval bool requires_checkpointed_witness() {
    return true;
}
static_assert(requires_checkpointed_witness<CkptSession>(),
              "Checkpointed concept must admit CheckpointedSession<B, R>.");

template <typename P>
consteval bool can_satisfy_checkpointed() {
    return requires { requires Checkpointed<P>; };
}
static_assert(!can_satisfy_checkpointed<PlainProto>(), "Checkpointed concept must REJECT non-checkpoint protocols.");
static_assert(!can_satisfy_checkpointed<proto::End>(), "Checkpointed concept must REJECT End.");

consteval bool check_fixy_assert_checkpointed_matches() {
    assert_checkpointed_matches<CkptSession, CommitPath, RollbackPath>();
    return true;
}
static_assert(check_fixy_assert_checkpointed_matches(), "assert_checkpointed_matches must accept the correct (P, B, R) "
                                                        "triple at consteval.");

constexpr int v061_surface_cardinality = 9;
static_assert(v061_surface_cardinality == 9, "The re-exported checkpoint surface cardinality drifted. "
                                             "Update the using-decls and this sentinel together.");

}  // namespace crucible::fixy::sess::checkpoint::v061_self_test

namespace crucible::fixy::sess::checkpoint {

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    struct Req {};
    struct Resp {};
    struct Err {};

    using B = proto::Send<Req, proto::Recv<Resp, proto::End>>;
    using R = proto::Send<Req, proto::Recv<Err, proto::End>>;
    using C = CheckpointedSession<B, R>;
    using P = proto::Send<Req, proto::End>;

    [[maybe_unused]] constexpr bool is_ckpt_yes = is_checkpointed_session_v<C>;
    [[maybe_unused]] constexpr bool is_ckpt_no = is_checkpointed_session_v<P>;
    [[maybe_unused]] constexpr bool base_eq_B = std::is_same_v<checkpoint_base_t<C>, B>;
    [[maybe_unused]] constexpr bool rollback_eq_R = std::is_same_v<checkpoint_rollback_t<C>, R>;

    constexpr auto satisfies_ckpt = []<typename Q>() { return Checkpointed<Q>; };
    [[maybe_unused]] constexpr bool concept_yes = satisfies_ckpt.template operator()<C>();
    [[maybe_unused]] constexpr bool concept_no = satisfies_ckpt.template operator()<P>();

    (void)is_ckpt_yes;
    (void)is_ckpt_no;
    (void)base_eq_B;
    (void)rollback_eq_R;
    (void)concept_yes;
    (void)concept_no;
}

}  // namespace crucible::fixy::sess::checkpoint
