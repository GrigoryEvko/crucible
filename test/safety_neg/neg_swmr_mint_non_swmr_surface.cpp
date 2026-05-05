// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// SWMR mint helpers must accept only the real session surface:
// matching writer/reader handles plus writer(Permission<writer_tag>)
// and reader() factories.  A lookalike type with only nested aliases
// is not a SwmrSessionSurface.

#include <crucible/permissions/Permission.h>
#include <crucible/sessions/SwmrSession.h>

#include <utility>

namespace {

struct WriterTag {};
struct ReaderTag {};

struct NotASession {
    using value_type = int;
    using writer_tag = WriterTag;
    using reader_tag = ReaderTag;

    struct WriterHandle {
        void publish(int const&) noexcept {}
    };

    struct ReaderHandle {
        [[nodiscard]] int load() const noexcept { return 0; }
    };
};

void misuse(NotASession& s,
            crucible::safety::Permission<WriterTag>&& permission) {
    (void)crucible::safety::proto::swmr_session::mint_swmr_writer<NotASession>(
        s, std::move(permission));
}

}  // namespace

int main() { return 0; }
