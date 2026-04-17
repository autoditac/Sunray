/* Set ASYNC_LOW_LATENCY flag on a serial port via TIOCSSERIAL ioctl.
   Usage: serial_lowlatency /dev/ttyS0 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <serial-device>\n", argv[0]);
        return 1;
    }
    int fd = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    struct serial_struct ser;
    if (ioctl(fd, TIOCGSERIAL, &ser) < 0) {
        perror("TIOCGSERIAL");
        close(fd);
        return 1;
    }
    ser.flags |= ASYNC_LOW_LATENCY;
    if (ioctl(fd, TIOCSSERIAL, &ser) < 0) {
        perror("TIOCSSERIAL");
        close(fd);
        return 1;
    }
    printf("%s: low_latency enabled\n", argv[1]);
    close(fd);
    return 0;
}
