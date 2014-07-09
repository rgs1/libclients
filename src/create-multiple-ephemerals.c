/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/*
 * Create N ephemerals
 */

#include <small/util.h>

#include <clients/clients.h>

typedef struct watcher_data watcher_data;
struct watcher_data {
  int created;
};

static void *new_watcher_data(void)
{
  watcher_data *wdata = (watcher_data *)safe_alloc(sizeof(watcher_data));
  return wdata;
}

static void reset_watcher_data(void *data)
{
  watcher_data *wdata = (watcher_data *)data;
  wdata->created = 0;
}

static void create_cb(int rc, const char *path, const void *data)
{
  if (!rc)
    info("Created %s", path);
  else
    warn("Create failed with rc=%d", rc);
}

static void my_watcher(zhandle_t *zzh, int type, int state, const char *path)
{
  session_context *context;
  watcher_data *wdata;
  int rc, num_ephemerals;

  if (type != ZOO_SESSION_EVENT)
    return;

  if (state != ZOO_CONNECTED_STATE)
    return;

  context = (session_context *)zoo_get_context(zzh);
  wdata = (watcher_data *)clients_context_data(context);
  num_ephemerals = clients_context_get_arg_int(context, "ephemeral-count");

  while (wdata->created <= num_ephemerals) {
    rc = zoo_acreate(zzh,
                     clients_context_get_arg(context, "path"),
                     "test",
                     4,
                     &ZOO_OPEN_ACL_UNSAFE,
                     ZOO_EPHEMERAL|ZOO_SEQUENCE,
                     create_cb,
                     NULL);

    if (!rc) {
      wdata->created++;
      info("Created %d ephemerals", wdata->created);
    } else {
      warn("Failed to create path");
      break;
    }
  }
}

int main(int argc, const char **argv)
{
  clients *c;

  c = clients_new(&my_watcher, &new_watcher_data, &reset_watcher_data);

  clients_add_arg(c, "path", 'P', "/yada", "parent path");
  clients_add_arg(c, "ephemeral-count", 'Q', "10", "how many ephemerals to create");

  clients_run(c, argc, argv);

  return 0;
}
