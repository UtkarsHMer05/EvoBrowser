// Portable posix_spawn "chdir" file action for the distributed tests (M38 CI).
//
// macOS/BSD provide POSIX `posix_spawn_file_actions_addchdir`; Linux glibc
// provides the same function under the `_np` (non-portable) suffix
// (`posix_spawn_file_actions_addchdir_np`, glibc >= 2.29). This header hides
// the difference so the e2e / crash-recovery / scheduler-restart tests compile
// on both platforms.
#pragma once

#include <spawn.h>

inline int evo_spawn_addchdir(posix_spawn_file_actions_t* actions,
                              const char* path) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || \
    defined(__NetBSD__)
  return posix_spawn_file_actions_addchdir(actions, path);
#else
  return posix_spawn_file_actions_addchdir_np(actions, path);
#endif
}
