// Link layer header.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

typedef enum
{
   LlTx,
   LlRx,
} connectionParametersRole;

typedef enum
{
   START,
   FLAG_RECEIVED,
   A_RECEIVED,
   C_RECEIVED,
   BCC1_OK,
   EXIT,
   ESCAPE_FOUND,
   PACKET,
   DISCONNECT,
   BCC2_OK
} connectionParametersState;

typedef struct
{
    char serialPort[50];
    connectionParametersRole role;
    int baudRate;
    int nRetransmissions;
    int timeout;
} connectionParameters;

// Open a connection using the "port" parameters defined in struct connectionParameters.
// Return "1" on success or "-1" on error.
int llopen(connectionParameters connectionParameters);

// Send data in buf with size bufSize.
// Return number of chars written, or "-1" on error.
int llwrite(int fd, const unsigned char *buf, int bufSize);

// Receive data in packet.
// Return number of chars read, or "-1" on error.
int llread(int fd, unsigned char *packet);

// Close previously opened connection.
// if showStatistics == TRUE, link layer should print statistics in the console on close.
// Return "1" on success or "-1" on error.
int llclose(int fd);

// timeout
void alarmHandler(int signal);

unsigned char readControlFrame (int fd);

int controlFrame(int fd, unsigned char A, unsigned char C);

#endif // _LINK_LAYER_H_
