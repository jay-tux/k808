//
// Created by jay on 12/20/24.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "server.h"

struct server {
  message_handler handler;
  void *user;
  const char *sock_file;
  int fd;
  struct sockaddr_un addr;
  volatile int exiting;
};

struct server *init_server(const char *sock_file, message_handler handler, void *user_data) {
  struct server *res = malloc(sizeof(struct server));
  res->fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (res->fd == -1) {
    free(res);
    return NULL;
  }

  remove(sock_file);

  res->addr.sun_family = AF_UNIX;
  strncpy(res->addr.sun_path, sock_file, sizeof(res->addr.sun_path));

  int ret = bind(res->fd, (struct sockaddr *)&res->addr, sizeof(struct sockaddr));
  if (ret == -1) {
    close(res->fd);
    free(res);
    return NULL;
  }

  res->handler = handler;
  res->user = user_data;
  res->sock_file = strdup(sock_file);
  res->exiting = 0;
  return res;
}

void server_run(struct server *srv) {
  if (listen(srv->fd, 4096) == -1) return;

  while (!srv->exiting) {
    char buf[4096];
    const int accept_fd = accept(srv->fd, NULL, NULL);
    if (accept_fd == -1) return;

    size_t len = recv(accept_fd, buf, sizeof(buf), 0);
    while (len != 0) {
      srv->handler(srv, len, buf, srv->user);
      len = recv(accept_fd, buf, sizeof(buf), 0);
    }
  }
}

void server_stop(struct server *srv) {
  srv->exiting = 1;
}

void server_free(struct server *srv) {
  close(srv->fd);
  remove(srv->sock_file);
  free(srv);
}