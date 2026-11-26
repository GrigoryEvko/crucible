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

namespace detail::pin_body_self_test {

// A pin asks StoresNoBorrowOutside of its body.  A body that captures this of
// a class that holds a session handle reaches the std::source_location of the
// handle, whose pointer the store walk reads as a writable pointer, so the pin
// refuses it while the handle holds a std::source_location and no site of the
// base.  A body that captures only the part that it writes passes.
struct PinWire {
    int sent = 0;
};
using PinHead = SessionHandle<Send<int, End>, PinWire, void, check::Enforced>;
class PinStage {
public:
    [[nodiscard]] auto body_of_this() noexcept {
        return [this](auto pinned) noexcept {
            ++steps_;
            return pinned;
        };
    }
    [[nodiscard]] auto body_of_steps() noexcept {
        return [&steps = steps_](auto pinned) noexcept {
            ++steps;
            return pinned;
        };
    }

private:
    ::fixy::Option<PinHead> held_{};
    int steps_ = 0;
};
static_assert(!::foundation::core::StoresNoBorrowOutside<decltype(PinStage{}.body_of_this())>);
static_assert(::foundation::core::StoresNoBorrowOutside<decltype(PinStage{}.body_of_steps())>);

}  // namespace detail::pin_body_self_test

namespace detail::read_body_self_test {

// The read of a handle and the read of the factory ask StoresNoBorrowOutside
// of the body (HoldFactory::read_slot of fixy/session/Flow.h).  A body that
// captures by reference an Option of a View can keep a View past the read, so
// each door refuses it, and a requires-expression on the door answers no.  The
// same body with no such capture reads.
struct ReadRegion {
    using permission_row = ::foundation::effects::Row<>;
};
struct ReadBrand {};
struct ReadWire {
    int sent = 0;
};
using Borrows = ::foundation::permissions::PermSet<BorrowedIn<ReadRegion, ReadBrand>>;
using ReadHead =
    SessionHandle<Send<Released<int, ReadRegion, ReadBrand>, End>, ReadWire, void, check::Enforced, Borrows>;
struct KeepsAView {
    ::fixy::Option<::fixy::View<int const>>& kept;
    template <class View>
    void operator()(View const& /*view*/) const noexcept {}
};
struct ReadsOnly {
    template <class View>
    void operator()(View const& /*view*/) const noexcept {}
};
template <class Body>
inline constexpr bool handle_reads_v =
    requires(ReadHead head, Body body) { std::move(head).template read<ReadRegion>(std::move(body)); };
template <class Body>
inline constexpr bool factory_reads_v =
    requires(ReadHead head, Body body) { HandleFactory::read<ReadRegion>(std::move(head), std::move(body)); };
static_assert(!handle_reads_v<KeepsAView> && handle_reads_v<ReadsOnly>);
static_assert(!factory_reads_v<KeepsAView> && factory_reads_v<ReadsOnly>);

}  // namespace detail::read_body_self_test

}  // namespace fixy::session
