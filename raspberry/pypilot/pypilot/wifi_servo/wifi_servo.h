/* Copyright (C) 2021 Marco Romanini <romaninim@gmail.com>
 *
 * This Program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 */

#include <arpa/inet.h>
#include <sys/socket.h>

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

private:
    const char *sendCommand(char *command);
    int readline(int fd, char ** out);
    bool connect();
    void disconnect();
    int sock;
    int client_fd;
    char *lastError;
    struct sockaddr_in address;
    bool isConnected;
};
