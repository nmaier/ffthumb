#ifndef THUMB_H
#define THUMB_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
typedef struct _ThumbContext ThumbContext;

typedef ThumbContext * (*pffthumb_create)(const char *filename);
typedef void (*pffthumb_free)(ThumbContext** ctx);
typedef int64_t (*pffthumb_load_frame)(ThumbContext *ctx, double position, char **buffer);
typedef void (*pffthumb_free_frame)(ThumbContext *ctx, char** buffer);
typedef const char* (*pffthumb_get_codec)(ThumbContext *ctx);
typedef double (*pffthumb_get_duration)(ThumbContext *ctx);
typedef long (*pffthumb_get_width)(ThumbContext *ctx);
typedef long (*pffthumb_get_height)(ThumbContext *ctx);

typedef struct _Thumber {
  pffthumb_create create;
  pffthumb_free free;
  pffthumb_load_frame load_frame;
  pffthumb_free_frame free_frame;
  pffthumb_get_codec get_codec;
  pffthumb_get_duration get_duration;
  pffthumb_get_width get_width;
  pffthumb_get_height get_height;
} Thumber;

typedef const Thumber* (*pffthumb_init)(int log);

const Thumber* ffthumb_init(int log);
ThumbContext * ffthumb_create(const char *filename);
void ffthumb_free(ThumbContext** ctx);
int64_t ffthumb_load_frame(ThumbContext *ctx, double position, char **buffer);
void ffthumb_free_frame(ThumbContext *ctx, char** buffer);
const char* ffthumb_get_codec(ThumbContext *ctx);
double ffthumb_get_duration(ThumbContext *ctx);
long ffthumb_get_width(ThumbContext *ctx);
long ffthumb_get_height(ThumbContext *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* THUMB_H */
