// Admitted reads admits, and the retag gate of fixy/Tagged.h reads
// Admitted.  This file tries to retag user input as integrity-verified,
// a pair that admitted_retags does not declare: it writes an explicit
// specialization of admits.  admits is a function at namespace scope that
// is not a template, so no specialization matches it.

#include <foundation/diag/FailClosed.h>

#include <meta>

template <>
consteval bool foundation::fail_closed::admits(std::meta::info, std::meta::info, std::meta::info) {
    return true;
}

int main() { return 0; }
