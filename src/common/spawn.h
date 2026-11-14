#ifndef A4_SPAWN_H
#define A4_SPAWN_H

#include <stddef.h>

/* Replace this process with the program at path, passing arg1 as argv[1].
 * Returns only if the spawn failed. NOTE: the PSL1GHT sysProcessExitSpawn2
 * wrapper returns void, so no kernel error code is available on failure;
 * callers must pre-validate with a4_spawn_check and treat any return as
 * failure. */

void a4_spawn(const char* path, const char* arg1);

/* Pre-flight check for a spawn target: the file must exist, be non-empty,
 * and start with the SELF magic ("SCE\0", 53 43 45 00). Returns 1 when
 * the target looks spawnable. Always fills detail with a short verdict. */
int a4_spawn_check(const char* path, char* detail, size_t detail_size);

#endif
