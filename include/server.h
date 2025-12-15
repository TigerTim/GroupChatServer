#ifndef SERVER_H
#define SERVER_H

#include "common.h"

typedef struct {
  int socket_fd;
  struct sockaddr_in address;
  int active;
} client_info_t;

typedef struct {
  uint8_t type; // 0 or 1
  uint32_t sender_ip;
  uint16_t sender_port;
  char data[MAX_MESSAGE_SIZE];
  size_t data_len;
} message_t;

void *handle_client(void *arg);
void broadcast_message(message_t *msg);
int read_until_newline(int socket_fd, char *buffer, size_t max_size);

#endif