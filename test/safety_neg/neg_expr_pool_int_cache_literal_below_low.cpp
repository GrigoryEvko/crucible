// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// ExprPool::IntCacheLiteral is Refined<in_range<-128, 127>, int64_t>.
// The door into the type must refuse the value immediately below the
// closed range, so that int_cache_[val - kIntCacheLow] cannot index
// before the first entry.
//
// Expected diagnostic: the in_range precondition of mint_refined fails in
// a constant expression.

#include <crucible/ExprPool.h>
#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr crucible::ExprPool::IntCacheLiteral bad =
        ::fixy::mint_refined<crucible::ExprPool::kIntCacheRange>(std::int64_t{-129});
    (void)bad;
}
