// Reads one JSON diagnostic of foundation/diag/JsonEmitter.h on each line of
// standard input, and writes one LSP CodeAction for each line that is not
// empty to standard output.  The exit status is 1 when a read fails or a
// line holds more characters than the buffer of the reader, and 0 at the end
// of the input.

#include "CodeAction.h"

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/os/Stdio.h>
#include <foundation/effects/Effect.h>
#include <foundation/os/Errno.h>

#include <cstddef>

namespace {

namespace lsp = ::crucible::tools::lsp_code_action;

// The reader holds a buffer of one megabyte, so it has static storage and
// not a place on the stack.
::fixy::os::LineReader<lsp::kMaxLineCharacters> g_reader;

}  // namespace

int main() {
    ::fixy::InitLoadCtx const ctx{::foundation::effects::host::InitOwner::mint_init_context()};
    while (true) {
        bool has_ended = false;
        bool has_failed = false;
        g_reader.next_line(ctx).match(
            [&](::fixy::Option<::fixy::TextView> line) {
                line.match(
                    [&](::fixy::TextView text) {
                        if (text.size() == 0) return;
                        lsp::write_lsp_code_action(ctx.cap().alloc, text);
                    },
                    [&]() { has_ended = true; });
            },
            [&](::fixy::os::LineError error) {
                has_failed = true;
                error.match(
                    [](::foundation::os::Errno code) {
                        ::fixy::report(::fixy::Sink::Err,
                                       "crucible_diag_lsp: the read of standard input failed "
                                       "(errno {})\n",
                                       ::foundation::os::error_number(code));
                    },
                    [](::fixy::os::LineTooLong) {
                        ::fixy::report(::fixy::Sink::Err,
                                       "crucible_diag_lsp: a line of standard input holds more "
                                       "than {} characters\n",
                                       lsp::kMaxLineCharacters);
                    });
            });
        if (has_failed) return 1;
        if (has_ended) return 0;
    }
}
