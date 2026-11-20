#ifndef AX_EMBED_H
#define AX_EMBED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AxEmbedBuffer {
  char* data;
  size_t length;
} AxEmbedBuffer;

/* Returns 0 on success, 1 for source diagnostics, and 2 for API/internal errors. */
int ax_embed_check(const char* source, const char* source_name, AxEmbedBuffer* diagnostics);
int ax_embed_emit_ir(const char* source, const char* source_name, AxEmbedBuffer* output);
int ax_embed_emit_asm(const char* source, const char* source_name,
                      const char* target, AxEmbedBuffer* output);
void ax_embed_buffer_release(AxEmbedBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif
