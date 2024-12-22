//
// Created by jay on 12/20/24.
//

#ifndef SERVER_H
#define SERVER_H

struct server;

typedef void (*message_handler)(struct server *srv, int len, const char *msg, void *user_data);

struct server *init_server(const char *sock_file, message_handler handler, void *user_data);
void server_run(struct server *srv);
void server_stop(struct server *srv);
void server_free(struct server *srv);

#endif //SERVER_H
