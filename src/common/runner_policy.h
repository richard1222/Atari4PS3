#ifndef A4_RUNNER_POLICY_H
#define A4_RUNNER_POLICY_H
#include "common/paths.h"
#include <string.h>
inline const char* a4_runner_save_dir(const char* core,const char* hint,const char* fallback) {
  if(core && hint && strcmp(core,"atari800")==0 && strcmp(hint,"800XL (64K)")==0)
    return A4_SAVES_8BIT;
  return fallback;
}
#endif
