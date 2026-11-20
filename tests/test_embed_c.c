#include "ax_embed.h"

#include <string.h>

int main(void) {
  const char* valid = "fn main() -> i32:\n    return 0\n";
  AxEmbedBuffer buffer = {0};
  if (ax_embed_check(valid, "embed-valid.ax", &buffer) != 0) return 1;
  ax_embed_buffer_release(&buffer);

  if (ax_embed_emit_ir(valid, "embed-valid.ax", &buffer) != 0) return 2;
  if (buffer.data == NULL || strstr(buffer.data, "func main") == NULL) return 3;
  ax_embed_buffer_release(&buffer);

  if (ax_embed_emit_asm(valid, "embed-valid.ax", "x86_64", &buffer) != 0) return 4;
  if (buffer.data == NULL || strstr(buffer.data, ".globl main") == NULL) return 5;
  ax_embed_buffer_release(&buffer);

  if (ax_embed_check("fn main() -> i32:\n    return missing_name\n", "embed-invalid.ax", &buffer) == 0) return 6;
  if (buffer.data == NULL || strstr(buffer.data, "embed-invalid.ax") == NULL) return 7;
  ax_embed_buffer_release(&buffer);
  return 0;
}
