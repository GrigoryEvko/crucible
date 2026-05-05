// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-021: mint_swmr_writer consumes a linear writer Permission.

#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SwmrSession.h>

namespace ses = crucible::safety::proto::swmr_session;
namespace safety = crucible::safety;

struct WriterTag {};
struct ReaderTag {};
using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag>;

int main() {
    Swmr swmr{};
    auto perm = safety::mint_permission_root<Swmr::writer_tag>();
    auto writer = ses::mint_swmr_writer<Swmr>(swmr, std::move(perm));
    auto duplicate = ses::mint_swmr_writer<Swmr>(swmr, perm);
    (void)writer;
    (void)duplicate;
    return 0;
}
