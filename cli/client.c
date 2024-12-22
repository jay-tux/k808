//
// Created by jay on 12/22/24.
//

#include "client.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

struct client {
  char *path;
  int fd;
};

struct client *client_init(const char *path) {
  struct client *res = malloc(sizeof(struct client));
  res->path = strdup(path);
  res->fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (res->fd == -1) {
    free(res->path);
    free(res);
    return NULL;
  }

  struct sockaddr_un sun = {0};
  sun.sun_family = AF_UNIX;
  strncpy(sun.sun_path, path, sizeof(sun.sun_path) - 1);

  const int ret = connect(res->fd, (struct sockaddr *)&sun, sizeof(struct sockaddr_un));
  if (ret == -1) {
    close(res->fd);
    free(res->path);
    free(res);
    return NULL;
  }

  return res;
}

void client_send(const struct client *client, const char *msg, const size_t len) {
  write(client->fd, msg, len);
}

size_t client_read_sync(const struct client *client, char **buf) {
  char buffer[1024];
  size_t rd = read(client->fd, buffer, sizeof(buffer));
  printf("(read %lu bytes)\n", rd);

  if (rd == -1) {
    *buf = NULL;
    return 0;
  }

  *buf = malloc(rd);
  memcpy(*buf, buffer, rd);
  size_t curr_len = rd;
  while (rd == 1024) {
    rd = read(client->fd, buffer, sizeof(buffer));
    char *cp = realloc(*buf, curr_len + rd);
    if (cp != NULL) *buf = cp;
    memcpy(*buf + curr_len, buffer, rd);
    curr_len += rd;
    printf("(read %lu bytes; buffer %lu bytes)\n", rd, curr_len);
  }

  return curr_len;
}

void client_free(struct client *client) {
  close(client->fd);
  free(client->path);
  free(client);
}