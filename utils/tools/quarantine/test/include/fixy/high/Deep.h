// A file of the language with a type whose walk doubles at each level: each
// pair holds the pair below it as both template arguments.  The plugin of
// main kept no answer of a type walk in a file of the language, so it read
// each path of the type, 2 to the power 40 visits, and did not stop.

#pragma once

namespace fixy::language_test {

template <class Left, class Right>
struct DeepPair {
    Left left;
    Right right;
};

using Deep0 = int;
using Deep1 = DeepPair<Deep0, Deep0>;
using Deep2 = DeepPair<Deep1, Deep1>;
using Deep3 = DeepPair<Deep2, Deep2>;
using Deep4 = DeepPair<Deep3, Deep3>;
using Deep5 = DeepPair<Deep4, Deep4>;
using Deep6 = DeepPair<Deep5, Deep5>;
using Deep7 = DeepPair<Deep6, Deep6>;
using Deep8 = DeepPair<Deep7, Deep7>;
using Deep9 = DeepPair<Deep8, Deep8>;
using Deep10 = DeepPair<Deep9, Deep9>;
using Deep11 = DeepPair<Deep10, Deep10>;
using Deep12 = DeepPair<Deep11, Deep11>;
using Deep13 = DeepPair<Deep12, Deep12>;
using Deep14 = DeepPair<Deep13, Deep13>;
using Deep15 = DeepPair<Deep14, Deep14>;
using Deep16 = DeepPair<Deep15, Deep15>;
using Deep17 = DeepPair<Deep16, Deep16>;
using Deep18 = DeepPair<Deep17, Deep17>;
using Deep19 = DeepPair<Deep18, Deep18>;
using Deep20 = DeepPair<Deep19, Deep19>;
using Deep21 = DeepPair<Deep20, Deep20>;
using Deep22 = DeepPair<Deep21, Deep21>;
using Deep23 = DeepPair<Deep22, Deep22>;
using Deep24 = DeepPair<Deep23, Deep23>;
using Deep25 = DeepPair<Deep24, Deep24>;
using Deep26 = DeepPair<Deep25, Deep25>;
using Deep27 = DeepPair<Deep26, Deep26>;
using Deep28 = DeepPair<Deep27, Deep27>;
using Deep29 = DeepPair<Deep28, Deep28>;
using Deep30 = DeepPair<Deep29, Deep29>;
using Deep31 = DeepPair<Deep30, Deep30>;
using Deep32 = DeepPair<Deep31, Deep31>;
using Deep33 = DeepPair<Deep32, Deep32>;
using Deep34 = DeepPair<Deep33, Deep33>;
using Deep35 = DeepPair<Deep34, Deep34>;
using Deep36 = DeepPair<Deep35, Deep35>;
using Deep37 = DeepPair<Deep36, Deep36>;
using Deep38 = DeepPair<Deep37, Deep37>;
using Deep39 = DeepPair<Deep38, Deep38>;
using Deep40 = DeepPair<Deep39, Deep39>;

inline Deep40* deep_pointer = nullptr;

}  // namespace fixy::language_test
