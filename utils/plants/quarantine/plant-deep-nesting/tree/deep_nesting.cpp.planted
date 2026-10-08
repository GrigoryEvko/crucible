// A library type and a C library variable behind deep nesting.  The type of
// deep_vector holds std::vector behind 70 template wrappers, and the token
// __errno_location of the macro errno comes behind 70 macros of this file.
// The plugin of main stopped each walk at 64 levels and gave no finding.  The
// plugin reads each walk to its end.

#include <cerrno>
#include <vector>

template <class T>
struct DeepWrap {
    T value;
};

template <int Depth, class T>
struct DeepNest {
    using type = DeepWrap<typename DeepNest<Depth - 1, T>::type>;
};

template <class T>
struct DeepNest<0, T> {
    using type = T;
};

DeepNest<70, std::vector<int>>::type deep_vector;

#define DEEP1(x) x
#define DEEP2(x) DEEP1(x)
#define DEEP3(x) DEEP2(x)
#define DEEP4(x) DEEP3(x)
#define DEEP5(x) DEEP4(x)
#define DEEP6(x) DEEP5(x)
#define DEEP7(x) DEEP6(x)
#define DEEP8(x) DEEP7(x)
#define DEEP9(x) DEEP8(x)
#define DEEP10(x) DEEP9(x)
#define DEEP11(x) DEEP10(x)
#define DEEP12(x) DEEP11(x)
#define DEEP13(x) DEEP12(x)
#define DEEP14(x) DEEP13(x)
#define DEEP15(x) DEEP14(x)
#define DEEP16(x) DEEP15(x)
#define DEEP17(x) DEEP16(x)
#define DEEP18(x) DEEP17(x)
#define DEEP19(x) DEEP18(x)
#define DEEP20(x) DEEP19(x)
#define DEEP21(x) DEEP20(x)
#define DEEP22(x) DEEP21(x)
#define DEEP23(x) DEEP22(x)
#define DEEP24(x) DEEP23(x)
#define DEEP25(x) DEEP24(x)
#define DEEP26(x) DEEP25(x)
#define DEEP27(x) DEEP26(x)
#define DEEP28(x) DEEP27(x)
#define DEEP29(x) DEEP28(x)
#define DEEP30(x) DEEP29(x)
#define DEEP31(x) DEEP30(x)
#define DEEP32(x) DEEP31(x)
#define DEEP33(x) DEEP32(x)
#define DEEP34(x) DEEP33(x)
#define DEEP35(x) DEEP34(x)
#define DEEP36(x) DEEP35(x)
#define DEEP37(x) DEEP36(x)
#define DEEP38(x) DEEP37(x)
#define DEEP39(x) DEEP38(x)
#define DEEP40(x) DEEP39(x)
#define DEEP41(x) DEEP40(x)
#define DEEP42(x) DEEP41(x)
#define DEEP43(x) DEEP42(x)
#define DEEP44(x) DEEP43(x)
#define DEEP45(x) DEEP44(x)
#define DEEP46(x) DEEP45(x)
#define DEEP47(x) DEEP46(x)
#define DEEP48(x) DEEP47(x)
#define DEEP49(x) DEEP48(x)
#define DEEP50(x) DEEP49(x)
#define DEEP51(x) DEEP50(x)
#define DEEP52(x) DEEP51(x)
#define DEEP53(x) DEEP52(x)
#define DEEP54(x) DEEP53(x)
#define DEEP55(x) DEEP54(x)
#define DEEP56(x) DEEP55(x)
#define DEEP57(x) DEEP56(x)
#define DEEP58(x) DEEP57(x)
#define DEEP59(x) DEEP58(x)
#define DEEP60(x) DEEP59(x)
#define DEEP61(x) DEEP60(x)
#define DEEP62(x) DEEP61(x)
#define DEEP63(x) DEEP62(x)
#define DEEP64(x) DEEP63(x)
#define DEEP65(x) DEEP64(x)
#define DEEP66(x) DEEP65(x)
#define DEEP67(x) DEEP66(x)
#define DEEP68(x) DEEP67(x)
#define DEEP69(x) DEEP68(x)
#define DEEP70(x) DEEP69(x)

int deep_error() { return DEEP70(errno); }
