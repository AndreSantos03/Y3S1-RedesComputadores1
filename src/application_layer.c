// Application layer protocol implementation

#include "../include/application_layer.h"
#include "../include/link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include<unistd.h>

#define BUF_SIZE 256
// Campo de Controlo
// Define o tipo de trama 
#define C_DADOS 0x01
#define C_START 0x02
#define C_END 0x03

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // GETh CONNECTION PARAMETERS
    LinkLayer connectionParameters;
    int i = 0;
    while (serialPort[i] != '\0'){
        connectionParameters.serialPort[i] = serialPort[i];
        i++;
    }

    connectionParameters.serialPort[i] = serialPort[i];
    if (role[0] == 't') connectionParameters.role = LlTx;
    else if (role[0] == 'r') connectionParameters.role = LlRx;
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // CALL LLOPEN
    if (llopen(connectionParameters) == -1) return;

    switch (connectionParameters.role)
    {
    case LlTx:
        // SENDS FILE PACKET BY PACKET
        {
            sendFile(filename);
        }
        break;
    case LlRx:
        // RECEIVES FILE PACKET BY PACKET
        {
            receiveFile();
        }
    default:
        break;
    }

    // CALL LLCLOSE
    llclose(connectionParameters);
}

void sendFile(const char *filename) {
    // OPEN FILE FOR READING
    FILE *file;
    file = fopen(filename, "rb");

    // GET FILE SIZE
    struct stat stats;
    stat(filename, &stats);
    size_t filesize = stats.st_size;
    printf("File Size: %ld bytes \n", filesize);
    printf("File Name: %s \n", filename);

    // READ FROM FILE
    unsigned char *filedata;
    filedata = (unsigned char *)malloc(filesize);
    fread(filedata, sizeof(unsigned char), filesize, file);

    // SEND TRAMA START
    unsigned char start[30];
    start[0] = C_START;

    // 0 - file size
    start[1] = 0x0;
    start[2] = sizeof(size_t);
    start[3] = (filesize >> 24) & 0xFF;
    start[4] = (filesize >> 16) & 0xFF;
    start[5] = (filesize >> 8) & 0xFF;
    start[6] = filesize & 0xFF;
    
    // 1 - file name
    start[7] = strlen(filename)+1; 
    int i = 8;
    for(int j = 0; j < strlen(filename)+1; j++, i++) {
        start[i] = filename[j];
    }

    llwrite(start, i);
    printf("START MESSAGE SENT - %d bytes written \n", i);

    // SEND FILE DATA BY CHUNKS
    size_t bytes_to_send = filesize;
    unsigned int N = 0; unsigned int index_file_data = 0;
    while (bytes_to_send != 0)
    {
        if (bytes_to_send >= 100) {
            unsigned char data_packet[104]; // enviar 100 bytes
            data_packet[0] = C_DADOS;
            data_packet[1] = N % 255; // N – número de sequência (módulo 255)
            data_packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            data_packet[3] = 0x64; // (K = 256 * L2 + L1)
            for (int i = 4; i < 104; i++, index_file_data++) {
                data_packet[i] = filedata[index_file_data];
            }
            if (llwrite(data_packet, 104) == -1) break;
            //sleep(3);
            bytes_to_send-=100;
            printf("DATA PACKET %d SENT - %d bytes written (%ld bytes left) \n", N, 104, bytes_to_send);
        }
        else {
            unsigned char data_packet[bytes_to_send+4]; // enviar os restantes bytes quando bytes_to_send < 100
            data_packet[0] = C_DADOS;
            data_packet[1] = N % 255; // N – número de sequência (módulo 255)
            data_packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            data_packet[3] = bytes_to_send; // (K = 256 * L2 + L1)
            for (int i = 4; i < bytes_to_send+4; i++, index_file_data++) {
                data_packet[i] = filedata[index_file_data];
            }
            if (llwrite(data_packet, bytes_to_send+4) == -1) break;
            //sleep(3);
            printf("DATA PACKET %d SENT - %ld bytes written (%d bytes left) \n", N, bytes_to_send, 0);
            bytes_to_send = 0;
        }
        N++;
    }

    // SEND TRAMA END
    unsigned char end[30];
    end[0] = C_END;
    for (int i = 1; i < 30; i++) {
        end[i] = start[i];
    }
    llwrite(end, i);
    printf("END MESSAGE SENT - %d bytes written \n", i);

    // CLOSE FILE FOR READING
    fclose(file);
}

void receiveFile() {
    // RECEIVE TRAMA START
    unsigned char packet[BUF_SIZE];
    int bytes = llread(packet);
    if (bytes != -1) printf("START RECEIVED - %d bytes received \n", bytes);

    // check start value
    if (packet[0] != C_START) exit(-1);

    // get file size
    if (packet[1] != 0x0) exit(-1);
    size_t filesize = (packet[3] << 24) | (packet[4] << 16) | (packet[5] << 8) | (packet[6]);

    // get file name 
    char filename[20];
    for(int j = 0, i = 8; j < packet[7]; j++, i++) {
        filename[j] = packet[i];
    }

    printf("File Size: %ld bytes\n", filesize);
    printf("File Name: %s \n", filename);

    // RECEIVE PACKET BY PACKET UNTIL TRAMA END
    unsigned char filedata[filesize];
    unsigned int index_file_data = 0;
    while (TRUE)
    {
        unsigned char data_received[BUF_SIZE];
        int bytes = llread(data_received);

        if (bytes != -1) {
            // check control
            if (data_received[0] == C_DADOS) {
                if (bytes != -1) printf("DATA PACKET RECEIVED - %d bytes received \n", bytes);
                // get K
                unsigned int K = data_received[2] * 256 + data_received[3];
                for (int i = 4; i < K+4; i++, index_file_data++) {
                    filedata[index_file_data] = data_received[i];
                }
            }
            // end cycle
            if (data_received[0] == C_END) {
                if (bytes != -1) printf("END MESSAGE RECEIVED - %d bytes received \n", bytes);
                break;
            }
        }
    }

    // SAVE FILE
    // get received file name
    unsigned char final_file_name[strlen(filename)+10];
    for (int i = 0, j = 0; i < strlen(filename)+10; i++, j++) {
        if (filename[j] == '.') {
            unsigned char received[10] = "-received";
            for (int k = 0; k < 9; k++, i++) {
                final_file_name[i] = received[k];
            }
        }
        final_file_name[i] = filename[j];
    }
    // open file for writing
    FILE *file;
    file = fopen((char *) final_file_name, "wb+");

    // write data to file
    fwrite(filedata, sizeof(unsigned char), filesize, file);

    // CLOSE FILE FOR WRITING
    fclose(file);
}
