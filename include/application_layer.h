// Application layer protocol header.
// NOTE: This file must not be changed.

#ifndef _APPLICATION_LAYER_H_
#define _APPLICATION_LAYER_H_

#include <stdio.h>

// Application layer main function.
// Arguments:
//   serialPort: Serial port name (e.g., /dev/ttyS0).
//   role: Application role {"tx", "rx"}.
//   baudrate: Baudrate of the serial port.
//   nTries: Maximum number of frame retries.
//   timeout: Frame timeout.
//   filename: Name of the file to send / receive.
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename);

void RcvFileSize_helper(unsigned char* packet, int size, unsigned long int *fileSize);

void D_Packet_helper(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer);

unsigned char * C_Packet(const unsigned int ctrlField, const char* filename, long int length, unsigned int* size);

unsigned char * D_Packet(unsigned char seq, unsigned char *data, int dataSize, int *packetSize);

#endif // _APPLICATION_LAYER_H_