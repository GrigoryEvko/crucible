// What the node readers of fixy/session/Protocol.h and
// fixy/session/Global.h claim, checked.
//
// Each reader claims the nested members of a combinator from its template
// arguments, and the mint and the projection refuse a node whose members
// disagree.  A member that no claim names is a member that a reader could
// read with no check.  For one instance of each combinator, this test
// walks the members that the instance declares and asks that each one is
// claimed.  It also asks that each registered combinator of the session
// layer has an instance here, and that each instance agrees with its own
// claims.

#include <fixy/session/Crash.h>
#include <fixy/session/Global.h>
#include <fixy/session/Projection.h>
#include <fixy/session/Protocol.h>

#include <foundation/algebra/Transition.h>

#include <meta>
#include <vector>

namespace s = ::fixy::session;
namespace g = ::fixy::session::global;
namespace t = ::foundation::algebra::transition;

namespace {

struct Alice {};
struct Bob {};
struct Hello {};

// A member that a reader can read at compile time: a type alias, a
// nested type or a static data member.  A crash label also has a value
// field, which the program reads at run time.
consteval bool is_readable_member(std::meta::info member) {
    return std::meta::has_identifier(member)
           && (std::meta::is_type_alias(member) || std::meta::is_type(member)
               || (std::meta::is_variable(member) && std::meta::is_static_member(member)));
}

// True when each readable member of `type` is named by a claim of `read`.
// A reader outside the class sees only the public members, so the walk
// takes the access of this scope.
consteval bool every_member_claimed(t::node_reader read, std::meta::info type) {
    const t::node_members view = read(type);
    if (!view.is_node) return false;
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::current())) {
        if (!is_readable_member(member)) continue;
        bool is_claimed = false;
        for (const t::member_claim& claim : view.claims) is_claimed = is_claimed || claim.name == std::meta::identifier_of(member);
        if (!is_claimed) return false;
    }
    return true;
}

consteval bool agrees(t::node_reader read, std::meta::info type) {
    return t::first_disagreeing_node(read, type) == std::meta::info{};
}

// One instance of each session combinator, a choice with and without the
// note of its sender.
constexpr std::meta::info session_instances[] = {
    ^^s::Send<int, s::End>,
    ^^s::Recv<int, s::End>,
    ^^s::Select<s::Send<int, s::End>>,
    ^^s::Select<s::Sender<Alice>, s::Send<int, s::End>>,
    ^^s::Offer<s::Recv<int, s::End>>,
    ^^s::Offer<s::Sender<Alice>, s::Recv<int, s::End>>,
    ^^s::Loop<s::Send<int, s::Continue>>,
    ^^s::Continue,
    ^^s::End,
    ^^s::VendorPinned<s::VendorBackend::NV, s::End>,
    ^^s::Delegate<s::End, s::End>,
    ^^s::Accept<s::End, s::End>,
    ^^s::Stop,
    ^^s::Commit<s::End>,
    ^^s::Roll,
    ^^s::Abort,
    ^^s::Crash<Alice>,
    ^^s::PeerMsg<Alice, Hello, int>,
    ^^s::Labelled<Hello, int>,
};

consteval bool every_session_instance_is_claimed() {
    for (const std::meta::info instance : session_instances) {
        if (!every_member_claimed(&s::detail::session_node_members, instance)) return false;
        if (!agrees(&s::detail::session_node_members, instance)) return false;
    }
    return true;
}

// Each registered combinator of the session layer has an instance above.
consteval bool every_registered_combinator_has_an_instance() {
    for (const std::meta::info registration :
         std::meta::members_of(s::detail::protocol_registry, std::meta::access_context::current())) {
        if (!std::meta::is_variable(registration) || std::meta::remove_const(std::meta::type_of(registration)) != ^^t::combinator) {
            continue;
        }
        const std::meta::info shape = std::meta::extract<t::combinator>(registration).shape;
        bool has_instance = false;
        for (const std::meta::info instance : session_instances) has_instance = has_instance || t::shape_of(instance) == shape;
        if (!has_instance) return false;
    }
    return true;
}

constexpr std::meta::info global_instances[] = {
    ^^g::End,
    ^^g::Var,
    ^^g::Rec<g::Var>,
    ^^g::Branch<Hello, int, g::End>,
    ^^g::Comm<Alice, Bob, g::Branch<Hello, int, g::End>>,
    ^^g::EnRouteChoice<Alice, Bob, Hello, g::Branch<Hello, int, g::End>>,
    ^^g::Crashed<Alice>,
};

consteval bool every_global_instance_is_claimed() {
    for (const std::meta::info instance : global_instances) {
        if (!every_member_claimed(&g::detail::global_node_members, instance)) return false;
        if (!agrees(&g::detail::global_node_members, instance)) return false;
    }
    return true;
}

static_assert(every_session_instance_is_claimed(),
              "a session combinator declares a member that the node reader of fixy/session/Protocol.h does not claim");
static_assert(every_registered_combinator_has_an_instance(),
              "a registered session combinator has no instance in this test");
static_assert(every_global_instance_is_claimed(),
              "a global combinator declares a member that the node reader of fixy/session/Global.h does not claim");

// The walk sees the members: a Send declares two, an Offer with a sender
// three, a Comm three and a crash label one.
consteval std::size_t readable_member_count(std::meta::info type) {
    std::size_t count = 0;
    for (const std::meta::info member : std::meta::members_of(type, std::meta::access_context::current())) {
        if (is_readable_member(member)) ++count;
    }
    return count;
}
static_assert(readable_member_count(^^s::Send<int, s::End>) == 2);
static_assert(readable_member_count(^^s::Offer<s::Sender<Alice>, s::Recv<int, s::End>>) == 3);
static_assert(readable_member_count(^^g::Comm<Alice, Bob, g::Branch<Hello, int, g::End>>) == 3);
static_assert(readable_member_count(^^s::Crash<Alice>) == 1);

// A node that its reader does not know is no node.
static_assert(!every_member_claimed(&s::detail::session_node_members, ^^int));
static_assert(agrees(&s::detail::session_node_members, ^^s::Loop<s::Offer<s::Recv<int, s::Continue>, s::Recv<long, s::End>>>));

}  // namespace

int main() { return 0; }
