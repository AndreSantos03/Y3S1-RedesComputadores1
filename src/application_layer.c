// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Function to establish a connection and handle data transfer
void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename) {

    // Define and initialize link layer connection parameters
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort, serialPort);
    connectionParameters.role = (strcmp(role, "tx") != 0) ? receiver : transmitter; // Compare the role string
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Establish a connection using link layer
    int fd = llopen(connectionParameters);
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    switch (connectionParameters.role) {

        case transmitter: {
            // Sender role

            // Open the file for reading
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            // Calculate the file size
            int initial = ftell(file);
            fseek(file, 0L, SEEK_END);
            long int f_size = ftell(file) - initial;
            fseek(file, initial, SEEK_SET);


            // Create and send the start packet to signal the beginning of transmission
            unsigned int controlPacketSize;
            unsigned char *startPacket = createControlPacket(2, filename, f_size, &controlPacketSize);
            if (llwrite(startPacket, controlPacketSize) == -1) {
                printf("An error occurred in the start Packet\n");
                exit(-1);
            }

            // Send data packets until there are no more data bytes left to send
            unsigned char i = 0;
            unsigned char *stuff = (unsigned char *)malloc(sizeof(unsigned char) * f_size);
            fread(stuff, sizeof(unsigned char), f_size, file);
            long int bytesLeftToSend = f_size;

            while (bytesLeftToSend > 0) {
                int size_of_data = (bytesLeftToSend > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : bytesLeftToSend;
                unsigned char *data = (unsigned char *)malloc(size_of_data);
                memcpy(data, stuff, size_of_data);
                int packetSize;
                /* unsigned char *packet = D_Packet(i, data, size_of_data, &packetSize); */

                unsigned char *packet = (unsigned char *)malloc(packetSize);

                // Populate the data packet fields
                packet[0] = 1; // Data packet type
                packet[1] = i; // Packet sequence number
                packet[2] = size_of_data >> 8 & 0xFF; // High byte of size_of_data
                packet[3] = size_of_data & 0xFF; // Low byte of size_of_data

                // Copy the data into the data packet
                memcpy(packet + 4, data, size_of_data);
                
                if (llwrite(packet, packetSize) == -1) {
                    printf("An error occurred in the data Packet\n");
                    exit(-1);
                }

                bytesLeftToSend -= MAX_PAYLOAD_SIZE;
                if (bytesLeftToSend <= 0) {
                    printf("Sent Packet with %d bytes --- 0 left to be sent! \n", packetSize);
                } else {
                    printf("Sent Packet with %d bytes --- %ld left to be sent! \n", packetSize, bytesLeftToSend);
                }
                printf("-----------------------\n");
                stuff += size_of_data;
                i = (i + 1) % 255;
            }

            // Send the final packet to signal the end of transmission
            unsigned char *endPacket = createControlPacket(3, filename, f_size, &controlPacketSize);
            while (llwrite(endPacket, controlPacketSize) == -1) {
                printf("An error occurred in the end Packet\n");
                
            }


            // Close the connection
            llclose(1);
            break;
        }

        case receiver: {
            // Receiver role

            unsigned char *packet = (unsigned char *)malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;

            // Wait for the start packet to initiate the reception
            while ((packetSize = llread(packet)) < 0);

            // Extract the new file size from the start packet
            unsigned char fSizeB = packet[2];
            unsigned char fSizeAux[fSizeB];
            memcpy(fSizeAux, packet + 3, fSizeB);

            // Reconstruct the file size
            unsigned long int rcvFileSize = 0;
            for (unsigned int i = 0; i < fSizeB; i++)
                rcvFileSize |= (fSizeAux[fSizeB - i - 1] << (8 * i));

            // Open a new file for writing
            FILE *newFile = fopen((char *)filename, "wb+");

            // Receive and write data packets until the end packet is received
            while (1) {

                // Wait for the next packet
                while ((packetSize = llread(packet)) < 0);

                // Break if the end packet is received
                if (packetSize == 0) break;

                // Check if the packet is a data packet (not an end packet)
                else if (packet[0] != 3) {
                    unsigned char *buffer = (unsigned char *)malloc(packetSize);
                    D_Packet_helper(packet, packetSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), packetSize - 4, newFile);
                    free(buffer);
                }

                // Continue if the packet is an end packet
                else continue;
            }

            // Close the new file
            fclose(newFile);
            break;
        }
        default:
            exit(-1);
            break;
    }
}

// Helper function to create a control packet
unsigned char *createControlPacket(const unsigned int ctrlField, const char *filename, long int length, unsigned int *size) {

    int len1 = 0;
    unsigned int tmp = length;

    // Calculate the number of bytes required to represent the file size
    while (tmp > 1) {
        tmp >>= 1;
        len1++;
    }
    len1 = (len1 + 7) / 8; // File size in bytes

    // Calculate the length of the file name
    const int len2 = strlen(filename);

    // Calculate the total size of the control packet
    *size = 5 + len1 + len2;
    unsigned char *packet = (unsigned char *)malloc(*size);

    // Populate the control packet fields
    unsigned int pos = 0;
    packet[pos++] = ctrlField;
    packet[pos++] = 0;          // T_1 (0 = file size)
    packet[pos++] = len1;        // L_1

    // Fill in the bytes representing the file size in the control packet
    for (unsigned char i = 0; i < len1; i++) {
        packet[2 + len1 - i] = length & 0xFF;
        length >>= 8; // V_1
    }

    pos += len1;
    packet[pos++] = 1; // T_2 (1 = file name)
    packet[pos++] = len2; // L_2

    // Copy the file name into the control packet
    memcpy(packet + pos, filename, len2); // V_2

    return packet;
}

// Helper function to create a data packet
unsigned char *D_Packet(unsigned char i, unsigned char *data, int size_of_data, int *packetSize) {

    *packetSize = 4 + size_of_data;
    unsigned char *packet = (unsigned char *)malloc(*packetSize);

    // Populate the data packet fields
    packet[0] = 1; // Data packet type
    packet[1] = i; // Packet sequence number
    packet[2] = size_of_data >> 8 & 0xFF; // High byte of size_of_data
    packet[3] = size_of_data & 0xFF; // Low byte of size_of_data

    // Copy the data into the data packet
    memcpy(packet + 4, data, size_of_data);

    return packet;
}

// Helper function to extract data from a data packet
void D_Packet_helper(const unsigned char *packet, const unsigned int packetSize, unsigned char *buffer) {
    // Copy data from the data packet, excluding the packet header
    memcpy(buffer, packet + 4, packetSize - 4);
    buffer += (packetSize - 4);
}
