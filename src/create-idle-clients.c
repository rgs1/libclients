/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/*
 * Create idle clients
 */

#include <small/util.h>

#include <clients/clients.h>

typedef struct watcher_data watcher_data;
struct watcher_data {
  char ready;
};

static void *new_data(void)
{
  watcher_data *wdata = (watcher_data *)safe_alloc(sizeof(watcher_data));
  return wdata;
}

static void reset_data(void *data)
{
  watcher_data *wdata = (watcher_data *)data;
  wdata->ready = 0;
}

static void watcher(zhandle_t *zzh, int type, int state, const char *path)
{
  if (type != ZOO_SESSION_EVENT)
    return;

  if (state != ZOO_CONNECTED_STATE)
    return;

  do {
    session_context *context;
    watcher_data *wdata;

    context = (session_context *)zoo_get_context(zzh);
    wdata = (watcher_data *)clients_context_data(context);
    wdata->ready = 1;
    info("Idle client ready");
  } while (0);
}

int main(int argc, const char **argv)
{
  clients *c;

  c = clients_new(&watcher, &new_data, &reset_data);

  clients_run(c, argc, argv);

  return 0;
}
