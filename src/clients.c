/*-*- Mode: C; c-basic-offset: 2; indent-tabs-mode: nil -*-*/

/*
 * TODO:
 *      graceful connect retries
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <fcntl.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <zookeeper.h>

#include <small/argparser.h>
#include <small/queue.h>
#include <small/util.h>

#include <clients/clients.h>


#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

typedef struct connection connection;

struct clients {
  argparser * argparser;
  void (*watcher)(zhandle_t *, int, int, const char *);
  void *(*new_watcher_data)(void);
  void (*reset_watcher_data)(void *);
  int epfd;
  connection *conns; /* state & meta-state for all zk clients */
};

struct connection {
  int events;
  int queued;
  pthread_mutex_t lock;
  zhandle_t *zh;
  const char *server;
  int session_timeout;
};

struct session_context {
  clients *c;
  connection *conn;
  void *data;
};


static void clients_start_child_proc(clients *clients, int child_num);
static void *clients_create_zk_clients(void *clients);
static void *clients_check_interests(void *clients);
static void do_check_interests(clients *c, connection *zkc);
static void *clients_poll(void *queue);
static void *clients_process_worker(void *data);
static void create_zk_client(clients *c, connection *conn, void *context);
static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *context);


#define CLIENTS_EXPORT __attribute__ ((visibility("default")))


CLIENTS_EXPORT clients * clients_new(
  void (*my_watcher)(zhandle_t *, int, int, const char *),
  void *(*new_watcher_data)(void),
  void (*reset_watcher_data)(void *))
{
  argparser *ap;
  clients *c = safe_alloc(sizeof(clients));

  ap = c->argparser = argparser_new(20);

  argparser_add(ap, "debug", 'G', "false", "Turn on debugging for logs");
  argparser_add(ap, "max-events", 'e', "100", "Set the max number of events");
  argparser_add(ap, "num-clients", 'c', "500", "Set the number of clients");
  argparser_add(ap, "num-procs", 'p', "20", "Set the number of processes");
  argparser_add(ap, "num-workers", 'W', "1", "# of workers to call zookeeper_process()");
  argparser_add(ap, "wait-time", 'w', "50", "Set the wait time for epoll_wait()");
  argparser_add(ap, "session-timeout", 's', "10000", "Session timeout for ZK clients");
  argparser_add(ap, "sleep-after-clients", 'N', "0", "Sleep after starting N clients");
  argparser_add(ap, "sleep-in-between", 'n', "5", "Sleep secs inbetween N started clnts");

  c->watcher = my_watcher;
  c->new_watcher_data = new_watcher_data;
  c->reset_watcher_data = reset_watcher_data;

  return c;
}

CLIENTS_EXPORT void clients_run(clients *c, int argc, const char **argv)
{
  argparser *ap;
  int i = 0;
  pid_t pid = 0;

  ap = c->argparser;

  argparser_parse(ap, argc, argv);

  if (!argparser_get_argc(ap))
    error(EXIT_BAD_PARAMS, "Server name not given.");

  if (!strcmp(argparser_get_str(ap, "debug"), "true"))
    zoo_set_debug_level(ZOO_LOG_LEVEL_DEBUG);

  prctl(PR_SET_NAME, "parent", 0, 0, 0);

  for (i=0; i < argparser_get_int(ap, "num-procs"); i++) {
    pid = fork();
    if (pid == -1)
      error(EXIT_SYSTEM_CALL, "Ugh, couldn't fork");

    if (!pid) {
      clients_start_child_proc(c, i);
    }
  }

  if (pid) { /* parent */
    /* TODO: wait() on children */
    while (1)
      sleep(100);
  }
}


CLIENTS_EXPORT
void *clients_context_data(session_context *context)
{
  return context->data;
}

CLIENTS_EXPORT
zhandle_t *clients_context_zoo_handle(session_context *context)
{
  return context->conn->zh;
}

CLIENTS_EXPORT void clients_add_arg(
  clients *c,
  const char *argname,
  char chr,
  const char *argvalue,
  const char *desc)
{
  argparser_add(c->argparser, argname, chr, argvalue, desc);
}

CLIENTS_EXPORT
const char *clients_context_get_arg(session_context *context, const char *arg)
{
  return (const char *)argparser_get_str(context->c->argparser, arg);
}

CLIENTS_EXPORT
int clients_context_get_arg_int(session_context *context, const char *arg)
{
  return argparser_get_int(context->c->argparser, arg);
}

