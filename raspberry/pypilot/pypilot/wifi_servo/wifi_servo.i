/* File: wifi_servo.i */
%module wifi_servo

%{
#include "wifi_servo.h"
%}

class WifiServo
{

public:
    enum CommandResult {OK, COMMAND_NOT_SENT, NOT_CONNECTED, UNKNOWN };
    WifiServo();

    const char *wheel(double relativeTime);
    const char *heading(double heading);
    const char *track(double track);
    const char *mode(char *mode);
    const char *enabled(int enabled);
    bool fault();

    int flags;
};
