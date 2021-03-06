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
int clients_context_get_arg_int(session_context *context, const char *arg);
zhandle_t *clients_context_take_handle(session_context *context);
void clients_context_put_handle(session_context *context);
/* get handle without taking the lock */
zhandle_t *clients_context_handle(session_context *context);

#endif
