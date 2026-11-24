// The compile-time checks of fixy/session/Handle.h.

#include <fixy/session/Handle.h>

namespace fixy::session {

static_assert(detail::is_sealed_passkey<HandleKey>() && detail::is_sealed_passkey<SessionOpenKey>(),
              "a passkey is final, has a private user-provided constructor, no copy, no move and a user-provided "
              "destructor, so no route but its one friend makes it");

static_assert(sizeof(std::size_t) == sizeof(std::uint64_t),
              "a label word has 64 bits, and the Transport carries it as std::size_t");

namespace detail::pole_self_test {

struct SomeChannel {};
struct Pole {
    using channel_type = SomeChannel;
    [[no_unique_address]] MoveOnlyResource move_only{};
};
struct HoldsAPole {
    Pole pole;
};
// The walk reads the template arguments of a member too.
struct MayHoldAPole {
    ::fixy::Option<Pole> pole{};
};
// A pole that is no specialization of a template is seen behind a
// reference.
struct RefersToAPole {
    Pole& pole;
};
struct Plain {
    [[no_unique_address]] MoveOnlyResource move_only{};
    int count = 0;
};
// A class that names its channel with a member that is no type is no pole.
struct NamesNoChannelType {
    [[no_unique_address]] MoveOnlyResource move_only{};
    static constexpr int channel_type = 0;
};
// A pole that is a specialization of a template is seen behind a reference,
// in a template argument and behind the pointer member of a Box, because the
// walk reads each specialization with the instantiating read.
template <class Payload>
struct TemplatePole {
    using channel_type = SomeChannel;
    Payload last{};
};
template <class Named>
struct NamesAType {};
struct RefersToATemplatePole {
    TemplatePole<int>& pole;
};
struct NamesATemplatePole {
    NamesAType<TemplatePole<long>> pole{};
};
struct BoxesATemplatePole {
    ::fixy::Box<TemplatePole<short>> pole;
};

static_assert(ChannelPole<Pole> && !ChannelPole<Pole&> && !ChannelPole<HoldsAPole>);
static_assert(ReachesAChannelPole<Pole> && ReachesAChannelPole<HoldsAPole> && ReachesAChannelPole<MayHoldAPole>
              && ReachesAChannelPole<RefersToAPole>);
static_assert(ReachesAChannelPole<RefersToATemplatePole> && ReachesAChannelPole<NamesATemplatePole>
              && ReachesAChannelPole<BoxesATemplatePole>);
static_assert(!PlainSessionResource<Pole> && !PlainSessionResource<HoldsAPole>);
static_assert(PlainSessionResource<Plain> && PlainSessionResource<NamesNoChannelType>);
// No table of this unit knows the pole, so the pole door refuses it too.
static_assert(!HasPoleTable<Pole>);

}  // namespace detail::pole_self_test

namespace detail::linear_handle_self_test {

struct Wire {
    int sent = 0;
    [[no_unique_address]] MoveOnlyResource one_holder{};
};
void cancel_session(Wire&) noexcept;

template <typename Proto, AbandonmentPolicy Policy>
using HandleAt = SessionHandle<Proto, Wire, void, Policy>;
using Owes = Send<int, End>;

// A handle over a plain Resource is linear by its base, at each position that
// owes a message, under each policy that checks abandonment.
static_assert(!::foundation::linear::LinearValue<Wire>);
static_assert(::foundation::linear::LinearValue<HandleAt<Owes, check::Enforced>>);
static_assert(::foundation::linear::LinearValue<HandleAt<Owes, check::Cancel>>);
static_assert(::foundation::linear::LinearValue<HandleAt<Owes, check::Pinned>>);
static_assert(::foundation::linear::LinearValue<HandleAt<Select<Owes, End>, check::Enforced>>);
// A handle at End gives its Resource back at close(), and check::Off checks
// no drop.
static_assert(!::foundation::linear::LinearValue<HandleAt<End, check::Enforced>>);
static_assert(!::foundation::linear::LinearValue<HandleAt<Owes, check::Off>>);

}  // namespace detail::linear_handle_self_test

}  // namespace fixy::session
