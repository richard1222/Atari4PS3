#ifndef A4_FSUTIL_H
#define A4_FSUTIL_H

#include <stddef.h>

bool a4_mkdir_p(const char* path);
bool a4_ensure_dirs(void);
bool a4_is_file(const char* path);
bool a4_read_file(const char* path, unsigned char** out_data, size_t* out_size);
bool a4_write_file(const char* path, const void* data, size_t size);

#endif
