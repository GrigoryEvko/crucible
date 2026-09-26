// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for CDAG_VERSION, a Tagged<uint32_t,
// source::FormatVersion>.
//
// Premise: disk-read header versions enter as source::External and
// are validated against the in-process source::FormatVersion constant.
// The two tags are intentionally distinct; no implicit retag may move
// a trusted format constant into an external-input lane.
//
// Distinct mismatch class from neg_cdag_version_raw_uint32.cpp:
//   * Companion: implicit raw extraction rejected.
//   * This fixture: cross-tag assignment rejected.

#include <crucible/Serialize.h>
#include <fixy/Tagged.h>

#include <cstdint>

int main() {
    using ExternalVersion = ::fixy::Tagged<std::uint32_t, ::fixy::tags::source::External>;

    // MUST fail: source::FormatVersion is not source::External.
    ExternalVersion disk = crucible::CDAG_VERSION;
    return static_cast<int>(disk.value());
}
