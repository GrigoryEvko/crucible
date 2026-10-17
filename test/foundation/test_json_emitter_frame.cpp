// The stack frame of the JSON emitter of foundation/diag/JsonEmitter.h.
//
// This file compiles with -Werror=frame-larger-than=8192.  Each function of
// the emitter that the file instantiates is checked, so an emitter that puts
// a large buffer on the stack stops the build here.  A thread with a small
// stack can then emit a record.  The run makes sure that the emitter takes
// the record and writes it to standard output.

#include <foundation/core/Report.h>
#include <foundation/core/Text.h>
#include <foundation/diag/JsonEmitter.h>

namespace {

[[gnu::noinline]] bool emit_one_record() noexcept {
    return foundation::diag::emit_json_violation(foundation::core::Sink::Out,
                                                 foundation::diag::Category::BudgetExceeded, "frame.cpp:1:2@frame_fn",
                                                 "the frame of the emitter");
}

}  // namespace

int main() {
    const bool is_written = emit_one_record();
    foundation::core::report(foundation::core::Sink::Out, "test_json_emitter_frame: {}\n",
                             is_written ? foundation::core::TextView{"passed"} : foundation::core::TextView{"FAILED"});
    return is_written ? 0 : 1;
}
