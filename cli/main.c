//
// Created by jay on 12/22/24.
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"

#ifndef K808_SERVER
#error "K808_SERVER is not defined. Expected a file path."
#endif

void help() {
  // TODO
  printf(" --- TODO! --- \n");
}

void command(const struct client *client, const char buf[1024]) {
  char buffer[1032] = { 'K', '8', '0', '8' };
  const size_t l = strlen(buf);
  *(uint32_t *)(buffer + 4) = (uint32_t)l;
  strncpy(buffer + 8, buf, l);
  buffer[l] = '\0';
  client_send(client, buffer, l);

  char *resp = NULL;
  size_t rd = client_read_sync(client, &resp);
  printf("(%lu bytes received)\n", rd);
  if (rd == 0) {
    printf("No or empty response.\n");
  }
  else {
    printf("Server responded: '%s'\n", resp);
  }
}

int main() {
  printf(" --- K808 CLI --- \n");
  printf("Connecting to daemon...\n");
  const struct client *client = client_init(K808_SERVER);
  if (client == NULL) {
    printf("Connection failed. Check if daemon is running.\n");
    return EXIT_FAILURE;
  }

  printf("Connected to daemon.\n");
  printf("Type '.h' for help, '.q' to quit.\n");
  while (1) {
    char buffer[1024];
    printf(">>> ");
    fflush(stdout);
    int rd = scanf("%1023s", buffer);
    if (rd == EOF) break;

    if (strcmp(buffer, ".q") == 0) break;

    if (strcmp(buffer, ".h") == 0) {
      help();
    }
    else {
      command(client, buffer);
    }
  }
}