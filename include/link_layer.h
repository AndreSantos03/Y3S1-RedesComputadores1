// Header file for the Link Layer protocol implementation.
// IMPORTANT: Do not modify this file.

#ifndef _LINK_LAYER_H_
#define _LINK_LAYER_H_

// Enumeration to define the role of the Link Layer (transmitter or receiver).
typedef enum {
    transmitter,
    receiver,
} LinkLayerRole;

// Struct to store Link Layer parameters, such as serial port, role, baud rate, retransmissions, and timeout.
typedef struct {
    char serialPort[50];     // Serial port identifier
    LinkLayerRole role;      // Role of the Link Layer
    int baudRate;            // Baud rate for communication
    int nRetransmissions;    // Number of retransmissions allowed
    int timeout;             // Timeout for communication
} LinkLayer;

// Enumeration to define Link Layer states.
typedef enum {
    START,
    FLAG_RECEIVED,
    A_RECEIVED,
    C_RECEIVED,
    BCC_CHECK,
    STOP_RECEIVED,
    DATA_FOUND,
    BYTE_DESTUFFING,
    DISCONNECTED,
    BCC2_CHECK
} llState;

// Maximum payload size accepted by the Link Layer.
#define MAX_PAYLOAD_SIZE 100

// Boolean values
#define FALSE 0
#define TRUE 1

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

// Define constants for serial communication.
#define BAUDRATE 38400
#define BUF_SIZE 256
#define FLAG 0x7E
#define ESC 0x7D
#define A_TX 0x03
#define A_RX 0x01
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
/* #define C_RR(Nr) ((Nr << 7) | 0x05)
#define C_REJ(Nr) ((Nr << 7) | 0x01)
#define C_N(Ns) (Ns << 6)
 */
#define C_RR0 0x05
#define C_RR1 0x85
#define C_REJ0 0x01
#define C_REJ1 0x81
#define C_N0 0x00
#define C_N1 0x40




// Function to establish a connection using the specified parameters.
// Returns "1" on success or "-1" on error.
int llopen(LinkLayer connectionParameters);

// Function to send data in the provided buffer with the specified size.
// Returns the number of characters written or "-1" on error.
int llwrite(const unsigned char *buf, int bufSize);

// Function to receive data into the packet buffer.
// Returns the number of characters read or "-1" on error.
int llread(unsigned char *packet);

// Function to close a previously opened connection.
// If showStatistics is TRUE, the Link Layer prints statistics in the console on close.
// Returns "1" on success or "-1" on error.
int llclose(int showStatistics);


#endif // _LINK_LAYER_H_
