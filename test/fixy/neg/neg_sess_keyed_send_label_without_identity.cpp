// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A keyed send puts the label word of its key on the wire.  The key is a
// class in an anonymous namespace, so it has no stable identity and no
// label word (rule 7 of the section on branches and labels of
// fixy/session/Fold.h).  The send reads the word in a constant variable,
// so the build stops with the refusal that names the rule.
//
// Expected diagnostic: Label_Key_Without_Identity, with the rule.
#include <fixy/Ctx.h>
#include <fixy/session/Entry.h>
#include <fixy/session/Projection.h>

#include <foundation/effects/Ctx.h>

#include <cstddef>
#include <utility>

namespace neg_sess_keyed_send_label_without_identity_types {
struct Q {};
namespace {
struct Hidden {};
}  // namespace
struct Wire {
    std::size_t word = 0;
    [[no_unique_address]] ::fixy::session::MoveOnlyResource one_holder{};
};
using SendsHidden = ::fixy::session::Send<::fixy::session::PeerMsg<Q, Hidden, void>, ::fixy::session::End>;
}  // namespace neg_sess_keyed_send_label_without_identity_types

int main() {
    using namespace neg_sess_keyed_send_label_without_identity_types;
    const ::fixy::TestRunnerCtx ctx{::foundation::effects::testing::test()};
    auto head = ::fixy::session::mint_session<SendsHidden>(ctx, Wire{});
    auto done = std::move(head).send([](Wire& wire, std::size_t word) noexcept {
        wire.word = word;
        return true;
    });
    static_cast<void>(std::move(done).close());
    return 0;
}
