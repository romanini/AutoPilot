/* Copyright (C) 2019 Sean D'Epagnier <seandepagnier@gmail.com>
 *
 * This Program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 */

#include <stdint.h>
#include <unistd.h>
#include <math.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include "Python.h"
#include "pymem.h"

#include "wifi_servo.h"

#define PORT 8023
#define IP "10.10.10.100"
// #define IP "172.16.0.90"
#define EOL "\n"

WifiServo::WifiServo() 
{
    this->flags = 0;
    this->isConnected = false;

    if ((this->sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
    }
    this->address.sin_family = AF_INET;
    this->address.sin_port = htons(PORT);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, IP, &(this->address.sin_addr)) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
    }
}

const char *WifiServo::wheel(double relativeTime) {
    char command[100];
    sprintf(command, "w%f",relativeTime);
    return this->sendCommand(command);
}

const char *WifiServo::heading(double heading) {
    char command[100];
    sprintf(command, "h%f",heading);
    return this->sendCommand(command);
}

const char *WifiServo::track(double track) {
    char command[100];
    sprintf(command, "t%f",track);
    return this->sendCommand(command);
}

const char *WifiServo::mode(char *mode) {
    char command[100];
    sprintf(command, "m%s",mode);
    const char *ret = this->sendCommand(command);
    printf("Mode returning %s", ret);
    return ret;
}

const char *WifiServo::enabled(int enabled) {
    char command[100];
    sprintf(command, "e%d",enabled);
    return this->sendCommand(command);

}

const char *WifiServo::sendCommand(char *command) {
    if (this->connect()) {
        size_t sendLen = send(this->sock, command, strlen(command), 0);
        if (sendLen != strlen(command)) {
            printf("\nDid not send full command [%s], only sent [%d] bytes", command, sendLen);
            return "ERROR: Command Not Sent";
        }
        // send EOL so teh arduino knows command is finished.
        send(this->sock, EOL, strlen(EOL), 0);
        char *result;         
        readline(this->sock, &result);
        return result;
        
    }
    return "ERROR: Not Connected";
}

int WifiServo::readline(int fd, char **out) {
    int buf_size = 100; 
    int in_buf = 0; 
    int ret;
    char ch; 
    char *buffer = (char *) PyMem_Malloc(buf_size); 
    char *new_buffer;

    do {
        // read a single byte
        ret = read(fd, &ch, 1);
        if (ret < 1) {
            // error or disconnect
            PyMem_Free(buffer);
            return -1;
        }

        // has end of line been reached?
        if (ch == '\n') 
            break; // yes

        // is more memory needed?
        if (in_buf == buf_size) {
            buf_size += 128; 
            new_buffer = (char *) PyMem_Realloc(buffer, buf_size); 

            if (!new_buffer) {
                PyMem_Free(buffer);
                return -1;
            } 

            buffer = new_buffer; 
        } 

        buffer[in_buf] = ch; 
        ++in_buf; 
    } while (true);

    // if the line was terminated by "\r\n", ignore the
    // "\r". the "\n" is not in the buffer
    if ((in_buf > 0) && (buffer[in_buf-1] == '\r'))
        --in_buf;

    // is more memory needed?
    if (in_buf == buf_size) {
        ++buf_size; 
        new_buffer = (char *) PyMem_Realloc(buffer, buf_size); 

        if (!new_buffer) {
            PyMem_Free(buffer);
            return -1;
        } 

        buffer = new_buffer; 
    } 

    // add a null terminator
    buffer[in_buf] = '\0';

    *out = buffer; // complete line

    return in_buf; // number of chars in the line, not counting the line break and null terminator
}

bool WifiServo::fault() {
    return flags;
}

void WifiServo::disconnect() {
    // disonnect from arduino
    if (this->isConnected) {
        close(this->client_fd);
        this->isConnected = false;
    }
}

bool WifiServo::connect() {
    if (!this->isConnected) {
        if ((this->client_fd = ::connect(this->sock, (struct sockaddr*)&(this->address), sizeof(this->address))) < 0) {
            printf("\nConnection Failed [%d]\n", errno );
        } else {
            this->isConnected = true;
    	}
    }
    return this->isConnected;
}


