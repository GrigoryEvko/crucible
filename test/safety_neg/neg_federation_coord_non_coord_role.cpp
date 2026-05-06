#include <crucible/sessions/FederationProtocol.h>

namespace fp = crucible::safety::proto::federation;

struct Key {};

static_assert(fp::role_protocol_matches_v<
                  fp::CoordRole, fp::SenderProto<Key>, Key>,
    "FederationCoord_NonCoordRole_Rejected");

int main() { return 0; }
