// The write of a linear payload is a class with a call operator, such as a
// closure.  A function decays to a pointer in the send, so a call through it
// names no body that takes the payload.  The function has the exact shape of
// the write, and it is refused only because it is a function.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Mint.h>
#include <foundation/Brand.h>
#include <foundation/effects/Effect.h>
#include <foundation/permissions/Permission.h>

#include <utility>

namespace neg_sess_send_linear_write_is_a_function_types {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace neg_sess_send_linear_write_is_a_function_types

namespace {

namespace s = ::fixy::session;
namespace fp = ::foundation::permissions;
using neg_sess_send_linear_write_is_a_function_types::Region;

template <class Message>
struct Mailbox {
    ::fixy::FixedList<Message, 1> held{};
    [[no_unique_address]] s::MoveOnlyResource one_holder{};
};

template <class Message>
[[nodiscard]] ::fixy::Option<Message> put_message(Mailbox<Message>& box, Message&& message) noexcept {
    if (box.held.size() != 0) return ::fixy::Option<Message>::some(std::move(message));
    static_cast<void>(box.held.append(std::move(message)).expect("the mailbox is empty"));
    return ::fixy::none;
}

}  // namespace

int main() {
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto root = fp::mint_permission_root<Region>();
    using Message = s::Transferable<int, Region, ::foundation::brand::brand_of_t<decltype(root)>>;
    auto sender = s::mint_permissioned_session<s::Send<Message, s::End>>(ctx, Mailbox<Message>{}, std::move(root));
    auto sender_done = std::move(sender).send(1, put_message<Message>);
    static_cast<void>(std::move(sender_done).close());
    return 0;
}
