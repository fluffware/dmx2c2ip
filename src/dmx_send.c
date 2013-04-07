#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <asm/termios.h>
#include <asm/ioctls.h>

#ifndef BOTHER
#define    BOTHER (CBAUDEX | B0)
#endif

extern int ioctl(int d, int request, ...);
static int 
open_serial(const char *device)
{
  struct termios2 settings;
  int fd = open(device, O_WRONLY);
  if (fd < 0) {
    fprintf(stderr, "open failed: %s", strerror(errno));
    return -1;
  }
  if (ioctl(fd, TCGETS2, &settings) < 0) {
     fprintf(stderr, "ioctl TCGETS2 failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  settings.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | INPCK | ISTRIP
			| INLCR | IGNCR | ICRNL | IXON);
  settings.c_iflag |= PARMRK;
  settings.c_oflag &= ~OPOST;
  settings.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  settings.c_cflag &= ~(CSIZE | PARENB | CBAUD);
  settings.c_cflag |= CS8 | BOTHER;
  settings.c_ispeed = 250000;
  settings.c_ospeed = 250000;
  if (ioctl(fd, TCSETS2, &settings) < 0) {
    fprintf(stderr, "ioctl TCSETS2 failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  
  return fd;
}

int
main(int argc, char *argv[])
{
  const static uint8_t packet[] = {0,0xda, 0x01, 0x02, 0x03, 0x04,0xde, 0xff, 0x34};
  int fd = open_serial(argv[1]);
  ioctl(fd, TCSBRK, 0);
  write(fd, packet, sizeof(packet));
  close(fd);
  return EXIT_SUCCESS;
}
