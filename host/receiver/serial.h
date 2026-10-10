/*
 * serial.h  --  Minimal raw serial port setup for the host receiver.
 */
#ifndef SERIAL_H
#define SERIAL_H

/* Open `dev` in raw mode at `baud` and return the fd, or -1 on error.
 * Supported bauds: 115200, 460800, 921600, 1500000, 2000000. */
int serial_open(const char *dev, int baud);

#endif /* SERIAL_H */
