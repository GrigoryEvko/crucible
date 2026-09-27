// The stack frame of the JSON emitter of foundation/diag/JsonEmitter.h.
//
// This file compiles with -Werror=frame-larger-than=8192.  Each function of
// the emitter that the file instantiates is checked, so an emitter that puts
// a large buffer on the stack stops the build here.  A thread with a small
// stack can then emit a record.  The run makes sure that the record reaches
// the stream.

#include <foundation/diag/JsonEmitter.h>

#include <cstdio>

namespace {

[[gnu::noinline]] bool emit_one_record(std::FILE* out) noexcept {
    return foundation::diag::emit_json_violation(out, foundation::diag::Category::BudgetExceeded,
                                                 "frame.cpp:1:2@frame_fn", "the frame of the emitter");
}

}  // namespace

int main() {
    std::FILE* out = std::tmpfile();
    if (out == nullptr) {
        std::fprintf(stderr, "test_json_emitter_frame: no temporary file\n");
        return 1;
    }
    const bool is_written = emit_one_record(out) && std::ftell(out) > 0;
    std::fclose(out);
    std::printf("test_json_emitter_frame: %s\n", is_written ? "passed" : "FAILED");
    return is_written ? 0 : 1;
}
