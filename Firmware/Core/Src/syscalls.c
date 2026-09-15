#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

int _close(int file)
{
  (void)file;
  errno = ENOSYS;
  return -1;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  if (st == NULL)
  {
    errno = EINVAL;
    return -1;
  }
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

off_t _lseek(int file, off_t ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  errno = ESPIPE;
  return (off_t)-1;
}

ssize_t _read(int file, void *ptr, size_t len)
{
  (void)file;
  (void)ptr;
  (void)len;
  errno = ENOSYS;
  return -1;
}

ssize_t _write(int file, const void *ptr, size_t len)
{
  (void)file;
  (void)ptr;
  /* There is deliberately no console on the product PCB. Treat writes as a
   * sink so incidental newlib diagnostics cannot stall application code. */
  return (ssize_t)len;
}
