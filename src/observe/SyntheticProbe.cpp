#include <crucible/observe/SyntheticProbe.h>

namespace crucible::observe {
static_assert(transport_probe_kind_name(TransportProbeKind::TcpBbr3) == "TcpBbr3");
static_assert(synthetic_probe_failure_name(SyntheticProbeFailureClass::Timeout)
              == "Timeout");
static_assert(safety::diag::is_diagnostic_class_v<SyntheticProbeFailure>);
}  // namespace crucible::observe
