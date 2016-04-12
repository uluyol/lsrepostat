/*
Copyright © 2016 Muhammed Uluyol <uluyol0@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include <cctype>
#include <cstdbool>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#ifndef DEBUG
#define DEBUG false
#endif

enum Mode {
  kNone = 0,
  kUncommitted = 1 << 0,
  kUntracked = 1 << 1,
  kUnstaged = 1 << 2,
  kUnpushed = 1 << 3,
  kAny = kUncommitted | kUntracked | kUnstaged | kUnpushed,
};

inline Mode operator|(Mode a, Mode b) {
  return static_cast<Mode>(static_cast<int>(a) | static_cast<int>(b));
}

void usage(char *name) {
  fprintf(stderr, "Usage: %s [-ctspa] [dir...]\n\n", name);
  fprintf(stderr, "\t-c\tlist repositories with uncommitted changes\n");
  fprintf(stderr, "\t-t\tlist repositories with untracked changes\n");
  fprintf(stderr, "\t-s\tlist repositories with unstaged changes\n");
  fprintf(stderr, "\t-p\tlist repositories with unpushed changes\n");
  fprintf(stderr, "\t-a\tlist repositories with any pending work (default)\n");
}

int Recurse(const std::string path, enum Mode mode);

int main(int argc, char **argv) {
  enum Mode mode = kNone;
  int c;

  while ((c = getopt(argc, argv, "utspa")) != -1) {
    switch (c) {
    case 'c':
      mode = mode | kUncommitted;
      break;
    case 't':
      mode = mode | kUntracked;
      break;
    case 's':
      mode = mode | kUnstaged;
      break;
    case 'p':
      mode = mode | kUnpushed;
      break;
    case 'a':
      mode = kAny;
      break;
    default:
      usage(argv[0]);
      return 2;
    }
  }

  if (optind >= argc)
    return Recurse(".", mode);

  for (int i = optind; i < argc; i++) {
    int ret = Recurse(std::string(argv[i]), mode);
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

typedef struct {
  int ret;
  bool empty;
} ExecStatus;

static int ExecInDirGetOutArgs(const char *dir, std::vector<char> *output,
                               const char *file,
                               std::vector<const char *> &args) {
  if (DEBUG) {
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs dir: %s\n", dir);
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs cmd:");
    for (auto &a : args) {
      if (a != 0)
        fprintf(stderr, " %s", a);
    }
    fprintf(stderr, "\n");
  }

  int fd[2];
  pipe(fd);

  pid_t pid = fork();
  if (pid == -1) {
    perror("failed to run command");
    exit(3);
  } else if (pid == 0) {
    dup2(fd[1], 1);
    close(fd[0]);
    if (!DEBUG) {
      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, 2);
    }
    if (chdir(dir) != 0) {
      char errmesg[128];
      strerror_r(errno, errmesg, sizeof(errmesg));
      fprintf(stderr, "failed to cd into %s: %s\n", dir, errmesg);
    }
    execvp(file, (char *const *)&args[0]);
  }
  close(fd[1]);

  if (DEBUG)
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs: reading...\n");

  ssize_t total = 0;
  ssize_t n = 0;
  do {
    total += n;
    output->resize(total + BUFSIZ);
  } while ((n = read(fd[0], &(output->at(total)), BUFSIZ)) > 0);
  output->resize(total);

  if (DEBUG) {
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs: empty stdout: %d\n",
            total == 0);
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs: waiting...\n");
  }

  int wstatus;
  if (waitpid(pid, &wstatus, 0) == -1) {
    perror("failed to wait");
    exit(4);
  }
  if (DEBUG)
    fprintf(stderr, "DEBUG: ExecInDirGetOutArgs: returned\n");

  if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0)
    return 0;
  return 1;
}

int ExecInDirGetOut(const char *dir, std::vector<char> *output,
                    const char *file, ...) {
  std::vector<const char *> args;
  args.push_back(file);
  va_list ap;

  va_start(ap, file);
  const char *s;
  while ((s = va_arg(ap, const char *)) != 0) {
    args.push_back(s);
  }
  va_end(ap);
  args.push_back(0);
  return ExecInDirGetOutArgs(dir, output, file, args);
}

ExecStatus ExecInDir(const char *dir, const char *file, ...) {
  std::vector<const char *> args;
  args.push_back(file);
  va_list ap;

  va_start(ap, file);
  const char *s;
  while ((s = va_arg(ap, const char *)) != 0) {
    args.push_back(s);
  }
  va_end(ap);
  args.push_back(0);

  std::vector<char> out;
  ExecStatus status;
  status.ret = ExecInDirGetOutArgs(dir, &out, file, args);
  status.empty = out.size() == 0;

  return status;
}

class VcsChecker {
public:
  VcsChecker(){};
  virtual ~VcsChecker(){};
  virtual bool HasUncommitted() = 0;
  virtual bool HasUnstaged() = 0;
  virtual bool HasUntracked() = 0;
  virtual bool HasUnpushed() = 0;

protected:
  const char *path_;
};

class GitChecker : public VcsChecker {
public:
  GitChecker(std::string path) { path_ = path.c_str(); }
  bool HasUncommitted();
  bool HasUnstaged();
  bool HasUntracked();
  bool HasUnpushed();
};

bool GitChecker::HasUncommitted() {
  auto st = ExecInDir(path_, "git", "diff-index", "--cached", "--quiet", "HEAD",
                      (char *)0);
  return st.ret != 0;
}

bool GitChecker::HasUnstaged() {
  return ExecInDir(path_, "git", "diff-files", "--quiet", (char *)0).ret != 0;
}

bool GitChecker::HasUntracked() {
  auto st = ExecInDir(path_, "git", "ls-files", "-o", "--exclude-standard",
                      (char *)0);
  return !st.empty;
}

void TrimRight(std::string *s) {
  ssize_t i;

  for (i = s->size() - 1; i >= 0; i--) {
    if (!isspace((*s)[i]))
      break;
  }

  s->resize(i + 1);
}

bool GitChecker::HasUnpushed() {
  std::vector<char> out;

  if (ExecInDirGetOut(path_, &out, "git", "symbolic-ref", "HEAD", (char *)0) !=
      0)
    return false;
  std::string local_name(out.begin(), out.end());
  TrimRight(&local_name);

  if (ExecInDirGetOut(path_, &out, "git", "rev-parse", local_name.c_str(),
                      (char *)0) != 0)
    return false;
  std::string local_rev(out.begin(), out.end());
  TrimRight(&local_rev);

  if (ExecInDirGetOut(path_, &out, "git", "for-each-ref",
                      "--format=%(upstream:short)", local_name.c_str(),
                      (char *)0) != 0)
    return false;
  std::string remote_name(out.begin(), out.end());
  TrimRight(&remote_name);

  if (ExecInDirGetOut(path_, &out, "git", "rev-parse", remote_name.c_str(),
                      (char *)0) != 0)
    return false;
  std::string remote_rev = std::string(out.begin(), out.end());
  TrimRight(&remote_rev);
  return local_rev != remote_rev;
}

bool isdir(const std::string path) {
  struct stat s;
  if (stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode))
    return true;
  return false;
}

int RecurseSubdirs(const std::string path, enum Mode mode) {
  DIR *dir = opendir(path.c_str());
  struct dirent *entry;
  for (entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
    if (entry->d_type != DT_DIR)
      continue;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    std::string p = path + "/" + entry->d_name;
    int ret = 0;
    if ((ret = Recurse(p, mode)) != 0)
      return ret;
  }
  return 0;
}

int Recurse(const std::string path, enum Mode mode) {
  struct stat s;
  if (stat(path.c_str(), &s) == -1) {
    perror("");
    return 1;
  }
  VcsChecker *checker = nullptr;
  if (isdir(path + "/.git"))
    checker = new GitChecker(path);

  if (checker == nullptr) {
    return RecurseSubdirs(path, mode);
  } else {
    std::vector<std::string> mesgs;
    if (mode | kUncommitted && checker->HasUncommitted())
      mesgs.push_back("has uncommited changes");
    if (mode | kUntracked && checker->HasUntracked())
      mesgs.push_back("has untracked changes");
    if (mode | kUnstaged && checker->HasUnstaged())
      mesgs.push_back("has unstaged changes");
    if (mode | kUnpushed && checker->HasUnpushed())
      mesgs.push_back("has unpushed changes");

    for (auto &m : mesgs)
      std::cout << path << " " << m << std::endl;

    delete checker;
  }
  return 0;
}