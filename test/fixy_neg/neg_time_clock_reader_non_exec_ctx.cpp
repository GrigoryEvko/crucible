// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-190 mint_clock_reader, mismatch class #2 of 2:
// NON-EXEC-CONTEXT FIRST ARGUMENT.
//
// Every §XXI ctx-bound mint takes an effects::IsExecCtx as its first
// argument.  A bare `int` is not an ExecCtx, so the mint's constrained
// template parameter rejects it before overload resolution completes.
//
// Distinct from neg_time_clock_reader_tsc_source.cpp (a source-axis
// constraint); here the failure is the ctx-type constraint.
//
// Expected diagnostic: constraints not satisfied / IsExecCtx /
// no matching function / mint_clock_reader.

#include <crucible/fixy/Time.h>

int main() {
    // Should FAIL: 42 (int) is not an effects::IsExecCtx.
    auto reader = ::crucible::fixy::time::mint_clock_reader<
        ::crucible::fixy::time::ClockSource_v::Monotonic>(42);

    return static_cast<int>(reader.read().peek());
}
