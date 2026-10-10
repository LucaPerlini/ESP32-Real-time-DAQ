/*
 * serial.c  --  Raw serial port setup (Linux termios).
 */
#include "serial.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

static speed_t baud_to_speed(int baud)
{
    switch (baud) {
        case 115200:  return B115200;
        case 460800:  return B460800;
        case 921600:  return B921600;
        case 1500000: return B1500000;
        case 2000000: return B2000000;
        default:      return 0;
    }
}

int serial_open(const char *dev, int baud)
{
    speed_t sp = baud_to_speed(baud);
    if (sp == 0) {
        fprintf(stderr, "serial: unsupported baud %d\n", baud);
        return -1;
    }

    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror("serial: open");
        return -1;
    }

    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) {
        perror("serial: tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, sp);
    cfsetospeed(&tio, sp);
    tio.c_cflag |= (CLOCAL | CREAD);
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cc[VMIN]  = 1;   /* block for at least 1 byte */
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        perror("serial: tcsetattr");
        close(fd);
        return -1;
    }
    tcflush(fd, TCIFLUSH);
    return fd;
}
