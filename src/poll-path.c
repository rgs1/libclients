/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/***
  Polls a path (getchildren + getdata on each child) with pauses of <pause> secs

  This is useful to measure read latency and overall read throughput.
***/

#include <string.h>
#include <unistd.h>

#include <small/latch.h>
#include <small/util.h>
#include <small/queue.h>
#include <small/timer.h>
#include <small/worker.h>

#include <clients/clients.h>


typedef struct watcher_data watcher_data;

struct watcher_data {
  latch *l;
  worker *w;
};

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


static void get_cb(int rc,
                   const char *val,
                   int vlen,
                   const struct Stat *stat,
                   const void *data)
{
  latch *l = (latch *)data;
  if (rc != ZOK)
    info("Failed to get data: %s", zerror(rc));
  latch_down(l);
}

static void children_cb(
  int rc,
  const struct String_vector *children,
  const void *data)
{
  const char *path;
  int i;
  session_context *context;
  watcher_data *wdata;
  zhandle_t *zh;

  context = (session_context *)data;
  wdata = (watcher_data *)clients_context_data(context);

  if (!children || rc != ZOK) {
    warn("Got no children");
    goto out;
  }

  path = clients_context_get_arg(context, "path");
  info("%s has %d children", path, children->count);
  latch_add(wdata->l, children->count);

  zh = clients_context_handle(context); /* lock already held by caller */
  for (i=0; i < children->count; i++) {
    char cpath[2048];

    if (strlen(path) == 1 && path[0] == '/')
      snprintf(cpath, sizeof(cpath), "/%s", children->data[i]);
    else
      snprintf(cpath, sizeof(cpath), "%s/%s", path, children->data[i]);

    rc = zoo_aget(zh, cpath, 0, get_cb, wdata->l);
    if (rc != ZOK) {
      info("Failed to issue get for %s", cpath);
      latch_down(wdata->l);
    }
  }

out:
  /* done issuing gets, so dec latch */
  latch_down(wdata->l);
}

static void *loop(void *data)
{
  const char *path;
  int stime, rc;
  worker *w = (worker *)data;
  session_context *context = (session_context *)worker_get_data(w);
  timer *t = timer_new();
  zhandle_t *zh;
  watcher_data *wdata = (watcher_data *)clients_context_data(context);

  path = clients_context_get_arg(context, "path");
  stime = clients_context_get_arg_int(context, "sleep-inbetween");

  while (1) {
    wdata->l = latch_new(1); /* wait for get_children */

    timer_start(t);

    zh = clients_context_take_handle(context); /* exclusive access to zhandle */
    rc = zoo_aget_children(zh, path, 0, children_cb, context);
    clients_context_put_handle(context);

    if (rc == ZOK) {
      char tbuf[64];

      info("Waiting for reqs to finish");
      latch_wait(wdata->l);
      timer_stop(t);
      info("Done fetching data in %s secs", timer_diff(t, tbuf, sizeof(tbuf)));
      fflush(stdout);
    } else
      info("Failed to get children, sleeping for a while...");

    latch_destroy(wdata->l);

    sleep(stime);
  }

  return NULL;
}

/* session expiration is handled for us (and a new session created) */
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
  clients_add_arg(c, "sleep-inbetween", 'D', "1", "secs to sleep inbetween");
  clients_run(c, argc, argv);

  return 0;
}
