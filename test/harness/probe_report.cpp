// The output of the harness: a pass line, reports to the two sinks, a
// report longer than one window of the formatter, and a failed assert.
//
// test/harness/check_output.py runs this program.  It compares standard
// output with the exact bytes, and it reads standard error for the report
// and for the text of the failed assert: its condition, its file and line,
// and its function.  The program writes the line of the assert, from
// __LINE__, before the assert.  The program must end by SIGABRT.

#include "../test_assert.h"

#include <fixy/Core.h>

namespace {

// A text of 600 characters: twelve copies of a line of 50.
constexpr ::fixy::TextView long_text{"the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"
                                     "the fifty characters of one line of the long text\n"};

}  // namespace

int main() {
    crucible::test::pass("probe_report: the pass line\n");
    ::fixy::report(::fixy::Sink::Out, "{} of {} on the out sink, {}, {{braces}}\n", 3, -4, true);
    ::fixy::report(::fixy::Sink::Out, "{}", long_text);
    ::fixy::report(::fixy::Sink::Err, "[{}] on the err sink\n", ::fixy::TextView{"text"});
    int const queue_depth = 3;
    int const assert_line = __LINE__ + 2;
    ::fixy::report(::fixy::Sink::Err, "the assert is on line {}\n", assert_line);
    assert(queue_depth == 4);
    return 0;
}
