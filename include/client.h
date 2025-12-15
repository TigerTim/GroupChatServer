#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

void *receive_messages(void *arg);
void generate_random_message(char *buffer, size_t max_size);
int read_message_from_server(int socket_fd, uint8_t *type, uint32_t *sender_ip,
                             uint16_t *sender_port, char *data,
                             size_t max_size);

#endif