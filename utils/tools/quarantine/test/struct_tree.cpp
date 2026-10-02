// A struct tree whose walk doubles at each level: each pair holds the pair
// below it as both members.  The restriction plain-result of std::bit_cast
// reads the result type.  The plugin of main visited each path of the tree,
// 2 to the power 40 visits, which takes hours, and GCC alone compiles the
// file in less than a second.  The plugin keeps the answer of each class, so
// the walk visits each class one time.

#include <bit>

template <class Left, class Right>
struct TreePair {
    Left left;
    Right right;
};

using Tree0 = unsigned char;
using Tree1 = TreePair<Tree0, Tree0>;
using Tree2 = TreePair<Tree1, Tree1>;
using Tree3 = TreePair<Tree2, Tree2>;
using Tree4 = TreePair<Tree3, Tree3>;
using Tree5 = TreePair<Tree4, Tree4>;
using Tree6 = TreePair<Tree5, Tree5>;
using Tree7 = TreePair<Tree6, Tree6>;
using Tree8 = TreePair<Tree7, Tree7>;
using Tree9 = TreePair<Tree8, Tree8>;
using Tree10 = TreePair<Tree9, Tree9>;
using Tree11 = TreePair<Tree10, Tree10>;
using Tree12 = TreePair<Tree11, Tree11>;
using Tree13 = TreePair<Tree12, Tree12>;
using Tree14 = TreePair<Tree13, Tree13>;
using Tree15 = TreePair<Tree14, Tree14>;
using Tree16 = TreePair<Tree15, Tree15>;
using Tree17 = TreePair<Tree16, Tree16>;
using Tree18 = TreePair<Tree17, Tree17>;
using Tree19 = TreePair<Tree18, Tree18>;
using Tree20 = TreePair<Tree19, Tree19>;
using Tree21 = TreePair<Tree20, Tree20>;
using Tree22 = TreePair<Tree21, Tree21>;
using Tree23 = TreePair<Tree22, Tree22>;
using Tree24 = TreePair<Tree23, Tree23>;
using Tree25 = TreePair<Tree24, Tree24>;
using Tree26 = TreePair<Tree25, Tree25>;
using Tree27 = TreePair<Tree26, Tree26>;
using Tree28 = TreePair<Tree27, Tree27>;
using Tree29 = TreePair<Tree28, Tree28>;
using Tree30 = TreePair<Tree29, Tree29>;
using Tree31 = TreePair<Tree30, Tree30>;
using Tree32 = TreePair<Tree31, Tree31>;
using Tree33 = TreePair<Tree32, Tree32>;
using Tree34 = TreePair<Tree33, Tree33>;
using Tree35 = TreePair<Tree34, Tree34>;
using Tree36 = TreePair<Tree35, Tree35>;
using Tree37 = TreePair<Tree36, Tree36>;
using Tree38 = TreePair<Tree37, Tree37>;
using Tree39 = TreePair<Tree38, Tree38>;
using Tree40 = TreePair<Tree39, Tree39>;

struct TreeSource {
    unsigned char bytes[1ul << 40];
};

Tree40 tree_from_source(TreeSource const& source) { return std::bit_cast<Tree40>(source); }
