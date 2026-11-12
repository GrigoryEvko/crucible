// The verdict of a checkpoint pair comes from checkpoint_verdict_of.  This
// file tries to make each pair compliant: it writes an explicit
// specialization of checkpoint_verdict_of.  checkpoint_verdict_of is a
// function at namespace scope that is not a template, so no
// specialization matches it.

#include <fixy/session/Checkpoint.h>

#include <meta>

template <>
consteval fixy::session::CheckpointVerdict fixy::session::checkpoint_verdict_of(std::meta::info, std::meta::info) {
    return fixy::session::CheckpointVerdict::Compliant;
}

int main() { return 0; }