static void clients_start_child_proc(clients *c, int child_num)
{
  char tname[20];
  int saved, j, num_workers, num_clients;
  pthread_t tid_interests, tid_poller, tid_create_clients, *tids_workers;
  queue *q;

  num_workers = argparser_get_int(c->argparser, "num-workers");
  num_clients = argparser_get_int(c->argparser, "num-clients");

  tids_workers = (pthread_t *)safe_alloc(sizeof(pthread_t) * num_workers);

  snprintf(tname, 20, "child[%d]", child_num);
  prctl(PR_SET_NAME, tname, 0, 0, 0);

  q = queue_new(num_clients);

  /* for threads needing clients */
  queue_set_user_data(q, (void *)c);

  c->conns = (connection *)safe_alloc(sizeof(connection) * num_clients);

  c->epfd = epoll_create(1);
  if (c->epfd == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL,
          "Failed to create an epoll instance: %s",
          strerror(saved));
  }

  /* prepare locks */
  for (j=0; j < num_clients; j++) {
    if (pthread_mutex_init(&c->conns[j].lock, 0)) {
      error(EXIT_SYSTEM_CALL, "Failed to init mutex");
    }
  }

  /* start threads */
  pthread_create(&tid_create_clients, NULL, &clients_create_zk_clients, c);
  set_thread_name(tid_create_clients, "creator");

  pthread_create(&tid_interests, NULL, &clients_check_interests, c);
  set_thread_name(tid_interests, "interests");

  pthread_create(&tid_poller, NULL, &clients_poll, q);
  set_thread_name(tid_poller, "poller");

  for (j=0; j < num_workers; j++) {
    char thread_name[128];

    snprintf(thread_name, 128, "work[%d]", j);
    pthread_create(&tids_workers[j], NULL, &clients_process_worker, q);
    set_thread_name(tids_workers[j], thread_name);
  }

  /* TODO: monitor each thread's health */
  while (1)
    sleep(100);
}

static void *clients_process_worker(void *data)
{
  connection *zkc;
  queue *q = (queue *)data;

  while (1) {
    zkc = (connection *)queue_remove(q);

    /* Note:
     *
     * watchers are called from here, so no need for locking from there
     */
    pthread_mutex_lock(&zkc->lock);
    zkc->queued = 0;
    zookeeper_process(zkc->zh, zkc->events);
    pthread_mutex_unlock(&zkc->lock);
  }

  return NULL;
}

static void *clients_check_interests(void *data)
{
  clients *c = (clients *)data;
  int num_clients, j;
  struct timespec req = { 0, 10 * 1000 * 1000 } ; /* 10ms */

  num_clients = argparser_get_int(c->argparser, "num-clients");

  while (1) {
    /* Lets see what new interests we've got (i.e.: new Pings, etc) */
    for (j=0; j < num_clients; j++) {
      do_check_interests(c, &c->conns[j]);
    }

    nanosleep(&req, NULL);
  }

  return NULL;
}

