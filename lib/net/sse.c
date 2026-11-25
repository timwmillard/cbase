#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MAX (1u << 20)

void sse_init(sse_parser *p, const sse_desc *desc) {
   memset(p, 0, sizeof(*p));
   p->on_event = desc->on_event;
   p->user_data = desc->user_data;
   p->max = desc->max ? desc->max : DEFAULT_MAX;
}

void sse_free(sse_parser *p) {
   free(p->line);
   free(p->data);
   memset(p, 0, sizeof(*p));
}

static bool sse_reserve(char **buf, size_t *cap, size_t need) {
   if (need <= *cap)
      return true;
   size_t n = *cap ? *cap : 256;
   while (n < need)
      n *= 2;
   char *b = realloc(*buf, n);
   if (!b)
      return false;
   *buf = b;
   *cap = n;
   return true;
}

static bool line_done(sse_parser *p) {
   const char *line = p->line;
   size_t len = p->line_len;
   p->line_len = 0;

   if (len == 0) {
      // A blank line dispatches the event, if it has data.
      if (p->data_len) {
         p->data_len--; // the last data line's "\n"
         p->data[p->data_len] = '\0';
         if (p->on_event)
            p->on_event(p->event[0] ? p->event : "message", p->data,
                        p->data_len, p->user_data);
      }
      p->data_len = 0;
      p->event[0] = '\0';
      return true;
   }
   if (line[0] == ':')
      return true; // a comment, e.g. a keep-alive

   const char *colon = memchr(line, ':', len);
   size_t name_len = colon ? (size_t)(colon - line) : len;
   const char *value = colon ? colon + 1 : line + len;
   if (value < line + len && *value == ' ')
      value++;
   size_t value_len = (size_t)(line + len - value);

   if (name_len == 4 && memcmp(line, "data", 4) == 0) {
      if (p->data_len + value_len + 1 > p->max ||
          !sse_reserve(&p->data, &p->data_cap, p->data_len + value_len + 2))
         return false;
      memcpy(p->data + p->data_len, value, value_len);
      p->data_len += value_len;
      p->data[p->data_len++] = '\n';
   } else if (name_len == 5 && memcmp(line, "event", 5) == 0) {
      snprintf(p->event, sizeof(p->event), "%.*s", (int)value_len, value);
   }
   // id and retry are for reconnecting, which is the caller's job.
   return true;
}

bool sse_feed(sse_parser *p, const char *in, size_t len) {
   for (size_t i = 0; i < len; i++) {
      char c = in[i];
      if (p->after_cr) {
         p->after_cr = false;
         if (c == '\n')
            continue;
      }
      if (c == '\r' || c == '\n') {
         p->after_cr = c == '\r';
         if (!line_done(p))
            return false;
         continue;
      }
      // Take the rest of the line in one go.
      size_t end = i;
      while (end < len && in[end] != '\r' && in[end] != '\n')
         end++;
      size_t n = end - i;
      if (p->line_len + n > p->max ||
          !sse_reserve(&p->line, &p->line_cap, p->line_len + n))
         return false;
      memcpy(p->line + p->line_len, in + i, n);
      p->line_len += n;
      i = end - 1;
   }
   return true;
}
