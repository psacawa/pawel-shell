#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#define MAX_TOKENS 1000
#define MAX_CMDS 10
#define HISTFILE ".shell-history"

void system_die(const char *cmd, int linenr) {
  fprintf(stderr, "[wiersz %d] %s : %s\n", linenr, cmd, strerror(errno));
  exit(EXIT_FAILURE);
}

#define DIE_IF_NEG(cmd)                                                        \
  ({                                                                           \
    typeof(cmd) ret = cmd;                                                     \
    if (ret < 0)                                                               \
      system_die(#cmd, __LINE__);                                              \
    ret;                                                                       \
  })

#define DIE_IF_NULL(cmd)                                                       \
  ({                                                                           \
    typeof(cmd) ret = cmd;                                                     \
    if (ret == NULL)                                                           \
      system_die(#cmd, __LINE__);                                              \
    ret;                                                                       \
  })

#define DIE(msg)                                                               \
  ({                                                                           \
    fprintf(stderr, msg);                                                      \
    exit(EXIT_FAILURE);                                                        \
  })

struct cmd_idx_data {
  size_t start; /* index into token array */
  size_t num;   /* number of tokens in cmd */
};

typedef char ***cmd_t;

struct shell_cmd {
  char **tokens;
  size_t num_tokens;
};

struct shell_pipeline {
  struct shell_cmd *cmds;
  size_t num_cmds;
};

static struct shell_pipeline parse(char *line) {
  int num_tokens = 0, num_cmds = 0;
  char *tokens[MAX_TOKENS];
  struct cmd_idx_data cmds[MAX_CMDS];

  /* prosta tokenizacja */
  char *p = line, *start;
  while (*p != '\0') {
    if (isspace(*p)) {
      p++;
      continue;
    }
    start = p;
    while (!isspace(*p) && *p != '\0')
      p++;
    size_t len = p - start;
    char *buf = DIE_IF_NULL(malloc(len + 1));
    strncpy(buf, start, len);
    buf[len] = '\0';
    if (num_tokens == MAX_TOKENS)
      DIE("za wiele tokenów\n");

    tokens[num_tokens++] = buf;
  }

  /* parsować polecenia */
  ssize_t last_pipe = -1, cur = 0;
  while (cur < num_tokens) {
    while (cur < num_tokens && strcmp(tokens[cur], "|"))
      cur++;
    /* przechwyć potok na początku/końca oraz dwa potoki pod rząd */
    if (cur == 0 || cur == num_tokens - 1 || cur - last_pipe == 1)
      DIE("bład parsowania\n");
    if (num_cmds == MAX_CMDS)
      DIE("za wiele poleceń\n");

    cmds[num_cmds].start = last_pipe + 1;
    cmds[num_cmds].num = cur - cmds[num_cmds].start;
    num_cmds++;

    last_pipe = cur++;
  }

  struct shell_pipeline ret = {
      .cmds = DIE_IF_NULL(calloc(sizeof(struct shell_cmd), num_cmds)),
      .num_cmds = num_cmds};
  for (int i = 0; i != num_cmds; ++i) {
    size_t num_tokens = cmds[i].num;
    ret.cmds[i].num_tokens = num_tokens;
    ret.cmds[i].tokens = DIE_IF_NULL(calloc(sizeof(char *), num_tokens));
    for (int j = 0; j != num_tokens; ++j) {
      size_t idx = cmds[i].start + j;
      ret.cmds[i].tokens[j] = tokens[idx];
    }
  }

  /* zwolnij potoki "|" */
  for (int i = 0; i != num_cmds; ++i)
    if (cmds[i].start != 0)
      free(tokens[cmds[i].start - 1]);

  return ret;
}

static int execute_pipeline(struct shell_pipeline pipeline) {
  int pipe_fds[pipeline.num_cmds - 1][2];
  pid_t pids[pipeline.num_cmds];
  for (int i = 0; i != pipeline.num_cmds - 1; ++i)
    DIE_IF_NEG(pipe(pipe_fds[i]));

  for (int i = 0; i != pipeline.num_cmds; ++i) {
    pids[i] = DIE_IF_NEG(fork());
    if (pids[i] == 0) {
      /* dziecko */
      if (i != 0)
        DIE_IF_NEG(dup2(pipe_fds[i - 1][0], 0));
      if (i != pipeline.num_cmds - 1)
        DIE_IF_NEG(dup2(pipe_fds[i][1], 1));

#define CLOSE_PIPELINE()                                                       \
  ({                                                                           \
    for (int i = 0; i != pipeline.num_cmds - 1; ++i) {                         \
      close(pipe_fds[i][0]);                                                   \
      close(pipe_fds[i][1]);                                                   \
    }                                                                          \
  })

      CLOSE_PIPELINE();
      DIE_IF_NEG(execvp(pipeline.cmds[i].tokens[0], pipeline.cmds[i].tokens));
    }
  }

  /* rodzic */
  CLOSE_PIPELINE();

  pid_t cpid;
  int wstatus;
  while ((cpid = waitpid(-1, &wstatus, 0)) > 0)
    if (cpid == pids[pipeline.num_cmds - 1]) {
      if (WIFEXITED(wstatus)) {
        int exit_status = WEXITSTATUS(wstatus);
        return exit_status;
      } else if (WIFSIGNALED(wstatus)) {
        return 128 + WTERMSIG(wstatus);
      } else {
        return 1;
      }
    }
  if (cpid < 0)
    err(1, "wait");
  __builtin_unreachable();
}

static void print_pipeline(struct shell_pipeline pipe) {
  for (int i = 0; i != pipe.num_cmds; ++i) {
    for (int j = 0; j != pipe.cmds[i].num_tokens; ++j) {
      printf("%s ", pipe.cmds[i].tokens[j]);
    }
    printf("\n");
  }
}

int main(int argc, char *argv[]) {
  DIE_IF_NEG(read_history(HISTFILE));

  while (1) {

    /* rl_reset_line_state(); */
    char *line = readline("> ");
    add_history(line);
    if (line == NULL)
      break;

    struct shell_pipeline pipeline = parse(line);
    if (pipeline.num_cmds != 0) {
      int exit_code = execute_pipeline(pipeline);
      printf("%d\n", exit_code);
    }

    free(line);
  }

  /* DIE_IF_NEG(write_history(HISTFILE)); */
  return 0;
}
