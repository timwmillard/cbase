/* slog.h - v0.1 - structured logging for C

   This is a single-header-file STB-style library that provides structured
   logging in C, inspired by Go's slog package.

   To use this library, do this in *ONE* C file:
      #define SLOG_IMPLEMENTATION
      #include "slog.h"

   LICENSE
     MIT License - See end of file for full license text.

   USAGE
     Basic usage:
       // Create a text handler
       slog_handler *handler = slog_text_handler_new(stdout, SLOG_INFO);

       // Create a logger
       slog_logger *logger = slog_new(handler);

       // Log with attributes
       slog_log_info(logger, "User logged in",
                     slog_string("username", "alice"),
                     slog_int("user_id", 123),
                     NULL);

       // Set as default logger
       slog_set_default(logger);

       // Use default logger
       slog_info("Server started",
                 slog_int("port", 8080),
                 NULL);
*/

#ifndef SLOG_H
#define SLOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels
typedef enum {
   SLOG_DEBUG = -4,
   SLOG_INFO = 0,
   SLOG_WARN = 4,
   SLOG_ERROR = 8
} slog_level;

// Attribute types
typedef enum {
   SLOG_TYPE_STRING,
   SLOG_TYPE_BYTES, // String with explicit length
   SLOG_TYPE_INT,
   SLOG_TYPE_INT64,
   SLOG_TYPE_UINT,
   SLOG_TYPE_UINT64,
   SLOG_TYPE_FLOAT,
   SLOG_TYPE_DOUBLE,
   SLOG_TYPE_BOOL,
   SLOG_TYPE_TIME,
   SLOG_TYPE_DURATION,
   SLOG_TYPE_GROUP
} slog_type;

// Value union for attributes
typedef union {
   const char *string_val;
   struct {
      const char *data;
      size_t len;
   } bytes_val;
   int int_val;
   int64_t int64_val;
   unsigned int uint_val;
   uint64_t uint64_val;
   float float_val;
   double double_val;
   bool bool_val;
   time_t time_val;
   int64_t duration_val; // in nanoseconds
   struct slog_attr *group_val;
} slog_value;

// Attribute structure
typedef struct slog_attr {
   const char *key;
   slog_type type;
   slog_value value;
} slog_attr;

// Record structure
typedef struct slog_record {
   time_t time;
   int time_ms; // milliseconds (0-999)
   const char *message;
   slog_level level;
   const char *file;
   int line;
   const char *function;
   slog_attr *attrs;
   size_t attr_count;
} slog_record;

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION slog_mutex;
#else
#include <pthread.h>
typedef pthread_mutex_t slog_mutex;
#endif

// Handler interface
typedef struct slog_handler {
   void (*handle)(struct slog_handler *self, const slog_record *record);
   bool (*enabled)(struct slog_handler *self, slog_level level);
   struct slog_handler *(*with_attrs)(struct slog_handler *self,
                                      const slog_attr *attrs, size_t count);
   struct slog_handler *(*with_group)(struct slog_handler *self,
                                      const char *name);
   void (*destroy)(struct slog_handler *self);

   // handler data
   FILE *output;
   slog_level min_level;
   slog_mutex mutex;
   void *data; // handler-specific data
} slog_handler;

// Logger structure
typedef struct slog_logger {
   slog_handler *handler;
   slog_attr *attrs;
   size_t attr_count;
} slog_logger;

// Global logger functions
void slog_set_default(slog_logger *logger);
slog_logger *slog_get_default(void);

// Logger creation
slog_logger *slog_new(slog_handler *handler);
slog_logger *slog_with(slog_logger *logger, ...); // Variadic attrs
slog_logger *slog_with_attrs(slog_logger *logger, const slog_attr *attrs,
                             size_t count);
slog_logger *slog_with_group(slog_logger *logger, const char *name);
void slog_destroy(slog_logger *logger);

// Logger-specific logging functions (implementation)
void slog_log_impl(slog_logger *logger, slog_level level, const char *file,
                   int line, const char *func, const char *msg, ...);
void slog_log_debug_impl(slog_logger *logger, const char *file, int line,
                         const char *func, const char *msg, ...);
void slog_log_info_impl(slog_logger *logger, const char *file, int line,
                        const char *func, const char *msg, ...);
void slog_log_warn_impl(slog_logger *logger, const char *file, int line,
                        const char *func, const char *msg, ...);
