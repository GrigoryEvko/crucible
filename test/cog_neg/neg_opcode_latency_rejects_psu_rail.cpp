// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for cog::opcodes_for / cog::HasOpcodeTable
// (GAPS-187 #1212).
//
// Premise: opcodes_for<K>::type is the kind-to-opcode-enum binding
// metafunction for substrate Cogs that publish opcode catalogs (Gpu,
// CpuCore, CpuSocket, NicPort, NvSwitch, DramChannel — six
// specialisations).  Non-schedulable Cogs (PsuRail / BmcSensor /
// OpticalTransceiver / PcieLaneGroup / NvmeNamespace / and the L2..L7
// aggregates) do NOT publish an opcode catalog — there is nothing
// to schedule on a power-supply rail or a thermal sensor; per-opcode
// latency / throughput data is a meaningless concept for them.
//
// HasOpcodeTable<K> is the load-bearing soundness gate that downstream
// templates (GAPS-188 mint_cog_mimic factory consuming the table,
// GAPS-191 FitsCog<Row,K> reading the throughput envelope, GAPS-810
// partition optimiser reading per-opcode placement cost) constrain on
// so the type system rejects the misuse "publish an OpcodeLatencyTable
// for a power rail" at template-substitution time.
//
// HasOpcodeTable<K> is defined as `HasCaps<K> && requires { typename
// opcodes_for<K>::type; }` — the conjunct binds the two concepts.
// A Cog publishes opcodes IFF it publishes capability schemas.  The
// rejection here exercises BOTH conjuncts simultaneously: PsuRail
// fails HasCaps (no caps_for specialisation) AND fails
// opcodes_for-defined (no opcodes_for specialisation), so the
// concept fails on the first conjunct.
//
// Why this is the load-bearing soundness gate:
//
// Without this gate, a future GAPS-188 factory accepting any CogKind
// would silently produce a PsuRailMimic — a Mimic stub bound to a Cog
// that has no compute substrate AND no opcode catalog.  Subsequent
// calls into that stub would either hit a bare `opcodes_for<CogKind::
// PsuRail>` resolution failure deep inside the kernel-emit pipeline
// (a confusing "incomplete type" error far from the source of the
// bug), or — worse — fall through to a default branch and produce a
// no-op stub that silently drops scheduled work.
//
// Companion fixture: neg_opcode_latency_quantiles_misordered.cpp
//   * That one tests rejection at the DATA-INVARIANT gate — the
//     OrderedLatencyQuantiles Refined alias refuses a triple violating
//     p50 ≤ p99 ≤ p999 at construction.  Distinct mismatch class
//     (precondition contract violation on a Refined wrapper).
//   * This one tests rejection at the CONCEPT gate — HasOpcodeTable<K>
//     refuses non-substrate CogKind atoms at template substitution.
//     Distinct mismatch class (concept substitution failure on a
//     constrained template parameter).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" / "constraints not
// satisfied" / "HasOpcodeTable" / "lookup_opcodes" / "CogKind::PsuRail"
// / "GAPS-187" pointing at the static_assert call site below.

#include <crucible/cog/OpcodeLatencyTable.h>

namespace cog = crucible::cog;

// Mock of the future GAPS-188 mint_cog_mimic / GAPS-191 FitsCog factory
// shape: a function templated on a CogKind value, constrained on
// HasOpcodeTable.  Calling it with a CogKind that has no opcode
// catalog fails the concept gate at substitution time.
template <cog::CogKind K>
    requires cog::HasOpcodeTable<K>
constexpr int lookup_opcodes() noexcept { return 1; }

// CogKind::PsuRail is a power-supply rail atom — no schedulable
// workload, no opcode catalog, no OpcodeLatencyTable specialisation.
// HasOpcodeTable<PsuRail> is false (fails on the HasCaps conjunct
// FIRST, then on the opcodes_for-defined conjunct), so the requires-
// clause refuses the substitution and the build fails here at the
// call site.
static_assert(lookup_opcodes<cog::CogKind::PsuRail>() == 1,
    "GAPS-187: cog::HasOpcodeTable concept MUST refuse non-substrate "
    "CogKind values.  If this static_assert ever evaluates, a future "
    "mint_cog_mimic factory would accept PsuRail as a target and "
    "produce a Mimic stub bound to a Cog with no compute substrate AND "
    "no opcode catalog — Cog-substrate-binding partition defense "
    "compromised at the per-opcode latency layer.");

int main() { return 0; }
