#ifndef SOH_RS_ASSERT_H
#define SOH_RS_ASSERT_H

// One compile-time assert macro usable from both the C++ core and the C actors that include the
// rs/ headers. soh compiles C as C23 (soh/CMakeLists.txt -> MSVC /std:clatest), where
// _Static_assert is a keyword; verified against MSVC 19.44 on 2026-09-01 (the older in-tree note
// in custom/scenes/CustomSceneData.h that MSVC's C mode "does not reliably take static_assert"
// predates that compiler). Note the warning split: the vanilla `src/` C files compile with /w
// (WARNING_OVERRIDE in the top-level CMakeLists), but `soh/*.c` - custom scenes and any C actor that
// includes these headers - gets the target-wide /W3 /WX. Either way a *failed* assert is a hard
// error, which is exactly the behaviour wanted for an ID-band violation.
#ifdef __cplusplus
#define RS_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define RS_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#endif // SOH_RS_ASSERT_H
