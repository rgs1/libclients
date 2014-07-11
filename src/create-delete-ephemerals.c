/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/*
 * Create & delete ephemerals, in a loop
 */

#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <small/util.h>
#include <small/queue.h>
#include <small/worker.h>

#include <clients/clients.h>

typedef struct watcher_data watcher_data;

struct watcher_data {
  worker *w;
};

static void create_cb(int rc, const char *path, const void *data)
{
  if (!rc) {
    queue *q = (queue *)data;
    info("Created %s", path);
    queue_add(q, strdup(path));
  } else
    warn("Create failed with rc=%d", rc);
}

static void delete_cb(int rc, const void *data)
{
  rc == 0 ?
    info("Delete was successful") :
    warn("Delete failed w/ rc = %d", rc);
}

static void *loop(void *data)
{
  const char *path;
  int count, sleep_time, i, rc;
  worker *w = (worker *)data;
  session_context *context = (session_context *)worker_get_data(w);
  queue *q;
  zhandle_t *zh;

  path = clients_context_get_arg(context, "path");
  count = clients_context_get_arg_int(context, "ephemeral-count");
  sleep_time = clients_context_get_arg_int(context, "sleep-before-ops");

  q = queue_new(count);

  while (1) {
    /* create ephemerals */

    zh = clients_context_take_handle(context); /* gets exclusive access to zhandle */

    for (i = 0; i < count; i++) {
      rc = zoo_acreate(zh,
                       path,
                       "test",
                       4,
                       &ZOO_OPEN_ACL_UNSAFE,
                       ZOO_EPHEMERAL|ZOO_SEQUENCE,
                       create_cb,
                       q);

      if (rc)
        warn("Failed to issue create request");
    }

    clients_context_put_handle(context); /* releases zhandle */

    sleep(sleep_time);

    /* now delete them.. */

    zh = clients_context_take_handle(context); /* gets exclusive access to zhandle */

    while (!queue_empty(q)) {
      char *p = queue_remove(q);

      if (!p) {
        warn("Got null path?");
        continue;
      }

      rc = zoo_adelete(zh, p, -1, delete_cb, NULL);
      if (rc)
        warn("Failed to issue delete request");

      free(p);
    }

    clients_context_put_handle(context); /* releases zhandle */

    sleep(sleep_time);
  }

  return NULL;
}

static void *new_data(void)
{
  watcher_data *wdata = (watcher_data *)safe_alloc(sizeof(watcher_data));
  return wdata;
}

static void reset_data(void *data)
{
  watcher_data *wdata = (watcher_data *)data;
  if (wdata->w)
    worker_destroy(wdata->w);
  wdata->w = NULL;
}

static void watcher(zhandle_t *zzh, int type, int state, const char *path)
{
  session_context *context;
  watcher_data *wdata;

  if (type != ZOO_SESSION_EVENT)
    return;

  if (state != ZOO_CONNECTED_STATE)
    return;

  context = (session_context *)zoo_get_context(zzh);
  wdata = (watcher_data *)clients_context_data(context);

  if (wdata->w)
    return;

  wdata->w = worker_create(&loop, context);
}

int main(int argc, const char **argv)
{
  clients *c;

  c = clients_new(&watcher, &new_data, &reset_data);

  clients_add_arg(c, "path", 'P', "/yada", "parent path");
  clients_add_arg(c, "ephemeral-count", 'Q', "10", "how many ephemerals to create");
  clients_add_arg(c, "sleep-before-ops", 'D', "5", "secs to sleep before issuing creates/deletes");

  clients_run(c, argc, argv);

  return 0;
}
