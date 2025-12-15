#define _DEFAULT_SOURCE
#include "../include/client.h"

static FILE *log_file = NULL;
static int socket_fd_global = -1;
static volatile int should_terminate = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

int convert(uint8_t *buf, ssize_t buf_size, char *str, ssize_t str_size) {
  if (buf == NULL || str == NULL || buf_size <= 0 ||
      str_size < (buf_size * 2 + 1)) {
    return -1;
  }

  for (int i = 0; i < buf_size; i++)
    sprintf(str + i * 2, "%02X", buf[i]);
  str[buf_size * 2] = '\0';

  return 0;
}

void generate_random_message(char *buffer, size_t max_size) {
  // generate random bytes using getentropy
  size_t random_bytes = (max_size - 10) / 2; // each bytes = 2 hex chars
  if (random_bytes > 256) {
    random_bytes = 256; // getentropy has a limit
  }

  uint8_t rand_buf[256];
  if (getentropy(rand_buf, random_bytes) != 0) {
    perror("getentropy");
    exit(EXIT_FAILURE);
  }

  // convert to hex string
  if (convert(rand_buf, random_bytes, buffer, max_size) != 0) {
    fprintf(stderr, "convert failed\n");
    exit(EXIT_FAILURE);
  }
}

int read_message_from_server(int socket_fd, uint8_t *type, uint32_t *sender_ip,
                             uint16_t *sender_port, char *data,
                             size_t max_size) {
  char buffer[MAX_MESSAGE_SIZE];
  size_t total_read = 0;

  while (total_read < MAX_MESSAGE_SIZE) {
    ssize_t bytes_read = recv(socket_fd, buffer + total_read, 1, 0);

    if (bytes_read <= 0)
      return -1;

    if (buffer[total_read] == '\n') {
      total_read++;
      break;
    }

    total_read++;
  }

  if (total_read < 1)
    return -1;

  *type = buffer[0];

  if (*type == TYPE_END)
    return 0;

  if (*type == TYPE_REGULAR && total_read >= 8) {
    size_t offset = 1;

    memcpy(sender_ip, buffer + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    memcpy(sender_port, buffer + offset, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    size_t data_len = total_read - offset;

    // exclude '\n' from data
    if (data_len > 0 && buffer[total_read - 1] == '\n')
      data_len--;

    if (data_len > 0) {
      memcpy(data, buffer + offset, data_len);
      data[data_len] = '\0';
      return data_len;
    }
  }

  return 0;
}

void *receive_messages(void *arg) {
  int socket_fd = *(int *)arg;

  while (!should_terminate) {
    uint8_t type;
    uint32_t sender_ip;
    uint16_t sender_port;
    char data[MAX_MESSAGE_SIZE];

    int result = read_message_from_server(socket_fd, &type, &sender_ip,
                                          &sender_port, data, MAX_MESSAGE_SIZE);

    if (result < 0)
      break;

    if (type == TYPE_END) {
      should_terminate = 1;
      break;
    }

    if (type == TYPE_REGULAR) {
      struct in_addr ip_addr;
      ip_addr.s_addr = sender_ip;
      char *ip_str = inet_ntoa(ip_addr);
      uint16_t port_host = ntohs(sender_port);

      pthread_mutex_lock(&log_mutex);
      if (log_file) {
        // use exact format
        fprintf(log_file, "%-15s%-10u%s\n", ip_str, port_host, data);
        fflush(log_file);
      }

      pthread_mutex_unlock(&log_mutex);
    }
  }

  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr, "Usage: %s <IP> <port> <# of messages> <log file>\n",
            argv[0]);
    return 1;
  }

  char *server_ip = argv[1];
  int server_port = atoi(argv[2]);
  int num_messages = atoi(argv[3]);
  char *log_file_path = argv[4];

  log_file = fopen(log_file_path, "w");
  if (!log_file) {
    perror("fopen");
    return 1;
  }

  socket_fd_global = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd_global < 0) {
    perror("socket");
    fclose(log_file);
    return 1;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);

  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    perror("int_pton");
    close(socket_fd_global);
    fclose(log_file);
    return 1;
  }

  if (connect(socket_fd_global, (struct sockaddr *)&server_addr,
              sizeof(server_addr)) < 0) {
    perror("connect");
    close(socket_fd_global);
    fclose(log_file);
    return 1;
  }

  pthread_t recv_thread;
  pthread_create(&recv_thread, NULL, receive_messages, &socket_fd_global);

  for (int i = 0; i < num_messages && !should_terminate; i++) {
    char message[MAX_MESSAGE_SIZE];
    generate_random_message(message, MAX_MESSAGE_SIZE - 10);

    char send_buffer[MAX_MESSAGE_SIZE];
    send_buffer[0] = TYPE_REGULAR;

    size_t msg_len = strlen(message);
    memcpy(send_buffer + 1, message, msg_len);
    send_buffer[1 + msg_len] = '\n';

    send(socket_fd_global, send_buffer, 2 + msg_len, 0);
  }

  if (!should_terminate) {
    char end_msg[2];
    end_msg[0] = TYPE_END;
    end_msg[1] = '\n';
    send(socket_fd_global, end_msg, 2, 0);
  }

  pthread_join(recv_thread, NULL);

  close(socket_fd_global);
  fclose(log_file);

  return 0;
}