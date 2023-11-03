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

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    LinkLayer connectionParameters;
    strcpy(connectionParameters.serialPort,serialPort);
    connectionParameters.role = strcmp(role, "tx") ? receiver : transmitter;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    int fd = llopen(connectionParameters);
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    switch (connectionParameters.role) {

        case transmitter: {
            
            FILE* file = fopen(filename, "rb");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            //Calculate the file size by moving the file pointer from the 
            //beginning of the file to the end and calculate the difference
            int initial = ftell(file);
            fseek(file,0L,SEEK_END);
            long int f_size = ftell(file) - initial;
            fseek(file, initial, SEEK_SET);

            //Write start packet for the receiver to know that packets are comming
            unsigned int C_PacketSize;
            unsigned char *startPacket = C_Packet(2, filename, f_size, &C_PacketSize);
            if(llwrite(startPacket, C_PacketSize) == -1){ 
                printf("An error occurred in the start Packet\n");
                exit(-1);
            }

            //Write data packets until there are no more data bytes left to send
            unsigned char i = 0;
            unsigned char* stuff = (unsigned char*)malloc(sizeof(unsigned char) * f_size);
            fread(stuff, sizeof(unsigned char), f_size, file);
            long int bytesLeftToSend = f_size;

            while (bytesLeftToSend >= 0) { 

                int size_of_data = bytesLeftToSend > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : bytesLeftToSend;
                unsigned char* data = (unsigned char*) malloc(size_of_data);
                memcpy(data, stuff, size_of_data);
                int packetSize;
                unsigned char* packet = D_Packet(i, data, size_of_data, &packetSize);
                
                if(llwrite(packet, packetSize) == -1) {
                    printf("An error occurred in the data Packet\n");
                    exit(-1);
                }

                bytesLeftToSend -= (long int) MAX_PAYLOAD_SIZE; 
                stuff += size_of_data; 
                i = (i + 1) % 255;   
            }

            //Write final packet for the receiver to know the delivery is finished
            unsigned char *endPacket = C_Packet(3, filename, f_size, &C_PacketSize);
            if(llwrite(endPacket, C_PacketSize) == -1) { 
                printf("An error occurred in the end Packet\n");
                exit(-1);
            }
            llclose(1); 
            break;
        }

        case receiver: {

            unsigned char *packet = (unsigned char *)malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;

            while ((packetSize = llread(packet)) < 0);

            //New file size
            unsigned long int rcvFileSize = 0;
            RcvFileSize_helper(packet, packetSize, &rcvFileSize);

            //Write new file
            FILE* newFile = fopen((char *) filename, "wb+");

            while (1) {    

                while ((packetSize = llread(packet)) < 0);

                if(packetSize == 0) break; //Breaks if every packet has been read

                else if(packet[0] != 3){
                    unsigned char *buffer = (unsigned char*)malloc(packetSize);
                    D_Packet_helper(packet, packetSize, buffer);
                    fwrite(buffer, sizeof(unsigned char), packetSize - 4, newFile);
                    free(buffer);
                }

                else continue;
            }

            fclose(newFile);
            break;
        }
        default:
            exit(-1);
            break;
    }
}

void RcvFileSize_helper(unsigned char* packet, int size, unsigned long int *f_size) {

    // File Size
    unsigned char fSizeB = packet[2];
    unsigned char fSizeAux[fSizeB];
    memcpy(fSizeAux, packet + 3, fSizeB);
    for(unsigned int i = 0; i < fSizeB; i++)
        *f_size |= (fSizeAux[fSizeB-i-1] << (8*i));
        
}

unsigned char * C_Packet(const unsigned int ctrlField, const char* filename, long int length, unsigned int* size){

    int len1 = 0;
    unsigned int tmp = length;
    while (tmp > 1) {
        tmp >>= 1;
        len1++;
    }
    len1 = (len1 + 7) / 8; //file size (bytes)
    const int len2 = strlen(filename); //file name (bytes)
    *size = 5 + len1 + len2;
    unsigned char *packet = (unsigned char*)malloc(*size);
    
    unsigned int pos = 0;
    packet[pos++] = ctrlField;
    packet[pos++] = 0; // T_1 (0 = file size)
    packet[pos++] = len1; // L_1

    for (unsigned char i = 0 ; i < len1 ; i++) {
        packet[2+len1-i] = length & 0xFF;
        length >>= 8; // V_1
    }

    pos += len1;
    packet[pos++] = 1; // T_2 (1 = file name)
    packet[pos++] = len2; // L_2

    memcpy(packet + pos, filename, len2); // V_2

    return packet;
}

unsigned char * D_Packet(unsigned char i, unsigned char *data, int size_of_data, int *packetSize){

    *packetSize = 4 + size_of_data;
    unsigned char* packet = (unsigned char*)malloc(*packetSize);

    packet[0] = 1;   
    packet[1] = i;
    packet[2] = size_of_data >> 8 & 0xFF;
    packet[3] = size_of_data & 0xFF;
    memcpy(packet + 4, data, size_of_data);

    return packet;
}

void D_Packet_helper(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer) {
    memcpy(buffer, packet + 4, packetSize - 4);
    buffer += (packetSize + 4);
}