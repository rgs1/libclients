#ifndef _CLIENTS_H_
#define _CLIENTS_H_

#include <zookeeper.h>


typedef struct clients clients;
typedef struct session_context session_context;

clients * clients_new(void (*)(zhandle_t *, int, int, const char *),
                      void *(*)(void),
                      void (*)(void *));
void clients_run(clients *c, int argc, const char **argv);
void clients_add_arg(clients *c, const char *name, char chr, const char *value, const char *desc);
void *clients_context_data(session_context *context);
const char *clients_context_get_arg(session_context *context, const char *arg);

#endif
