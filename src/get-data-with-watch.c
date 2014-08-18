/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/*
 * calls getData(path, watch: true) repeatedly
 */

#include <small/util.h>

#include <clients/clients.h>


typedef struct {
  char following;
} watcher_data;


static void *new_watcher_data(void)
{
  watcher_data *wdata = (watcher_data *)safe_alloc(sizeof(watcher_data));
  return wdata;
}

static void reset_watcher_data(void *data)
{
  watcher_data *wdata = (watcher_data *)data;
  wdata->following = 0;
}

static int is_connected(zhandle_t *zh)
{
  return zoo_state(zh) == ZOO_CONNECTED_STATE;
}


static void get_cb(int rc,
                   const char *val,
                   int vlen,
                   const struct Stat *stat,
                   const void *data)
{
  if (!rc)
    info("Got data");
  else
    info("Failed to get data");
}

/* session expiration is handled for us (and a new session created) */
static void watcher(zhandle_t *zzh, int type, int state, const char *ppath)
{
  char *path;
  session_context *context = (session_context *)zoo_get_context(zzh);
  int rc;

  path = (char *)clients_context_get_arg(context, "path");

  if (type != ZOO_SESSION_EVENT) {
    info("%d %d %s", type, state, path);
    rc = zoo_aget(zzh, path, 1, get_cb, NULL);
    if (rc)
      warn("Failed to get path");
    return;
  }

  if (is_connected(zzh)) {
    watcher_data *data = (watcher_data *)clients_context_data(context);

    if (!data->following) {
      rc = zoo_aget(zzh, path, 1, get_cb, NULL);
      if (rc)
        warn("Failed to get path");
      else
        data->following = 1;
    }
  }
}

int main(int argc, const char **argv)
{
  clients *c;

  c = clients_new(&watcher, &new_watcher_data, &reset_watcher_data);
  clients_add_arg(c, "path", 'P', "/yada", "parent path");
  clients_run(c, argc, argv);

  return 0;
}
