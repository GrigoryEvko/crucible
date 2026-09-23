// Each entry of an associated context must carry the projection of its
// role.  Bob's entry here receives the reply label that Alice never
// sends.  Until fixy/session/Subtype.h lands, association asks for the
// projected type itself.
//
// The domain is exact and each queue is empty, as the projection says,
// so the refusal comes from the local-type clause.

#include <fixy/session/Projection.h>

namespace {

namespace g = ::fixy::session::global;
namespace s = ::fixy::session;

struct Alice {};
struct Bob {};
struct Ask {};
struct Other {};

using Question = g::Msg<Alice, Bob, Ask, int, g::End>;

using WrongBob = s::TypingContext<s::RoleState<Alice, s::OutQueue<>, typename s::project_t<Question, Alice>::local>,
                                  s::RoleState<Bob, s::OutQueue<>, s::Recv<s::PeerMsg<Alice, Other, int>, s::End>>>;

constexpr int check_context() noexcept {
    s::ensure_associated<WrongBob, Question>();
    return 0;
}

}  // namespace

int main() { return check_context(); }
