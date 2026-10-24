#include "sim_test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>

void simRunForked(void (*body)()) {
  int fds[2];
  if (pipe(fds) != 0) TEST_FAIL_MESSAGE("pipe() failed");
  fflush(stdout);
  fflush(stderr);
  pid_t pid = fork();
  if (pid < 0) TEST_FAIL_MESSAGE("fork() failed");
  if (pid == 0) {
    close(fds[0]);
    dup2(fds[1], STDOUT_FILENO);
    if (TEST_PROTECT()) body();
    fflush(stdout);
    exit(Unity.CurrentTestFailed ? 1 : 0);  // exit(), not _exit(): gcov writes counters at exit
  }
  close(fds[1]);
  std::string out;
  char buf[512];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status)) {
    char m[64];
    snprintf(m, sizeof m, "device process crashed: signal %d", WTERMSIG(status));
    TEST_FAIL_MESSAGE(m);
  }
  if (WEXITSTATUS(status) != 0) {
    // Child line: "<file>:<line>:<test>:FAIL: <message>" -> "at <file>:<line>: <message>"
    std::string msg = "device process failed";
    size_t fail = out.find(":FAIL");
    if (fail != std::string::npos) {
      size_t start = out.rfind('\n', fail);
      start = start == std::string::npos ? 0 : start + 1;
      std::string head = out.substr(start, fail - start);  // file:line:test
      size_t lineEnd = head.find(':', head.find(':') + 1);
      std::string where = lineEnd == std::string::npos ? head : head.substr(0, lineEnd);
      size_t body = out.find(": ", fail + 1);
      size_t eol = out.find('\n', fail);
      std::string text = body == std::string::npos || body > eol ? "" : out.substr(body + 2, eol - body - 2);
      while (!text.empty() && (text.back() == '\r' || text.back() == ' ')) text.pop_back();
      msg = "at " + where + (text.empty() ? "" : ": " + text);
    }
    TEST_FAIL_MESSAGE(msg.c_str());
  }
}
