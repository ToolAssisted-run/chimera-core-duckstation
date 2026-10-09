// The POSIX calls DuckStation makes that a sandbox has no system call for,
// and the one glibc extension musl does not carry.
//
// A waterbox guest talks to the host through a small, deliberate surface: the
// files the frontend mounted, and nothing else. There is no kernel behind it,
// so a call like getcwd() does not fail - it stops the machine, because the
// sandbox refuses to guess what the guest meant.
//
// These are answered in terms of what the sandbox DOES have, and they are
// compiled into the native reference as well, so that both flavors are told
// the same thing: a path resolved on a desktop and a path resolved in the
// sandbox must be the same string, or the two machines were not given the same
// disc.

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

// The kernel's thread id (waterbox/chimera-shim.h).
extern "C" int chimera_gettid(void)
{
  return 1;
}

// Where the machine is. A sandbox stands in the one directory it has, which
// is where every mounted file already is - so the answer is "here", and a
// path resolved against it comes back naming the same file.
extern "C" char* getcwd(char* buf, size_t size)
{
  if (!buf || size < 2)
  {
    errno = ERANGE;
    return nullptr;
  }

  buf[0] = '.';
  buf[1] = '\0';
  return buf;
}

// Existence and readability are the same question here: the sandbox hands
// the guest read-only mounts, so a file that opens is readable and one that
// does not is absent. Writability is answered no - what the machine writes
// leaves through the save-data channel, never through a path.
extern "C" int access(const char* path, int mode)
{
  if (mode & W_OK)
  {
    errno = EACCES;
    return -1;
  }
  FILE* f = fopen(path, "rb");
  if (f == nullptr)
  {
    errno = ENOENT;
    return -1;
  }
  fclose(f);
  return 0;
}

// Making a directory. DuckStation keeps its own house on a desktop - a folder
// for memory cards, one for save states, one for screenshots - and creates
// them if they are missing. The root is reported as already there, which is
// true; anything else is refused as read-only, which is also true.
extern "C" int mkdir(const char* path, mode_t mode)
{
  if (path && (!strcmp(path, ".") || !strcmp(path, "./") || !strcmp(path, "/")))
  {
    errno = EEXIST;
    return -1;
  }

  errno = EROFS;
  return -1;
}

// There are no links in a sandbox - there is a flat list of mounted files.
extern "C" int lstat(const char* path, struct stat* st)
{
  return stat(path, st);
}
