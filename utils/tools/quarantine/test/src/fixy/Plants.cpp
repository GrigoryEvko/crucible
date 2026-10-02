// The plants of the include rules.  The test table puts this file in the
// layer low.  check_plugin.py compiles it in error mode with no plant, and
// one time for each PLANT_ macro.  Each plant gives one finding, and the file
// with no plant gives none.

#include <cstring>

#if defined(PLANT_LAYER_HEADER)
// The allow rows give the layer low no <cmath>.
#include <cmath>
#endif

#if defined(PLANT_DOOR_HEADER)
// src/fixy/SocketDoor.cpp is the door of <sys/socket.h>.
#include <sys/socket.h>
#endif

#if defined(PLANT_UPWARD_INCLUDE)
// fixy/high/High.h is in the layer high, above the layer low.
#include <fixy/high/High.h>
#endif

namespace probe {

int plants_value() { return 1; }

}  // namespace probe
