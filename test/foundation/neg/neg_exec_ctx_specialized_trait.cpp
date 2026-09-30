// Every ctx-bound gate asks IsExecCtx whether its argument is an execution
// context, and IsExecCtx reads the structure test is_exec_ctx.  This file
// tries to make each type a context that owns every effect: it writes an
// explicit specialization of is_exec_ctx.  The test is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <foundation/effects/Ctx.h>

#include <meta>

template <>
consteval bool foundation::effects::is_exec_ctx(std::meta::info) {
    return true;
}

int main() { return 0; }
