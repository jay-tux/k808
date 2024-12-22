//
// Created by jay on 12/22/24.
//

#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>

struct client;

struct client *client_init(const char *path);
void client_send(const struct client *client, const char *msg, size_t len);
size_t client_read_sync(const struct client *client, char **buf);
void client_free(struct client *client);

#endif //CLIENT_H
