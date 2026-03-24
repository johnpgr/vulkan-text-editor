#include "base/base_mod.h"

#if OS_WINDOWS
#include "base/threads/threads_win32.cpp"
#else
#include "base/threads/threads_posix.cpp"
#endif
