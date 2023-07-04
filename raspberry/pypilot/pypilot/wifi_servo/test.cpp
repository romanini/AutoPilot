#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <errno.h>
#include "wifi_servo.h"

int main(void)
{
    printf("starting...");
    WifiServo servo;
    double rt1 = 0.5;
    const char *ret1 = servo.wheel(rt1);
    printf("Sent wheel [%f] and got back [%s]\n", rt1, ret1);
    double rt2 = 0.6;
    const char *ret2 = servo.wheel(rt2);
    printf("Sent wheel [%f] and got back [%s]\n", rt2, ret2);
    double h = 123.4;
    const char *ret3 = servo.heading(h);
    printf("Sent heading [%f] and got back [%s]\n", h, ret3);
    double t = 321.4;
    const char *ret4 = servo.track(t);
    printf("Sent track [%f] and got back [%s]\n", t, ret4);
    char m[] = "compass";
    const char *ret5 = servo.mode(m);
    printf("Sent mode [%s] and got back [%s]\n", m, ret5);
    int e = 1;
    const char *ret6 = servo.enabled(e);
    printf("Sent enabled [%d] and got back [%s]\n", e, ret6);
}
