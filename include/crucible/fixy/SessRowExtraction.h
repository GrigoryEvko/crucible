#pragma once

#include <crucible/sessions/SessionRowExtraction.h>

#include <cstddef>
#include <type_traits>

namespace crucible::fixy::sess::row {

using ::crucible::safety::proto::NumericalPayloadRow;

using ::crucible::safety::proto::payload_row;
using ::crucible::safety::proto::payload_row_t;

using ::crucible::safety::proto::payload_row_effect;
using ::crucible::safety::proto::payload_row_effect_t;

using ::crucible::safety::proto::payload_effect_row_t;

using ::crucible::safety::proto::protocol_effect_row;
using ::crucible::safety::proto::protocol_effect_row_t;

}  // namespace crucible::fixy::sess::row

namespace crucible::fixy::sess::row::v062_self_test {

namespace proto = ::crucible::safety::proto;
namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

struct UserPod {
    int x;
    double y;
};
struct ProvTag {};

using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
using BgComp = eff::Computation<eff::Row<eff::Effect::Bg>, int>;

using TaggedIo = saf::Tagged<IoComp, ProvTag>;
using StaleIo = saf::Stale<IoComp>;
using LinearIo = saf::Linear<IoComp>;
using SecretIo = saf::Secret<IoComp>;

using End_ = proto::End;
using SendIo = proto::Send<IoComp, proto::End>;
using RecvBg = proto::Recv<BgComp, proto::End>;

static_assert(std::is_same_v<NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>,
                             proto::NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>>,
              "NumericalPayloadRow must reach identically through fixy::");
static_assert(NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>::tolerance
                  == saf::Tolerance::BITEXACT,
              "NumericalPayloadRow preserves the tolerance grade NTTP.");

static_assert(std::is_same_v<payload_row_t<int>, eff::Row<>>);
static_assert(std::is_same_v<payload_row_t<UserPod>, eff::Row<>>);

static_assert(std::is_same_v<payload_row_t<IoComp>, eff::Row<eff::Effect::IO>>);
static_assert(std::is_same_v<payload_row_t<BgComp>, eff::Row<eff::Effect::Bg>>);

static_assert(std::is_same_v<payload_row_t<IoComp>, proto::payload_row_t<IoComp>>,
              "payload_row_t must reach identically through fixy::");

static_assert(std::is_same_v<payload_row_t<TaggedIo>, payload_row_t<IoComp>>, "Tagged is transparent for payload_row.");
static_assert(std::is_same_v<payload_row_t<StaleIo>, payload_row_t<IoComp>>, "Stale is transparent for payload_row.");
static_assert(std::is_same_v<payload_row_t<LinearIo>, payload_row_t<IoComp>>, "Linear is transparent for payload_row.");
static_assert(std::is_same_v<payload_row_t<SecretIo>, payload_row_t<IoComp>>, "Secret is transparent for payload_row.");

static_assert(std::is_same_v<typename payload_row<IoComp>::type, eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<payload_row_effect_t<eff::Row<eff::Effect::IO>>, eff::Row<eff::Effect::IO>>,
              "payload_row_effect_t is identity on bare effects::Row<...>.");

using NPR = NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>;
static_assert(std::is_same_v<payload_row_effect_t<NPR>, eff::Row<eff::Effect::IO>>,
              "payload_row_effect_t strips tolerance from NumericalPayloadRow.");

static_assert(std::is_same_v<typename payload_row_effect<eff::Row<eff::Effect::IO>>::type, eff::Row<eff::Effect::IO>>);

static_assert(std::is_same_v<payload_effect_row_t<IoComp>, eff::Row<eff::Effect::IO>>,
              "payload_effect_row_t<Computation<Row<IO>, int>> = Row<IO>.");
static_assert(std::is_same_v<payload_effect_row_t<int>, eff::Row<>>,
              "payload_effect_row_t<int> = Row<> (bare base case).");

static_assert(std::is_same_v<payload_effect_row_t<TaggedIo>, proto::payload_effect_row_t<TaggedIo>>,
              "payload_effect_row_t must reach identically through fixy::");

static_assert(std::is_same_v<protocol_effect_row_t<End_>, eff::Row<>>, "protocol_effect_row_t<End> = Row<>.");

static_assert(std::is_same_v<protocol_effect_row_t<SendIo>, eff::Row<eff::Effect::IO>>,
              "protocol_effect_row_t<Send<Computation<Row<IO>,_>, End>> = Row<IO>.");
static_assert(std::is_same_v<protocol_effect_row_t<RecvBg>, eff::Row<eff::Effect::Bg>>,
              "protocol_effect_row_t<Recv<Computation<Row<Bg>,_>, End>> = Row<Bg>.");

static_assert(std::is_same_v<typename protocol_effect_row<End_>::type, eff::Row<>>);

static_assert(std::is_same_v<protocol_effect_row_t<SendIo>, proto::protocol_effect_row_t<SendIo>>,
              "protocol_effect_row_t must reach identically through fixy::");

constexpr int v062_surface_cardinality = 8;
static_assert(v062_surface_cardinality == 8, "The re-exported row-extraction surface cardinality drifted. "
                                             "Update the using-decls and this sentinel together.");

}  // namespace crucible::fixy::sess::row::v062_self_test

namespace crucible::fixy::sess::row {

inline void runtime_smoke_test() noexcept {
    namespace proto = ::crucible::safety::proto;
    namespace eff = ::crucible::effects;
    namespace saf = ::crucible::safety;

    using IoComp = eff::Computation<eff::Row<eff::Effect::IO>, int>;
    using End_ = proto::End;
    using SendIo = proto::Send<IoComp, End_>;

    [[maybe_unused]] constexpr bool comp_ok = std::is_same_v<payload_row_t<IoComp>, eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool bare_ok = std::is_same_v<payload_row_t<int>, eff::Row<>>;
    [[maybe_unused]] constexpr bool proto_ok = std::is_same_v<protocol_effect_row_t<SendIo>, eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool effect_strip_ok =
        std::is_same_v<payload_effect_row_t<IoComp>, eff::Row<eff::Effect::IO>>;

    using NPR = NumericalPayloadRow<saf::Tolerance::BITEXACT, eff::Row<eff::Effect::IO>>;
    [[maybe_unused]] constexpr bool npr_strip_ok = std::is_same_v<payload_row_effect_t<NPR>, eff::Row<eff::Effect::IO>>;

    (void)comp_ok;
    (void)bare_ok;
    (void)proto_ok;
    (void)effect_strip_ok;
    (void)npr_strip_ok;
}

}  // namespace crucible::fixy::sess::row
