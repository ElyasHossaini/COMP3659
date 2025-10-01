#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* #define ENABLE_REDIRECT */

const char *msg1 = "A message to be written to the file via fd\n";
const char *msg2 = "A message to be written to the file via alt_fd\n";
const char *msg3 = "A message to be written to the standard output\n";

int main()
{
  int fd = open("outfile", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);     /* open a file for writing */

  if (fd == -1)
    {
      printf("open failed\n");
      return 1;
    }
  else
    printf("open succeeded - file descriptor: %d\n", fd);

  int alt_fd = dup(fd);     /* create a second file descriptor serving as an alias for the first */

  if (alt_fd == -1)
    {
      printf("dup failed\n");
      return 2;
    }
  else
    printf("dup succeeded - alternative file descriptor: %d\n", alt_fd);

  write(fd, msg1, strlen(msg1));     /* can write to the file via either descriptior */
  write(fd, msg2, strlen(msg2));

#ifdef ENABLE_REDIRECT     /* at first, the line below will be stripped out during compilation */
  dup2(fd, 1);             /* redirect the standard output, so that writes there *also* go to the file (1 will be an alias as well) */
#endif

  close(fd);
  close(alt_fd);
  
  write(1, msg3, strlen(msg3));     /* write a message to the standard output (by default, the terminal) */

  return 0;
}
