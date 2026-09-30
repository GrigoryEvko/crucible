// The checkpoint mint asks require_agreeing_members whether each node of
// a protocol keeps the members that its arguments give.  This file tries
// to let each node lie: it writes an explicit specialization of
// require_agreeing_members.  require_agreeing_members is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/session/Protocol.h>

#include <meta>

template <>
consteval bool fixy::session::detail::require_agreeing_members(std::meta::info) {
    return true;
}

int main() { return 0; }