static void do_check_interests(clients *c, connection *zkc)
{
  int fd, rc, interest, saved, client_ready;
  struct epoll_event ev;
  struct timeval tv;

  fd = -1;
  client_ready = 1;

  /* TODO: if queued to be processed, should we skip? */
  pthread_mutex_lock(&zkc->lock);
  if (zkc->zh) {
    rc = zookeeper_interest(zkc->zh, &fd, &interest, &tv);
  } else {
    client_ready = 0;
  }
  pthread_mutex_unlock(&zkc->lock);

  if (!client_ready)
    return;

  if (rc || fd == -1) {
    if (fd != -1 && (rc == ZINVALIDSTATE || rc == ZCONNECTIONLOSS))
      /* Note that ev must be !NULL for kernels < 2.6.9 */
      epoll_ctl(c->epfd, EPOLL_CTL_DEL, fd, &ev);
    return;
  }

  ev.data.ptr = zkc;
  ev.events = 0;
  if (interest & ZOOKEEPER_READ)
    ev.events |= EPOLLIN;

  if (interest & ZOOKEEPER_WRITE)
    ev.events |= EPOLLOUT;

  if (epoll_ctl(c->epfd, EPOLL_CTL_MOD, fd, &ev) == -1) {
    saved = errno;
    if (saved != ENOENT)
      error(EXIT_SYSTEM_CALL,
            "epoll_ctl_mod failed with: %s ",
            strerror(saved));

    /* New FD, lets add it */
    if (epoll_ctl(c->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
      saved = errno;
      error(EXIT_SYSTEM_CALL,
            "epoll_ctl_add failed with: %s",
            strerror(saved));
    }
  }
}

static void * clients_create_zk_clients(void *data)
{
  clients *c = (clients *)data;
  const char *server;
  int j, after, inbetween, num_clients, session_timeout;

  after = argparser_get_int(c->argparser, "sleep-after-clients");
  inbetween = argparser_get_int(c->argparser, "sleep-in-between");
  num_clients  = argparser_get_int(c->argparser, "num-clients");
  session_timeout = argparser_get_int(c->argparser, "session-timeout");
  server = argparser_get_argv(c->argparser, 0);

  for (j=0; j < num_clients; j++) {
    session_context *context = safe_alloc(sizeof(session_context));

    context->c = c;
    context->data = c->new_watcher_data();

    pthread_mutex_lock(&c->conns[j].lock);
    c->conns[j].server = server;
    c->conns[j].session_timeout = session_timeout;
    context->conn = &c->conns[j];
    create_zk_client(c, &c->conns[j], context);
    pthread_mutex_unlock(&c->conns[j].lock);

    if (after > 0 && j > 0 && j % after == 0) {
      info("Sleeping for %d secs after having created %d clients",
           inbetween,
           j);
      sleep(inbetween);
    }
  }

  info("Done creating clients...");

  return NULL;
}

static void *clients_poll(void *data)
{
  int ready, j, saved;
  int events;
  struct epoll_event *evlist;
  queue *q = (queue *)data;
  connection *conn;
  clients *c = (clients *)queue_get_user_data(q);
  int max_events, wait_time;

  max_events = argparser_get_int(c->argparser, "max-events");
  wait_time = argparser_get_int(c->argparser, "wait-time");

  evlist = (struct epoll_event *)safe_alloc(
      sizeof(struct epoll_event) * max_events);

  while (1) {
    ready = epoll_wait(c->epfd, evlist, max_events, wait_time);
    if (ready == -1) {
      if (errno == EINTR)
        continue;

      saved = errno;
      error(EXIT_SYSTEM_CALL, "epoll_wait failed with: %s", strerror(saved));
    }

    /* Go over file descriptors that are ready */
    for (j=0; j < ready; j++) {
      events = 0;
      if (evlist[j].events & (EPOLLIN|EPOLLOUT)) {
        if (evlist[j].events & EPOLLIN)
          events |= ZOOKEEPER_READ;
        if (evlist[j].events & EPOLLOUT)
          events |= ZOOKEEPER_WRITE;

        conn = (connection *)evlist[j].data.ptr;

        pthread_mutex_lock(&conn->lock);

        if (!conn->queued) {
          conn->events = events;
          conn->queued = 1;
          queue_add(q, (void *)conn);
        }

        pthread_mutex_unlock(&conn->lock);

      } else if (evlist[j].events & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) {
        /* Invalid FDs will be removed when zookeeper_interest() indicates
         * they are not valid anymore */
      } else {
        warn("Unknown events: %d\n", evlist[j].events);
      }
    }
  }

  return NULL;
}

static void create_zk_client(clients *c, connection *conn, void *context)
{
  int fd, rc, interest, saved;
  int flags = 0; /* ZOO_READONLY */
  struct epoll_event ev;
  struct timeval tv;
  zhandle_t *zh;

  /* try until we succeed */
  while (1) {
    zh = zookeeper_init(conn->server,
                        watcher,
                        conn->session_timeout,
                        0,
                        context,
                        flags);
    fd = -1;
    rc = zookeeper_interest(zh, &fd, &interest, &tv);
    if (rc == ZOK)
      break;

    if (rc == ZCONNECTIONLOSS) {
      /* busy server perhaps? lets try again */
      zookeeper_close(zh);
      continue;
    }

    /* TODO: this error func is a bad idea */
    error(3, "zookeeper_interest failed with rc=%d\n", rc);
  }

  ev.events = 0;
  if (interest & ZOOKEEPER_READ)
    ev.events |= EPOLLIN;
  if (interest & ZOOKEEPER_WRITE)
    ev.events |= EPOLLOUT;
  ev.data.ptr = conn;

  if (epoll_ctl(c->epfd, EPOLL_CTL_ADD, fd, &ev) == -1) {
    saved = errno;
    error(EXIT_SYSTEM_CALL, "epoll_ctl_add failed with: %s", strerror(saved));
  }

  conn->zh = zh;
}

/* no locks are taken here, those happen from wherever zookeeper_process
 * is called. */
static void watcher(zhandle_t *zzh, int type, int state, const char *path, void *ctxt)
{
  session_context *context = (session_context *)zoo_get_context(zzh);
  clients *c = context->c;

  if (state == ZOO_EXPIRED_SESSION_STATE) {
    /* Cleanup the expired session */
    zookeeper_close(zzh);

    /* create a new session */
    c->reset_watcher_data(context->data);
    create_zk_client(c, context->conn, context);
  } else {
    /* dispatch the event to the "real" watcher */
    c->watcher(zzh, type, state, path);
  }
}
