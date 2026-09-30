// Every mint of a session asks permission_flow_closes whether each path of
// the protocol keeps the permission set sound.  This file tries to admit a
// flow that leaves a loan open: it writes an explicit specialization of
// permission_flow_closes.  permission_flow_closes is a function at
// namespace scope that is not a template, so no specialization matches it.

#include <fixy/session/Handle.h>

#include <meta>

template <>
consteval bool fixy::session::detail::permission_flow_closes(std::meta::info, std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
