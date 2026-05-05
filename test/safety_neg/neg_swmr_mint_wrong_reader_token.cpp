// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GAPS-021: token-bearing reader mint requires a SharedPermission for
// exactly Swmr::reader_tag; a different tag is not a reader proof.

#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SwmrSession.h>

namespace ses = crucible::safety::proto::swmr_session;
namespace safety = crucible::safety;

struct WriterTag {};
struct ReaderTag {};
struct WrongReaderTag {};
using Swmr = ses::SwmrSession<int, WriterTag, ReaderTag>;

int main() {
    Swmr swmr{};
    auto wrong = safety::mint_permission_share(
        safety::mint_permission_root<WrongReaderTag>());
    auto reader = ses::mint_swmr_reader<Swmr>(swmr, wrong);
    return reader.has_value() ? reader->load() : 1;
}
