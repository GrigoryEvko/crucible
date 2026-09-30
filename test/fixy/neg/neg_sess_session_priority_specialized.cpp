// A channel mint asks session_priority whether its two Resources state one
// priority.  This file tries to give each Resource one priority: it writes
// an explicit specialization of session_priority.  session_priority is a
// function at namespace scope that is not a template, so no specialization
// matches it.

#include <fixy/session/Handle.h>

#include <meta>

template <>
consteval fixy::session::watch::priority fixy::session::session_priority(std::meta::info) {
    return fixy::session::watch::priority::lowest;
}

int main() { return 0; }
