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
    connectionParameters.role = strcmp(role, "tx") ? LlRx : LlTx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    int fd = llopen(connectionParameters);
    if (fd < 0) {
        perror("Connection error\n");
        exit(-1);
    }

    switch (connectionParameters.role) {

        case LlTx: {
            
            FILE* file = fopen(filename, "rb");
            if (file == NULL) {
                perror("File not found\n");
                exit(-1);
            }

            //Calculate file size
            int aux = ftell(file);
            fseek(file,0L,SEEK_END);
            long int fileSize = ftell(file) - aux;
            fseek(file, aux, SEEK_SET);

            //Write initial packet
            unsigned int ctrlPacketSize;
            unsigned char *initialPacket = controlPacket(2, filename, fileSize, &ctrlPacketSize);
            if(llwrite(initialPacket, ctrlPacketSize) == -1){ 
                printf("Error in initial packet\n");
                exit(-1);
            }

            //Write data packets
            unsigned char seq = 0;
            unsigned char* content = (unsigned char*)malloc(sizeof(unsigned char) * fileSize);
            fread(content, sizeof(unsigned char), fileSize, file);
            long int remainingBytes = fileSize;

            while (remainingBytes >= 0) { 

                int dataSize = remainingBytes > (long int) MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : remainingBytes;
                unsigned char* data = (unsigned char*) malloc(dataSize);
                memcpy(data, content, dataSize);
                int packetSize;
                unsigned char* packet = dataPacket(seq, data, dataSize, &packetSize);
                
                if(llwrite(packet, packetSize) == -1) {
                    printf("Error in data packet\n");
                    exit(-1);
                }

                remainingBytes -= (long int) MAX_PAYLOAD_SIZE; 
                content += dataSize; 
                seq = (seq + 1) % 255;   
            }

            //Write final packet
            unsigned char *finalPacket = controlPacket(3, filename, fileSize, &ctrlPacketSize);
            if(llwrite(finalPacket, ctrlPacketSize) == -1) { 
                printf("Error in final packet\n");
                exit(-1);
            }
            llclose(1); 
            break;
        }

        case LlRx: {

            unsigned char *packet = (unsigned char *)malloc(MAX_PAYLOAD_SIZE);
            int packetSize = -1;

            while ((packetSize = llread(packet)) < 0);

            //New file size
            unsigned long int rcvFSize = 0;
            auxRcvFileSize(packet, packetSize, &rcvFSize);

            //Write new file
            FILE* newFile = fopen((char *) filename, "wb+");

            while (1) {    

                while ((packetSize = llread(packet)) < 0);

                if(packetSize == 0) break; //Breaks if every packet has been read
                else if(packet[0] != 3){
                    unsigned char *buffer = (unsigned char*)malloc(packetSize);
                    auxDataPacket(packet, packetSize, buffer);
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

void auxRcvFileSize(unsigned char* packet, int size, unsigned long int *fileSize) {

    // File Size
    unsigned char fSizeB = packet[2];
    unsigned char fSizeAux[fSizeB];
    memcpy(fSizeAux, packet + 3, fSizeB);
    for(unsigned int i = 0; i < fSizeB; i++)
        *fileSize |= (fSizeAux[fSizeB-i-1] << (8*i));
        
}

unsigned char * controlPacket(const unsigned int ctrlField, const char* filename, long int length, unsigned int* size){

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

unsigned char * dataPacket(unsigned char seq, unsigned char *data, int dataSize, int *packetSize){

    *packetSize = 4 + dataSize;
    unsigned char* packet = (unsigned char*)malloc(*packetSize);

    packet[0] = 1;   
    packet[1] = seq;
    packet[2] = dataSize >> 8 & 0xFF;
    packet[3] = dataSize & 0xFF;
    memcpy(packet + 4, data, dataSize);

    return packet;
}

void auxDataPacket(const unsigned char* packet, const unsigned int packetSize, unsigned char* buffer) {
    memcpy(buffer, packet + 4, packetSize - 4);
    buffer += (packetSize + 4);
}