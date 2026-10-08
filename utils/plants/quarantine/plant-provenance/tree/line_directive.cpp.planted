// A #line directive changes the file and the line of the code after it.  The
// pointer after it gives no finding with the plugin of main, because the
// file elsewhere.cpp does not exist and is outside the root.  The plugin
// refuses the directive in each file under the root.

inline int* before_directive = nullptr;

#line 100 "elsewhere.cpp"
inline int* after_directive = nullptr;
