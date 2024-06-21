#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
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
#include <syslog.h>
#include <pthread.h>
#include <time.h>
#include "queue.h"

// socket data file macro
#define PORT "9000"
#define BUFFERSIZE 1024

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE
#endif

#ifndef USE_AESD_CHAR_DEVICE
#define FILEPATH "/var/tmp/aesdsocketdata"
#else
#define FILEPATH "/dev/aesdchar"
#endif

int sockfd;
pthread_mutex_t mutex;

struct thread_param_t {
  int client_sockfd;
  struct sockaddr_in client_addr;
  socklen_t client_addrlen;
  bool is_complete;
};

typedef struct slist_data_s slist_data_t;
struct slist_data_s {
  pthread_t thread_id;
  struct thread_param_t *thread_param;
  SLIST_ENTRY(slist_data_s) entries;
};

// singly linked list initialization


// Signal handler
void signal_handler(int signal) {
  if (signal == SIGINT || signal == SIGTERM) {
    syslog(LOG_INFO, "Caught signal, exiting");
    close(sockfd);
#ifndef USE_AESD_CHAR_DEVICE
    remove(FILEPATH);
#endif
    syslog(LOG_INFO, "Closed server socket and deleted file %s", FILEPATH);
    exit(0);
  }
}

// Thread function for every separate connection
void thread_func(void *thread_param) {
  struct thread_param_t *tparam = (struct thread_param_t *) thread_param;
  char buffer[BUFFERSIZE] = {0};
  size_t bytes_recv;
  FILE *file;

  syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(tparam->client_addr.sin_addr));

  // mutex lock
  pthread_mutex_lock(&mutex);

  // write the data to the file
  file = fopen(FILEPATH, "a");
  if (file == NULL) {
    perror("fopen failed");
    closelog();
    close(tparam->client_sockfd);
    close(sockfd);
    pthread_mutex_unlock(&mutex);
    exit(-1);
  }

  while ((bytes_recv = recv(tparam->client_sockfd, buffer, BUFFERSIZE, 0)) > 0) {
    fwrite(buffer, 1, bytes_recv, file);
    if (buffer[bytes_recv - 1] == '\n')
      break;
  }
  fclose(file);

  // read the data from the file
  file = fopen(FILEPATH, "r");
  if (file == NULL) {
    perror("fopen failed");
    closelog();
    close(tparam->client_sockfd);
    close(sockfd);
    return;
  }

  while (fgets(buffer, BUFFERSIZE, file) != NULL) {
    send(tparam->client_sockfd, buffer, strlen(buffer), 0);
  }
  fclose(file);

  // mutex unlock
  pthread_mutex_unlock(&mutex);

  close(tparam->client_sockfd);
  tparam->is_complete = true;
  syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(tparam->client_addr.sin_addr));
}

#ifndef USE_AESD_CHAR_DEVICE

static inline void timespec_add(struct timespec *result,
				const struct timespec *ts_1,
				const struct timespec *ts_2) {
  result->tv_sec = ts_1->tv_sec + ts_2->tv_sec;
  result->tv_nsec = ts_1->tv_nsec + ts_2->tv_nsec;
  if( result->tv_nsec > 1000000000L ) {
    result->tv_nsec -= 1000000000L;
    result->tv_sec++;
  }
}

void *timer_handler(union sigval sigvalue) {
  FILE *file;
  time_t ctime;

  pthread_mutex_lock(&mutex);
  // write the data to the file
  file = fopen(FILEPATH, "a");
  if (file == NULL) {
    perror("fopen failed");
    return NULL;
  }

  time(&ctime);
  struct tm *time_info = localtime(&ctime);
  char timestamp[50];
  strftime(timestamp, sizeof(timestamp), "timestamp:%Y-%m-%d %H:%M:%S\n", time_info);
  fprintf(file, "%s", timestamp);
  fclose(file);

  // mutex unlock
  pthread_mutex_unlock(&mutex);
  return NULL;
}

void setup_timer(void) {
  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;

  int clockid = CLOCK_MONOTONIC;
  memset(&sev, 0, sizeof(struct sigevent));
  memset(&its, 0, sizeof(struct itimerspec));

  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = (void*)timer_handler;
  if (timer_create(clockid, &sev, &timerid) != 0) {
    perror("timer create failed");
    exit(1);
  }
  struct timespec start_time;
  if (clock_gettime(clockid, &start_time) != 0) {
    perror("clock_gettime failed");
    exit(1);
  }
  its.it_interval.tv_sec = 10;
  timespec_add(&its.it_value, &start_time, &its.it_interval);
  if (timer_settime(timerid, TIMER_ABSTIME, &its, NULL) != 0) {
    perror("timer settime failed");
    exit(1);
  }
}

#endif

int main(int argc, char *argv[]) {
  int ret = 0;
  int is_daemon = 0;
  struct addrinfo hints, *result;
  struct sigaction sigact;

  // mutex init
  pthread_mutex_init(&mutex, NULL);

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

#ifndef USE_AESD_CHAR_DEVICE
  setup_timer();
#endif

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
  struct sockaddr_in addr;
  socklen_t addrlen = sizeof(addrlen);

  SLIST_HEAD(slisthead, slist_data_s) head;
  SLIST_INIT(&head);

  while (1) {
    client_sockfd = accept(sockfd, (struct sockaddr *)&addr, &addrlen);
    if (client_sockfd == -1) {
      perror("accept failed");
      closelog();
      close(sockfd);
      exit(1);
    }

    // create the thread parameters
    struct thread_param_t *thread_param = (struct thread_param_t *) malloc(sizeof(struct thread_param_t));
    if (thread_param == NULL) {
      perror("thread_param malloc failed");
      continue;
    }

    thread_param->client_sockfd = client_sockfd;
    thread_param->client_addr = addr;
    thread_param->client_addrlen = addrlen;
    thread_param->is_complete = false;

    slist_data_t *thread_ptr = NULL;
    slist_data_t *thread_ptr_temp = NULL;
    thread_ptr = malloc(sizeof(slist_data_t));
    if (thread_ptr == NULL) {
      perror("thread_ptr malloc failed");
      free(thread_param);
      continue;
    }

    pthread_t new_thread;
    ret = pthread_create(&new_thread, NULL, (void *)(thread_func), thread_param);
    if (ret != 0) {
      perror("pthread create failed");
      free(thread_param);
      free(thread_ptr);
      continue; // to skip the next lines
    }

    thread_ptr->thread_id = new_thread;
    thread_ptr->thread_param = thread_param;
    SLIST_INSERT_HEAD(&head, thread_ptr, entries);

    SLIST_FOREACH_SAFE(thread_ptr, &head, entries, thread_ptr_temp) {
      if (thread_ptr->thread_param->is_complete) {
	pthread_join(thread_ptr->thread_id, NULL);
	SLIST_REMOVE(&head, thread_ptr, slist_data_s, entries);
	free(thread_ptr->thread_param);
	free(thread_ptr);
      }
    }
    free(thread_ptr_temp);
  }

  return 0;
}
