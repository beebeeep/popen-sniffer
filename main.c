#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/errno.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#define STDERR(...) (fprintf(stderr, __VA_ARGS__))

typedef  struct {
    int in;
    int out;
    sem_t *sem;
    FILE* log;
} copy_args;

void *copy_data(void *a) {
  copy_args *args = (copy_args *)a;
  int bufsize = 128;
  char *buf = malloc(bufsize);
  printf("[%d] starting\n", args->in);
  for(;;) {
    ssize_t n = read(args->in, buf, bufsize);
    if (n <= 0) {
      STDERR("[%d] got %ld: %s\n", args->in, n, strerror(errno));
      break;
    }
    STDERR("read %ld bytes from %d\n", n, args->in);
    if (write(args->out, buf, n) <= 0) {
      break;
    }
    fwrite(buf, 1, n, args->log);
  }

  printf("done %d\n", args->in);
  sem_post(args->sem);
  return NULL;
}

void runCmd(int pipes[3][2], char *cmd, char *argv[]) {
  if (pipe(pipes[0]) < 0) { // stdout
    STDERR("creating pipe: %s\n", strerror(errno));
  }
  if (pipe(pipes[1]) < 0) { // stdin
    STDERR("creating pipe: %s\n", strerror(errno));
  }
  if (pipe(pipes[2]) < 0) { // stderr
    STDERR("creating pipe: %s\n", strerror(errno));
  }

  if (fork() == 0 ) {
      dup2(pipes[0][0], STDIN_FILENO);  // attach input pipe to stdin 
      dup2(pipes[1][1], STDOUT_FILENO); // attach output pipe to stdout
      dup2(pipes[2][1], STDERR_FILENO); // attach output pipe to stderr

      close(pipes[0][1]);       // close write end of stdin
      close(pipes[1][0]);       // close read end of stdout
      close(pipes[2][0]);       // close read end of stderr

      if (execvp(cmd, argv) != 0) {
        STDERR("failed to start command: %s\n", strerror(errno));
      }
  }

  close(pipes[0][0]);
  close(pipes[1][1]);
  close(pipes[2][1]);
}

int main(int argc, char *argv[]) {
  char *stdin_logfile = malloc(256);
  char *stdout_logfile = malloc(256);
  char *stderr_logfile = malloc(256);
  int ch;

  strncpy(stdin_logfile, "stdin.txt", 256);
  strncpy(stdout_logfile, "stdout.txt", 256);
  strncpy(stderr_logfile, "stderr.txt", 256);
  while ((ch = getopt(argc, argv, "i:o:e:")) != -1) {
    switch (ch) {
    case 'i':
      stdin_logfile = (char *)realloc(stdin_logfile, strlen(optarg) + 1);
      strncpy(stdin_logfile, optarg, strlen(optarg));
      break;
    case 'o':
      stdout_logfile = (char *)realloc(stdout_logfile, strlen(optarg) + 1);
      strncpy(stdout_logfile, optarg, strlen(optarg));
      break;
    case 'e':
      stderr_logfile = (char *)realloc(stdin_logfile, strlen(optarg) + 1);
      strncpy(stderr_logfile, optarg, strlen(optarg));
      break;
    }
  }
  
  // the rest of arguments are  command we are running and its args
  if (argc-optind < 1) {
    STDERR("specify command\n");
    return -1;
  }
  char **cmd_args = malloc(argc-optind+2);
  for (int i = 0; i < argc-optind; i++) {
    cmd_args[i] = argv[optind+i];
  }
  cmd_args[argc-optind+1] = NULL;

  FILE *stdin_log = fopen(stdin_logfile, "a");
  if (stdin_log == NULL) {
    STDERR("Failed to open stdin log file: %s\n", strerror(errno));
    return -1;
  }
  FILE *stdout_log = fopen(stdout_logfile, "a");
  if (stdin_log == NULL) {
    STDERR("Failed to open stdout log file: %s\n", strerror(errno));
    return -1;
  }

  FILE *stderr_log = fopen(stderr_logfile, "a");
  if (stdin_log == NULL) {
    STDERR("Failed to open stdout log file: %s\n", strerror(errno));
    return -1;
  }

  int pipes[3][2];
  runCmd(pipes, cmd_args[0], cmd_args);

  pthread_t stdout_logger, stdin_logger, stderr_logger; 
  sem_t *sem = sem_open("psn_semaphore", O_CREAT, 0600, 0);
  if (sem == SEM_FAILED) {
    STDERR("creating semaphore: %s\n", strerror(errno));
    return -1;
  }

  copy_args stdout_args = {.in = pipes[1][0], .out = STDOUT_FILENO, .log = stdout_log, .sem = sem};
  copy_args stdin_args = {.in = STDIN_FILENO, .out = pipes[0][1], .log = stdin_log, .sem = sem};
  copy_args stderr_args = {.in = pipes[2][0], .out = STDERR_FILENO, .log = stderr_log, .sem = sem};
  pthread_create(&stdout_logger, NULL, copy_data, &stdout_args);
  pthread_create(&stdin_logger, NULL, copy_data, &stdin_args);
  pthread_create(&stderr_logger, NULL, copy_data, &stderr_args);

  sem_wait(sem);
  // close(pipes[1][0]);
  close(STDIN_FILENO);
  // close(pipes[2][0]);
  pthread_join(stdout_logger, NULL);
  pthread_join(stdin_logger, NULL);
  pthread_join(stderr_logger, NULL);
  /*
  int bufsize = 1024;
  char *buf = malloc(bufsize);
  for(;;) {
    int n = read(pipes[1][0], buf, bufsize);
    if (n == 0) {
      break;
    }
    if (n == -1) {
      STDERR("failed to read: %s\n", strerror(errno));
      return -1;
    }
    write(STDOUT_FILENO, buf, n);
  }
  */
  
  return 0;
}

