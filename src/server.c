#define _DEFAULT_SOURCE
#include "../include/server.h"

static client_info_t *clients = NULL;
static int num_clients = 0;
static int expected_clients = 0;
static int clients_done = 0;
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t broadcast_mutex = PTHREAD_MUTEX_INITIALIZER;

int read_until_newline(int socket_fd, char *buffer, size_t max_size) {
  size_t total_read = 0;

  while (total_read < max_size - 1) {
    ssize_t bytes_read = recv(socket_fd, buffer + total_read, 1, 0);

    if (bytes_read <= 0) {
      return -1;
    }

    // also count '\n'
    if (buffer[total_read] == '\n') {
      total_read++;
      break;
    }

    total_read++;
  }

  return total_read;
}

void broadcast_message(message_t *msg) {
  pthread_mutex_lock(&broadcast_mutex);

  char send_buffer[MAX_MESSAGE_SIZE];
  size_t offset = 0;

  send_buffer[offset++] = msg->type;

  if (msg->type == TYPE_REGULAR) {
    memcpy(send_buffer + offset, &msg->sender_ip, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(send_buffer + offset, &msg->sender_port, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    memcpy(send_buffer + offset, msg->data, msg->data_len);
    offset += msg->data_len;
  } else {
    send_buffer[offset++] = '\n'; // end of msg
  }

  pthread_mutex_lock(&clients_mutex);
  for (int i = 0; i < num_clients; i++) {
    if (clients[i].active) {
      send(clients[i].socket_fd, send_buffer, offset, 0);
    }
  }

  pthread_mutex_unlock(&clients_mutex);
  pthread_mutex_unlock(&broadcast_mutex);
}

void *handle_client(void *arg) {
  int client_index = *(int *)arg;
  free(arg);

  int client_fd = clients[client_index].socket_fd;
  struct sockaddr_in client_addr = clients[client_index].address;

  char buffer[MAX_MESSAGE_SIZE];

  while (1) {
    int bytes_read = read_until_newline(client_fd, buffer, MAX_MESSAGE_SIZE);

    if (bytes_read <= 0) {
      break;
    }

    uint8_t msg_type = buffer[0];

    if (msg_type == TYPE_END) {
      pthread_mutex_lock(&clients_mutex);
      clients_done++;

      if (clients_done == expected_clients) {
        message_t end_msg;
        end_msg.type = TYPE_END;
        pthread_mutex_unlock(&clients_mutex);

        broadcast_message(&end_msg);

        pthread_mutex_lock(&clients_mutex);
        clients[client_index].active = 0;
        pthread_mutex_unlock(&clients_mutex);

        close(client_fd);
        pthread_exit(NULL);
      }

      pthread_mutex_unlock(&clients_mutex);

    } else if (msg_type == TYPE_REGULAR) {
      message_t msg;
      msg.type = TYPE_REGULAR;
      msg.sender_ip = client_addr.sin_addr.s_addr;
      msg.sender_port = client_addr.sin_port;
      msg.data_len = bytes_read - 1; // exclude '\n'
      memcpy(msg.data, buffer + 1, msg.data_len);

      broadcast_message(&msg);
    }
  }

  pthread_mutex_lock(&clients_mutex);
  clients[client_index].active = 0;
  pthread_mutex_unlock(&clients_mutex);

  close(client_fd);
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <port> <# of clients>\n", argv[0]);
    return 1;
  }

  // input port and # of clients
  int port = atoi(argv[1]);
  expected_clients = atoi(argv[2]);

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    perror("socket");
    return 1;
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    close(server_fd);
    return 1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, expected_clients) < 0) {
    perror("listen");
    close(server_fd);
    return 1;
  }

  clients = malloc(expected_clients * sizeof(client_info_t));
  if (!clients) {
    perror("malloc");
    close(server_fd);
    return 1;
  }

  pthread_t *threads = malloc(expected_clients * sizeof(pthread_t));
  if (!threads) {
    perror("malloc");
    free(clients);
    close(server_fd);
    return 1;
  }

  for (int i = 0; i < expected_clients; i++) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      perror("accept");
      continue;
    }

    clients[i].socket_fd = client_fd;
    clients[i].address = client_addr;
    clients[i].active = 1;

    int *index = malloc(sizeof(int));
    *index = i;

    pthread_create(&threads[i], NULL, handle_client, index);
    pthread_detach(threads[i]);

    pthread_mutex_lock(&clients_mutex);
    num_clients++;
    pthread_mutex_unlock(&clients_mutex);
  }

  while (1) {
    pthread_mutex_lock(&clients_mutex);
    int done = (clients_done == expected_clients);
    pthread_mutex_unlock(&clients_mutex);

    if (done)
      break;

    usleep(100000);
  }

  close(server_fd);
  free(clients);
  free(threads);

  return 0;
}