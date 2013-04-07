#include "dmx_serial.h"
/* Puts all the platformdependant hacks in a separate file */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <asm/termios.h>
#include <asm/ioctls.h>
#include <string.h>

#ifndef BOTHER
#define    BOTHER CBAUDEX
#endif
extern int ioctl(int d, int request, ...);

#define DMX_RATE 250000

int 
dmx_serial_open(const char *device, GError **err)
{
  struct termios2 settings;
  int fd = open(device, O_RDONLY);
  if (fd < 0) {
    g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(errno),
		"open failed: %s", strerror(errno));
    return -1;
  }
  if (ioctl(fd, TCGETS2, &settings) < 0) {
      g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(errno),
		  "ioctl TCGETS2 failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  settings.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | INPCK | ISTRIP
			| INLCR | IGNCR | ICRNL | IXON | PARMRK);
  settings.c_iflag |= PARMRK;
  settings.c_oflag &= ~OPOST;
  settings.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  settings.c_cflag &= ~(CSIZE | PARENB | CBAUD);
  settings.c_cflag |= CS8 | BOTHER;
  settings.c_ispeed = DMX_RATE;
  settings.c_ospeed = DMX_RATE;
  if (ioctl(fd, TCSETS2, &settings) < 0) {
    g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(errno),
		"ioctl TCSETS2 failed: %s", strerror(errno));
    close(fd);
    return -1;
  }
  
  return fd;
}
