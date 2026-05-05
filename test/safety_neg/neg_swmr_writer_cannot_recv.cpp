// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-021: a SWMR writer role cannot perform reader recv/load work.

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
    auto value = writer.recv();
    return value;
}