void slog_log_error_impl(slog_logger *logger, const char *file, int line,
                         const char *func, const char *msg, ...);

// Default logger functions (implementation)
void slog_debug_impl(const char *file, int line, const char *func,
                     const char *msg, ...);
void slog_info_impl(const char *file, int line, const char *func,
                    const char *msg, ...);
void slog_warn_impl(const char *file, int line, const char *func,
                    const char *msg, ...);
void slog_error_impl(const char *file, int line, const char *func,
                     const char *msg, ...);

// Convenience macros that auto-append NULL and capture source location
#define slog_log(logger, level, ...)                                           \
   slog_log_impl(logger, level, __FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_log_debug(logger, ...)                                            \
   slog_log_debug_impl(logger, __FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_log_info(logger, ...)                                             \
   slog_log_info_impl(logger, __FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_log_warn(logger, ...)                                             \
   slog_log_warn_impl(logger, __FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_log_error(logger, ...)                                            \
   slog_log_error_impl(logger, __FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_debug(...)                                                        \
   slog_debug_impl(__FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_info(...)                                                         \
   slog_info_impl(__FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_warn(...)                                                         \
   slog_warn_impl(__FILE__, __LINE__, __func__, __VA_ARGS__, NULL)
#define slog_error(...)                                                        \
   slog_error_impl(__FILE__, __LINE__, __func__, __VA_ARGS__, NULL)

// Attribute constructors (macros that create compound literals)
#define slog_string(k, v)                                                      \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_STRING, .value.string_val = (v)})
#define slog_bytes(k, data, len)                                               \
   (&(slog_attr){.key = (k),                                                   \
                 .type = SLOG_TYPE_BYTES,                                      \
                 .value.bytes_val = {(data), (len)}})
#define slog_int(k, v)                                                         \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_INT, .value.int_val = (v)})
#define slog_int64(k, v)                                                       \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_INT64, .value.int64_val = (v)})
#define slog_uint(k, v)                                                        \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_UINT, .value.uint_val = (v)})
#define slog_uint64(k, v)                                                      \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_UINT64, .value.uint64_val = (v)})
#define slog_float(k, v)                                                       \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_FLOAT, .value.float_val = (v)})
#define slog_double(k, v)                                                      \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_DOUBLE, .value.double_val = (v)})
#define slog_bool(k, v)                                                        \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_BOOL, .value.bool_val = (v)})
#define slog_time(k, v)                                                        \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_TIME, .value.time_val = (v)})
#define slog_duration(k, v)                                                    \
   (&(slog_attr){                                                              \
       .key = (k), .type = SLOG_TYPE_DURATION, .value.duration_val = (v)})
#define slog_group(k, a, c)                                                    \
   (&(slog_attr){.key = (k), .type = SLOG_TYPE_GROUP, .value.group_val = (a)})

// Built-in handlers
slog_handler *slog_text_handler_new(FILE *output, slog_level min_level);
slog_handler *slog_json_handler_new(FILE *output, slog_level min_level);
slog_handler *slog_color_text_handler_new(FILE *output, slog_level min_level);

// Utility functions
const char *slog_level_string(slog_level level);
bool slog_enabled(slog_logger *logger, slog_level level);

#ifdef __cplusplus
}
#endif

#endif // SLOG_H

// Implementation
#ifdef SLOG_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

// Thread safety - use pthread on Unix, Windows threads on Windows
#ifdef _WIN32
#define slog_mutex_init(m)    InitializeCriticalSection(m)
#define slog_mutex_lock(m)    EnterCriticalSection(m)
#define slog_mutex_unlock(m)  LeaveCriticalSection(m)
#define slog_mutex_destroy(m) DeleteCriticalSection(m)
#else
#define slog_mutex_init(m)    pthread_mutex_init(m, NULL)
#define slog_mutex_lock(m)    pthread_mutex_lock(m)
#define slog_mutex_unlock(m)  pthread_mutex_unlock(m)
#define slog_mutex_destroy(m) pthread_mutex_destroy(m)
#endif

// Global default logger
static slog_logger *slog_default_logger = NULL;
static slog_mutex slog_default_logger_mutex;
static int slog_initialized = 0;

// Initialize on first use
static void slog_init_once(void) {
   if (!slog_initialized) {
      slog_mutex_init(&slog_default_logger_mutex);
      slog_initialized = 1;
   }
}

// Helper to duplicate attributes
static slog_attr *slog_duplicate_attrs(const slog_attr *attrs, size_t count) {
   if (count == 0 || attrs == NULL)
      return NULL;

   slog_attr *new_attrs = (slog_attr *)malloc(sizeof(slog_attr) * count);
   if (!new_attrs)
      return NULL;

   for (size_t i = 0; i < count; i++) {
      new_attrs[i] = attrs[i];
      // Deep copy strings
      if (attrs[i].type == SLOG_TYPE_STRING && attrs[i].value.string_val) {
         new_attrs[i].value.string_val = strdup(attrs[i].value.string_val);
      }
      // Deep copy bytes
      if (attrs[i].type == SLOG_TYPE_BYTES && attrs[i].value.bytes_val.data) {
         char *data_copy = (char *)malloc(attrs[i].value.bytes_val.len);
         if (data_copy) {
            memcpy(data_copy, attrs[i].value.bytes_val.data,
                   attrs[i].value.bytes_val.len);
            new_attrs[i].value.bytes_val.data = data_copy;
         }
      }
      if (attrs[i].key) {
         new_attrs[i].key = strdup(attrs[i].key);
      }
      // Deep copy groups would need recursive copying - simplified here
   }

   return new_attrs;
}

// Helper to free attributes
static void slog_free_attrs(slog_attr *attrs, size_t count) {
   if (!attrs)
      return;

   for (size_t i = 0; i < count; i++) {
      if (attrs[i].key) {
         free((void *)attrs[i].key);
      }
      if (attrs[i].type == SLOG_TYPE_STRING && attrs[i].value.string_val) {
         free((void *)attrs[i].value.string_val);
      }
      if (attrs[i].type == SLOG_TYPE_BYTES && attrs[i].value.bytes_val.data) {
         free((void *)attrs[i].value.bytes_val.data);
      }
      // Free groups recursively if needed
   }
   free(attrs);
}

// Global logger functions
void slog_set_default(slog_logger *logger) {
   slog_init_once();
   slog_mutex_lock(&slog_default_logger_mutex);
   if (slog_default_logger) {
      slog_destroy(slog_default_logger);
   }
   slog_default_logger = logger;
   slog_mutex_unlock(&slog_default_logger_mutex);
}

slog_logger *slog_get_default(void) {
   slog_init_once();
   slog_mutex_lock(&slog_default_logger_mutex);
   slog_logger *logger = slog_default_logger;
   slog_mutex_unlock(&slog_default_logger_mutex);
   return logger;
}

// Logger creation
slog_logger *slog_new(slog_handler *handler) {
   if (!handler)
      return NULL;

   slog_logger *logger = (slog_logger *)malloc(sizeof(slog_logger));
   if (!logger)
      return NULL;

   logger->handler = handler;
   logger->attrs = NULL;
   logger->attr_count = 0;

   return logger;
}

slog_logger *slog_with_attrs(slog_logger *logger, const slog_attr *attrs,
                             size_t count) {
   if (!logger || count == 0)
      return logger;

   slog_logger *new_logger = (slog_logger *)malloc(sizeof(slog_logger));
   if (!new_logger)
      return NULL;

   // Create new handler with attributes
   if (logger->handler && logger->handler->with_attrs) {
      new_logger->handler =
          logger->handler->with_attrs(logger->handler, attrs, count);
   } else {
      new_logger->handler = logger->handler;
   }

   // Combine existing and new attributes
   size_t total_attrs = logger->attr_count + count;
   new_logger->attrs = (slog_attr *)malloc(sizeof(slog_attr) * total_attrs);
   if (!new_logger->attrs) {
      free(new_logger);
      return NULL;
   }

   // Copy existing attributes
   if (logger->attr_count > 0) {
      memcpy(new_logger->attrs, logger->attrs,
             sizeof(slog_attr) * logger->attr_count);
   }

   // Copy new attributes
   memcpy(new_logger->attrs + logger->attr_count, attrs,
          sizeof(slog_attr) * count);
   new_logger->attr_count = total_attrs;

   return new_logger;
}

slog_logger *slog_with(slog_logger *logger, ...) {
   va_list args;
   va_start(args, logger);

   // Count attributes
   size_t count = 0;
   while (va_arg(args, slog_attr *) != NULL) {
      count++;
   }
   va_end(args);

   if (count == 0)
      return logger;

   // Collect attributes
   slog_attr *attrs = (slog_attr *)malloc(sizeof(slog_attr) * count);
   if (!attrs)
      return logger;

   va_start(args, logger);
   slog_attr *attr;
   for (size_t i = 0; i < count; i++) {
      attr = va_arg(args, slog_attr *);
      if (!attr)
         break;
      attrs[i] = *attr;
   }
   va_end(args);

   slog_logger *new_logger = slog_with_attrs(logger, attrs, count);
   free(attrs);

   return new_logger;
}

slog_logger *slog_with_group(slog_logger *logger, const char *name) {
   if (!logger || !name)
      return logger;

   slog_logger *new_logger = (slog_logger *)malloc(sizeof(slog_logger));
   if (!new_logger)
      return NULL;

   if (logger->handler && logger->handler->with_group) {
      new_logger->handler = logger->handler->with_group(logger->handler, name);
   } else {
      new_logger->handler = logger->handler;
   }

   new_logger->attrs = slog_duplicate_attrs(logger->attrs, logger->attr_count);
   new_logger->attr_count = logger->attr_count;

   return new_logger;
}

void slog_destroy(slog_logger *logger) {
   if (!logger)
      return;

   if (logger->handler && logger->handler->destroy) {
      logger->handler->destroy(logger->handler);
   }

   slog_free_attrs(logger->attrs, logger->attr_count);
   free(logger);
}

// Core logging function
static void slog_log_va(slog_logger *logger, slog_level level, const char *msg,
                        const char *file, int line, const char *func,
                        va_list args) {
   if (!logger || !logger->handler)
      return;

   // Check if enabled
   if (logger->handler->enabled &&
       !logger->handler->enabled(logger->handler, level)) {
      return;
   }

   // Count variadic attributes
   va_list args_copy;
   va_copy(args_copy, args);
   size_t var_attr_count = 0;
   while (va_arg(args_copy, slog_attr *) != NULL) {
      var_attr_count++;
   }
   va_end(args_copy);

   // Create record
   slog_record record = {0};
#ifdef _WIN32
   FILETIME ft;
   GetSystemTimeAsFileTime(&ft);
   ULARGE_INTEGER uli;
   uli.LowPart = ft.dwLowDateTime;
   uli.HighPart = ft.dwHighDateTime;
   // Convert to Unix time (100ns intervals since 1601 to seconds since 1970)
   uint64_t ns100 = uli.QuadPart - 116444736000000000ULL;
   record.time = (time_t)(ns100 / 10000000ULL);
   record.time_ms = (int)((ns100 / 10000ULL) % 1000);
#else
   struct timespec ts;
   clock_gettime(CLOCK_REALTIME, &ts);
   record.time = ts.tv_sec;
   record.time_ms = (int)(ts.tv_nsec / 1000000);
#endif
   record.message = msg;
   record.level = level;
   record.file = file;
   record.line = line;
   record.function = func;

   // Combine logger attrs and variadic attrs
   size_t total_attrs = logger->attr_count + var_attr_count;
   if (total_attrs > 0) {
      record.attrs = (slog_attr *)malloc(sizeof(slog_attr) * total_attrs);
      if (record.attrs) {
         // Copy logger attributes
         if (logger->attr_count > 0) {
            memcpy(record.attrs, logger->attrs,
                   sizeof(slog_attr) * logger->attr_count);
         }

         // Copy variadic attributes
         size_t idx = logger->attr_count;
         slog_attr *attr;
         while ((attr = va_arg(args, slog_attr *)) != NULL &&
                idx < total_attrs) {
            record.attrs[idx] = *attr;
            idx++;
         }
         record.attr_count = idx;
      }
   }

   // Call handler
   if (logger->handler->handle) {
      logger->handler->handle(logger->handler, &record);
   }

   // Cleanup
   if (record.attrs) {
      free(record.attrs);
   }
}

// Logger-specific logging functions (implementation)
void slog_log_impl(slog_logger *logger, slog_level level, const char *file,
                   int line, const char *func, const char *msg, ...) {
   va_list args;
   va_start(args, msg);
   slog_log_va(logger, level, msg, file, line, func, args);
   va_end(args);
}

#define SLOG_LOGGER_FUNC(name, level)                                          \
   void slog_log_##name##_impl(slog_logger *logger, const char *file,          \
                               int line, const char *func, const char *msg,    \
                               ...) {                                          \
      va_list args;                                                            \
      va_start(args, msg);                                                     \
      slog_log_va(logger, level, msg, file, line, func, args);                 \
      va_end(args);                                                            \
   }

SLOG_LOGGER_FUNC(debug, SLOG_DEBUG)
SLOG_LOGGER_FUNC(info, SLOG_INFO)
SLOG_LOGGER_FUNC(warn, SLOG_WARN)
SLOG_LOGGER_FUNC(error, SLOG_ERROR)

// Default logger functions (implementation)
#define SLOG_DEFAULT_FUNC(name, level)                                         \
   void slog_##name##_impl(const char *file, int line, const char *func,       \
                           const char *msg, ...) {                             \
      slog_logger *logger = slog_get_default();                                \
      if (!logger)                                                             \
         return;                                                               \
      va_list args;                                                            \
      va_start(args, msg);                                                     \
      slog_log_va(logger, level, msg, file, line, func, args);                 \
      va_end(args);                                                            \
   }

SLOG_DEFAULT_FUNC(debug, SLOG_DEBUG)
SLOG_DEFAULT_FUNC(info, SLOG_INFO)
SLOG_DEFAULT_FUNC(warn, SLOG_WARN)
SLOG_DEFAULT_FUNC(error, SLOG_ERROR)

// Utility functions
const char *slog_level_string(slog_level level) {
   switch (level) {
   case SLOG_DEBUG:
      return "DEBUB";
   case SLOG_INFO:
      return "INFO";
   case SLOG_WARN:
      return "WARN";
   case SLOG_ERROR:
      return "ERROR";
   default:
      return "????";
   }
}

bool slog_enabled(slog_logger *logger, slog_level level) {
   if (!logger || !logger->handler)
      return false;
   if (logger->handler->enabled) {
      return logger->handler->enabled(logger->handler, level);
   }
   return true; // Default to enabled
}

// Helper function to write escaped string for JSON
static void slog_write_json_escaped(FILE *output, const char *str, size_t len) {
   if (len == (size_t)-1)
      len = strlen(str);

   for (size_t i = 0; i < len; i++) {
      unsigned char c = str[i];
      switch (c) {
      case '"':
         fprintf(output, "\\\"");
         break;
      case '\\':
         fprintf(output, "\\\\");
         break;
      case '\b':
         fprintf(output, "\\b");
         break;
      case '\f':
         fprintf(output, "\\f");
         break;
      case '\n':
         fprintf(output, "\\n");
         break;
      case '\r':
         fprintf(output, "\\r");
         break;
      case '\t':
         fprintf(output, "\\t");
         break;
      default:
         if (c < 32) {
            fprintf(output, "\\u00%02x", (unsigned int)c);
         } else {
            fputc(c, output);
         }
         break;
      }
   }
}

// Helper function to write escaped string for text output
static void slog_write_text_escaped(FILE *output, const char *str, size_t len) {
   if (len == (size_t)-1)
      len = strlen(str);

   fputc('"', output);
   for (size_t i = 0; i < len; i++) {
      unsigned char c = str[i];
      switch (c) {
      case '"':
         fprintf(output, "\\\"");
         break;
      case '\\':
         fprintf(output, "\\\\");
         break;
      case '\n':
         fprintf(output, "\\n");
         break;
      case '\r':
         fprintf(output, "\\r");
         break;
      case '\t':
         fprintf(output, "\\t");
         break;
      default:
         if (c < 32 || c >= 127) {
            fprintf(output, "\\x%02x", c);
         } else {
            fputc(c, output);
         }
         break;
      }
   }
   fputc('"', output);
}

static void slog_text_handler_handle(slog_handler *self,
                                     const slog_record *record) {
   if (!self || !record)
      return;

   slog_mutex_lock(&self->mutex);

   // Format: time=2026-01-21T23:40:16.933+11:00 level=INFO msg="message"
   // key=value...
   struct tm *tm_info = localtime(&record->time);
   char time_buf[32];
   strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", tm_info);

   // Get timezone offset
   char tz_buf[8];
#ifdef _WIN32
   TIME_ZONE_INFORMATION tz_info;
   GetTimeZoneInformation(&tz_info);
   int tz_offset_min = -tz_info.Bias;
#else
   int tz_offset_min = (int)(tm_info->tm_gmtoff / 60);
#endif
   int tz_hours = tz_offset_min / 60;
   int tz_mins = abs(tz_offset_min % 60);
   snprintf(tz_buf, sizeof(tz_buf), "%+03d:%02d", tz_hours, tz_mins);

   fprintf(self->output, "time=%s.%03d%s level=%s msg=", time_buf,
           record->time_ms, tz_buf, slog_level_string(record->level));
   slog_write_text_escaped(self->output, record->message, -1);

   // Print source info for debug level
   if (record->level == SLOG_DEBUG) {
      fprintf(self->output, " source=\"%s:%d\"", record->file, record->line);
   }

   // Print attributes
   for (size_t i = 0; i < record->attr_count; i++) {
      fprintf(self->output, " %s=", record->attrs[i].key);

      switch (record->attrs[i].type) {
      case SLOG_TYPE_STRING:
         slog_write_text_escaped(self->output,
                                 record->attrs[i].value.string_val, -1);
         break;
      case SLOG_TYPE_BYTES:
         slog_write_text_escaped(self->output,
                                 record->attrs[i].value.bytes_val.data,
                                 record->attrs[i].value.bytes_val.len);
         break;
      case SLOG_TYPE_INT:
         fprintf(self->output, "%d", record->attrs[i].value.int_val);
         break;
      case SLOG_TYPE_INT64:
         fprintf(self->output, "%lld",
                 (long long)record->attrs[i].value.int64_val);
         break;
      case SLOG_TYPE_UINT:
         fprintf(self->output, "%u", record->attrs[i].value.uint_val);
         break;
      case SLOG_TYPE_UINT64:
         fprintf(self->output, "%llu",
                 (unsigned long long)record->attrs[i].value.uint64_val);
         break;
      case SLOG_TYPE_FLOAT:
         fprintf(self->output, "%f", record->attrs[i].value.float_val);
         break;
      case SLOG_TYPE_DOUBLE:
         fprintf(self->output, "%f", record->attrs[i].value.double_val);
         break;
      case SLOG_TYPE_BOOL:
         fprintf(self->output, "%s",
                 record->attrs[i].value.bool_val ? "true" : "false");
         break;
      case SLOG_TYPE_TIME:
         fprintf(self->output, "%ld", record->attrs[i].value.time_val);
         break;
      case SLOG_TYPE_DURATION:
         fprintf(self->output, "%lldns",
                 (long long)record->attrs[i].value.duration_val);
         break;
      case SLOG_TYPE_GROUP:
         fprintf(self->output, "{...}");
         break;
      }
   }

   fprintf(self->output, "\n");
   fflush(self->output);

   slog_mutex_unlock(&self->mutex);
}

static bool slog_text_handler_enabled(slog_handler *self, slog_level level) {
   if (!self)
      return false;
   return level >= self->min_level;
}

static void slog_text_handler_destroy(slog_handler *self) {
   if (!self)
      return;
   slog_mutex_destroy(&self->mutex);
   free(self);
}

slog_handler *slog_text_handler_new(FILE *output, slog_level min_level) {
   slog_handler *handler = (slog_handler *)malloc(sizeof(slog_handler));
   if (!handler)
      return NULL;

   handler->output = output ? output : stdout;
   handler->min_level = min_level;
   slog_mutex_init(&handler->mutex);

   handler->handle = slog_text_handler_handle;
   handler->enabled = slog_text_handler_enabled;
   handler->with_attrs = NULL; // Simplified - would need implementation
   handler->with_group = NULL; // Simplified - would need implementation
   handler->destroy = slog_text_handler_destroy;

   return handler;
}

// JSON handler implementation
static void slog_json_handler_handle(slog_handler *self,
                                     const slog_record *record) {
   if (!self || !record)
      return;

   slog_mutex_lock(&self->mutex);

   fprintf(self->output, "{");

   // Time (ISO 8601 format with milliseconds)
   char time_buf[32];
   strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S",
            localtime(&record->time));
   fprintf(self->output, "\"time\":\"%s.%03dZ\",", time_buf, record->time_ms);

   // Level
   fprintf(self->output, "\"level\":\"%s\",", slog_level_string(record->level));

   // Message
   fprintf(self->output, "\"msg\":\"");
   slog_write_json_escaped(self->output, record->message, -1);
   fprintf(self->output, "\"");

   // Attributes
   for (size_t i = 0; i < record->attr_count; i++) {
      fprintf(self->output, ",\"%s\":", record->attrs[i].key);

      switch (record->attrs[i].type) {
      case SLOG_TYPE_STRING:
         fprintf(self->output, "\"");
         slog_write_json_escaped(self->output,
                                 record->attrs[i].value.string_val, -1);
         fprintf(self->output, "\"");
         break;
      case SLOG_TYPE_BYTES:
         fprintf(self->output, "\"");
         slog_write_json_escaped(self->output,
                                 record->attrs[i].value.bytes_val.data,
                                 record->attrs[i].value.bytes_val.len);
         fprintf(self->output, "\"");
         break;
      case SLOG_TYPE_INT:
         fprintf(self->output, "%d", record->attrs[i].value.int_val);
         break;
      case SLOG_TYPE_INT64:
         fprintf(self->output, "%lld",
                 (long long)record->attrs[i].value.int64_val);
         break;
      case SLOG_TYPE_UINT:
         fprintf(self->output, "%u", record->attrs[i].value.uint_val);
         break;
      case SLOG_TYPE_UINT64:
         fprintf(self->output, "%llu",
                 (unsigned long long)record->attrs[i].value.uint64_val);
         break;
      case SLOG_TYPE_FLOAT:
         fprintf(self->output, "%f", record->attrs[i].value.float_val);
         break;
      case SLOG_TYPE_DOUBLE:
         fprintf(self->output, "%f", record->attrs[i].value.double_val);
         break;
      case SLOG_TYPE_BOOL:
         fprintf(self->output, "%s",
                 record->attrs[i].value.bool_val ? "true" : "false");
         break;
      case SLOG_TYPE_TIME:
         fprintf(self->output, "%ld", record->attrs[i].value.time_val);
         break;
      case SLOG_TYPE_DURATION:
         fprintf(self->output, "%lld",
                 (long long)record->attrs[i].value.duration_val);
         break;
      case SLOG_TYPE_GROUP:
         fprintf(self->output, "{}");
         break;
      }
   }

   if (record->level == SLOG_DEBUG) {
      // Source
      if (record->function && record->function[0] != '\0') {
         fprintf(
             self->output,
             ",\"source\":{\"file\":\"%s\",\"line\":%d,\"function\":\"%s\"}",
             record->file, record->line, record->function);
      } else {
         fprintf(self->output, ",\"source\":{\"file\":\"%s\",\"line\":%d}",
                 record->file, record->line);
      }
   }

   fprintf(self->output, "}\n");
   fflush(self->output);

   slog_mutex_unlock(&self->mutex);
}

slog_handler *slog_json_handler_new(FILE *output, slog_level min_level) {
   slog_handler *handler = (slog_handler *)malloc(sizeof(slog_handler));
   if (!handler)
      return NULL;

   handler->output = output ? output : stdout;
   handler->min_level = min_level;
   slog_mutex_init(&handler->mutex);

   handler->handle = slog_json_handler_handle;
   handler->enabled = slog_text_handler_enabled;
   handler->with_attrs = NULL;
   handler->with_group = NULL;
   handler->destroy = slog_text_handler_destroy;

   return handler;
}

// Color text handler implementation (humanlog-style)
// ANSI color codes
#define SLOG_COLOR_RESET   "\033[0m"
#define SLOG_COLOR_DIM     "\033[2m"
#define SLOG_COLOR_BOLD    "\033[1m"
#define SLOG_COLOR_RED     "\033[31m"
#define SLOG_COLOR_GREEN   "\033[32m"
#define SLOG_COLOR_YELLOW  "\033[33m"
#define SLOG_COLOR_BLUE    "\033[34m"
#define SLOG_COLOR_MAGENTA "\033[35m"
#define SLOG_COLOR_CYAN    "\033[36m"
#define SLOG_COLOR_WHITE   "\033[37m"

static const char *slog_level_color(slog_level level) {
   switch (level) {
   case SLOG_DEBUG:
      return SLOG_COLOR_BLUE;
   case SLOG_INFO:
      return SLOG_COLOR_CYAN;
   case SLOG_WARN:
      return SLOG_COLOR_YELLOW;
   case SLOG_ERROR:
      return SLOG_COLOR_RED;
   default:
      return SLOG_COLOR_RESET;
   }
}

static void slog_color_text_handler_handle(slog_handler *self,
                                           const slog_record *record) {
   if (!self || !record)
      return;

   slog_mutex_lock(&self->mutex);

   // Format: Jan 21 17:10:03.123 |INFO| Message key=value key=value...
   struct tm *tm_info = localtime(&record->time);
   char time_buf[32];
   strftime(time_buf, sizeof(time_buf), "%b %d %H:%M:%S", tm_info);

   // Get level color
   const char *level_color = slog_level_color(record->level);

   // Timestamp (dim) with milliseconds
   fprintf(self->output, "%s%s.%03d%s ", SLOG_COLOR_DIM, time_buf,
           record->time_ms, SLOG_COLOR_RESET);

   // Level with color and pipes (4 chars max)
   fprintf(self->output, "|%s%.4s%s| ", level_color,
           slog_level_string(record->level), SLOG_COLOR_RESET);

   // Message (white/normal)
   fprintf(self->output, "%s", record->message);

   // Print attributes with colors
   for (size_t i = 0; i < record->attr_count; i++) {
      // Key in cyan
      fprintf(self->output, " %s%s%s=", SLOG_COLOR_GREEN, record->attrs[i].key,
              SLOG_COLOR_RESET);

      // Value in green
      fprintf(self->output, "%s", SLOG_COLOR_DIM);

      switch (record->attrs[i].type) {
      case SLOG_TYPE_STRING:
         fprintf(self->output, "%s", record->attrs[i].value.string_val);
         break;
      case SLOG_TYPE_BYTES:
         fwrite(record->attrs[i].value.bytes_val.data, 1,
                record->attrs[i].value.bytes_val.len, self->output);
         break;
      case SLOG_TYPE_INT:
         fprintf(self->output, "%d", record->attrs[i].value.int_val);
         break;
      case SLOG_TYPE_INT64:
         fprintf(self->output, "%lld",
                 (long long)record->attrs[i].value.int64_val);
         break;
      case SLOG_TYPE_UINT:
         fprintf(self->output, "%u", record->attrs[i].value.uint_val);
         break;
      case SLOG_TYPE_UINT64:
         fprintf(self->output, "%llu",
                 (unsigned long long)record->attrs[i].value.uint64_val);
         break;
      case SLOG_TYPE_FLOAT:
         fprintf(self->output, "%f", record->attrs[i].value.float_val);
         break;
      case SLOG_TYPE_DOUBLE:
         fprintf(self->output, "%f", record->attrs[i].value.double_val);
         break;
      case SLOG_TYPE_BOOL:
         fprintf(self->output, "%s",
                 record->attrs[i].value.bool_val ? "true" : "false");
         break;
      case SLOG_TYPE_TIME:
         fprintf(self->output, "%ld", record->attrs[i].value.time_val);
         break;
      case SLOG_TYPE_DURATION:
         fprintf(self->output, "%lldns",
                 (long long)record->attrs[i].value.duration_val);
         break;
      case SLOG_TYPE_GROUP:
         fprintf(self->output, "{...}");
         break;
      }

      fprintf(self->output, "%s", SLOG_COLOR_RESET);
   }

   // Print source info for debug level
   if (record->level == SLOG_DEBUG) {
      fprintf(self->output, " %ssource%s=%s\"%s:%d\"%s", SLOG_COLOR_GREEN,
              SLOG_COLOR_RESET, SLOG_COLOR_DIM, record->file, record->line,
              SLOG_COLOR_RESET);
   }

   fprintf(self->output, "\n");
   fflush(self->output);

   slog_mutex_unlock(&self->mutex);
}

slog_handler *slog_color_text_handler_new(FILE *output, slog_level min_level) {
   slog_handler *handler = (slog_handler *)malloc(sizeof(slog_handler));
   if (!handler)
      return NULL;

   handler->output = output ? output : stdout;
   handler->min_level = min_level;
   slog_mutex_init(&handler->mutex);

   handler->handle = slog_color_text_handler_handle;
   handler->enabled = slog_text_handler_enabled;
   handler->with_attrs = NULL;
   handler->with_group = NULL;
   handler->destroy = slog_text_handler_destroy;

   return handler;
}

#endif // SLOG_IMPLEMENTATION

// Copyright 2026 Tim Millard
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
