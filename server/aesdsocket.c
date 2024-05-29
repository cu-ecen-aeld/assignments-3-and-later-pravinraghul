#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <arpa/inet.h>
#include <errno.h>
// logging
#include <syslog.h>


// socket data file macro
#define PORT "9000"
#define FILEPATH "/var/tmp/aesdsocketdata"
#define BUFFERSIZE 1024

int sockfd;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
      syslog(LOG_INFO, "Caught signal, exiting");
      close(sockfd);
      remove(FILEPATH);
      syslog(LOG_INFO, "Closed server socket and deleted file %s", FILEPATH);
      exit(0);
    }
}

int main(int argc, char *argv[]) {
  int ret = 0;
  int is_daemon = 0;
  struct addrinfo hints, *result;
  struct sigaction sigact;

  // set the flag to run aesdsocket application in daemon
  if ((argc == 2) && (strcmp(argv[1], "-d") == 0)) {
    is_daemon = 1;
  }

  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_handler = signal_handler;
  sigfillset(&sigact.sa_mask);

  ret = sigaction(SIGINT, &sigact, NULL);
  if (ret == -1) {
    perror("cannot handle SIGINT");
    exit(1);
  }

  ret = sigaction(SIGTERM, &sigact, NULL);
  if (ret == -1) {
    perror("cannot handle SIGTERM");
    exit(1);
  }
 
  // logging begins
  openlog("aesdsocket", LOG_CONS | LOG_PID, LOG_USER);

  // using getaddrinfo api
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  ret = getaddrinfo(NULL, "9000", &hints, &result);
  if (ret != 0) {
    printf("getaddrinfo failed: %s\n", gai_strerror(ret));
    closelog();
    exit(EXIT_FAILURE);
  }

  // socket
  sockfd = socket(result->ai_family, result->ai_socktype, 0);
  if (sockfd == -1) {
    perror("socket failed");
    closelog();
    freeaddrinfo(result);
    exit(1);
  }

  // to reuse the same
  ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
  if (ret == -1) {
    perror("setsockopt failed");
    closelog();
    close(sockfd);
    freeaddrinfo(result);
    exit(1);
  }

  // binding
  ret = bind(sockfd, result->ai_addr, result->ai_addrlen);
  if (ret == -1) {
    perror("bind failed");
    closelog();
    close(sockfd);
    freeaddrinfo(result);
    exit(1);
  }
  freeaddrinfo(result);

  // creating a daemon after binding is ensured
  if (is_daemon) {
    pid_t pid = fork();
    if (pid == -1) {
      perror("Forking failed\n");
      closelog();
      close(sockfd);
      exit(1);
    } else if (pid > 0) { // parent process
      closelog();
      close(sockfd);
      exit(0);
    }
    printf("running in daemon mode: process id : %d\n", getpid());
  }

  // listening
  ret = listen(sockfd, 10);
  if (ret == -1) {
    perror("listen failed");
    closelog();
    close(sockfd);
    exit(1);
  }

  // connections accepting in infinite loop
  int client_sockfd;
  char buffer[BUFFERSIZE] = {0};
  struct sockaddr_in address;
  socklen_t addrlen = sizeof(addrlen);
  FILE *file;

  while (1) {
    client_sockfd = accept(sockfd, (struct sockaddr *)&address, &addrlen);
    if (client_sockfd == -1) {
      perror("accept failed");
      closelog();
      close(sockfd);
      exit(1);
    }

    // log new connections
    syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(address.sin_addr));

    // open the file
    file = fopen(FILEPATH, "a");
    if (file == NULL) {
      perror("fopen failed");
      closelog();
      close(client_sockfd);
      close(sockfd);
      exit(1);
    }

    // write to the file
    size_t bytes_recv;
    while ((bytes_recv = recv(client_sockfd, buffer, BUFFERSIZE, 0)) > 0) {
      fwrite(buffer, 1, bytes_recv, file);
      if (buffer[bytes_recv - 1] == '\n')
  	break;
    }

    // close the file
    fclose(file);

    file = fopen(FILEPATH, "r");
    if (file == NULL) {
      perror("fopen failed");
      closelog();
      close(client_sockfd);
      close(sockfd);
      exit(1);
    }

    while (fgets(buffer, BUFFERSIZE, file) != NULL) {
      send(client_sockfd, buffer, strlen(buffer), 0);
    }
    fclose(file);

    close(client_sockfd);
    syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(address.sin_addr));    
  }
  
  return 0;
}
