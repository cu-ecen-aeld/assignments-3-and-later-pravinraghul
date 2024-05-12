#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>

#include <syslog.h>

int main(int argc, char *argv[]) {

  openlog ("writer", LOG_CONS | LOG_PID, LOG_USER);
 
  // Invalid use of arguments
  if (argc != 3) {
    printf("writer <path> <string>\n");
    syslog(LOG_ERR, "writer <path> <string>\n");
    exit(1);
  }

  int filestr_size = strlen(argv[1]);
  char *file = (char *)malloc(filestr_size+1);
  if (!file) {
    syslog(LOG_ERR, "Malloc failed\n");
    exit(1);
  }

  memcpy(file, argv[1], filestr_size);
  file[filestr_size] = '\0';

  char *path = dirname(argv[1]);
  mkdir(path, 0766);

  // open's the existing file or creates the new file.
  int fd = open(file,
		O_CREAT | O_WRONLY | O_TRUNC,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
  if (fd == -1) {
    syslog(LOG_ERR, "Unable to open the file: %s : %s\n", file, strerror(errno));
    exit(1);
  }

  // write the string to the file
  int ret = write(fd, argv[2], strlen(argv[2]));
  if (ret == -1) {
    syslog(LOG_ERR, "Unable to write to the file: %s\n", file);
    close(fd);
    exit(1);
  }

  //sprintf("Writing %s to %s\n");
  syslog(LOG_DEBUG, "Writing %s to %s\n", argv[2], file);

  close(fd);
  free(file);
  return 0;
}
