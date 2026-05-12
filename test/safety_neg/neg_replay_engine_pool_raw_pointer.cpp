// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// WRAP-RE-3 (#995): ReplayEngine::init admits the pool dependency as
// ReplayEngine::PoolBorrow = safety::BorrowedRef<const PoolAllocator>.
// A raw PoolAllocator* must not cross the init boundary.
//
// Expected diagnostic: no init overload accepting PoolAllocator*.

#include <crucible/ReplayEngine.h>

int main() {
  crucible::ReplayEngine engine{};
  const crucible::RegionNode* region = nullptr;
  const crucible::PoolAllocator* pool = nullptr;
  engine.init(region, pool);
  return 0;
}
