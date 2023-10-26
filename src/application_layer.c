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
#define C_DATA 0x01
#define C_START 0x02
#define C_END 0x03

void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    // get connection parameters and store them in a struct
    LinkLayer connectionParameters;
    int i;
    for (i = 0; serialPort[i] != '\0'; i++){
        connectionParameters.serialPort[i] = serialPort[i];
    }


    connectionParameters.serialPort[i] = serialPort[i];

    if (role[0] == 't') {
        connectionParameters.role = Transmitter;
    }

    else if (role[0] == 'r') {
        connectionParameters.role = Receiver;
    }
    
    connectionParameters.baudRate = baudRate;
    connectionParameters.nRetransmissions = nTries;
    connectionParameters.timeout = timeout;

    // Open cable connection with set frame 
    if (llopen(connectionParameters) == -1) return;

    // Check if we are the Transmitter or Receiver and act acoordlingly
    if(connectionParameters.role == Transmitter){
        transmit(filename);
    }
    else {
        receive();
    }
    
    // Close cable connection
    llclose(connectionParameters);
}

void transmit(const char *filename) {
    // fopen for file reading
    FILE *file;
    file = fopen(filename, "rb");

    // get and store file size
    struct stat stats;
    stat(filename, &stats);
    size_t filesize = stats.st_size;
    printf("Size of file to be sent: %ld bytes \n", filesize);
    printf("Name of file to be sent: %s \n", filename);

    // fread to read from file
    unsigned char *filedata;
    filedata = (unsigned char *)malloc(filesize);
    fread(filedata, sizeof(unsigned char), filesize, file);

    // send start frame
    unsigned char start_frame[30];
    start_frame[0] = C_START;

    // 0 - file size
    start_frame[1] = 0x0;
    start_frame[2] = sizeof(size_t);
    start_frame[3] = (filesize >> 24) & 0xFF;
    start_frame[4] = (filesize >> 16) & 0xFF;
    start_frame[5] = (filesize >> 8) & 0xFF;
    start_frame[6] = filesize & 0xFF;
    
    // 1 - file name
    start_frame[7] = strlen(filename)+1; 
    int i = 8;
    int j = 0;
    while (j < strlen(filename) + 1){
        start_frame[i] = filename[j];
        j++;
        i++;
    }

    llwrite(start_frame, i);
    printf("Sent start frame: %d bytes written \n", i);

    // send data by packets of 100 except the last
    size_t bytes_left_to_send = filesize;
    unsigned int N = 0; unsigned int index_file_data = 0;
    while (bytes_left_to_send != 0)
    {
        if (bytes_left_to_send >= 100) {
            unsigned char packet[104]; // enviar 100 bytes em cada pacote
            packet[0] = C_DATA;
            packet[1] = N % 255; // N – número de sequência (módulo 255)
            packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            packet[3] = 0x64; // (K = 256 * L2 + L1)
            int i = 4;
            while (i < 104){
                packet[i] = filedata[index_file_data];
                i++;
                index_file_data++;
            }
            if (llwrite(packet, 104) == -1) break;
            bytes_left_to_send -= 100;
            printf("Sent data packet nº %d: %d bytes written (There is %ld bytes left to be sent) \n", N, 104, bytes_left_to_send);
            //sleep(3);

        }
        else {
            unsigned char packet[bytes_left_to_send+4]; // enviar os restantes bytes quando bytes_left_to_send < 100
            packet[0] = C_DATA;
            packet[1] = N % 255; // N – número de sequência (módulo 255)
            packet[2] = 0x0; // L2 L1 – indica o número de octetos (K) do campo de dados
            packet[3] = bytes_left_to_send; // (K = 256 * L2 + L1)
            int i = 4;
            while (i < bytes_left_to_send+4){
                packet[i] = filedata[index_file_data];
                i++;
                index_file_data++;
            }
            if (llwrite(packet, bytes_left_to_send+4) == -1) break;
            printf("Sent data packet nº %d: %ld bytes written (There is %d bytes left to be sent) \n", N, bytes_left_to_send, 0);
            bytes_left_to_send = 0;
            //sleep(3);
        }
        N++;
    }

    // send end frame
    unsigned char end[30];
    end[0] = C_END;
    int k = 1;
    while (k < 30){
        end[k] = start_frame[k];
        k++;
    }
    
    llwrite(end, i);
    printf("Sent end frame %d bytes written \n", i);

    // fclose for reading
    fclose(file);
}

void receive() {
    // receive start frame
    unsigned char packet[BUF_SIZE];
    int bytes = llread(packet);
    if (bytes != -1) printf("Received start frame: %d bytes received \n", bytes);

    // check start value
    if (packet[0] != C_START) exit(-1);

    // get file size
    if (packet[1] != 0x0) exit(-1);
    size_t filesize = (packet[3] << 24) | (packet[4] << 16) | (packet[5] << 8) | (packet[6]);

    // get file name 
    char filename[20];
    int j = 0;
    int i = 8;
    while (j < packet[7]){
        filename[j] = packet[i];
        j++;
        i++;
    }

    printf("File Size: %ld bytes\n", filesize);
    printf("File Name: %s \n", filename);

    // receive each packet one by one until end frame
    unsigned char filedata[filesize];
    unsigned int index_file_data = 0;
    while (TRUE)
    {
        unsigned char data_received[BUF_SIZE];
        int bytes = llread(data_received);

        if (bytes != -1) {
            // check control
            if (data_received[0] == C_DATA) {
                if (bytes != -1) printf("Received data packet: %d bytes received \n", bytes);
                // get K
                unsigned int K = data_received[2] * 256 + data_received[3];
                int i = 4;
                while (i < K+4){
                    filedata[index_file_data] = data_received[i];
                    i++;
                    index_file_data++;
                }
            }
            // end cycle
            if (data_received[0] == C_END) {
                if (bytes != -1) printf("Received end frame: %d bytes received \n", bytes);
                break;
            }
        }
    }

    // save file
    // get received file name
    unsigned char final_file_name[strlen(filename)+10];
    for (int i = 0, j = 0; i < strlen(filename)+10; i++, j++) {
        if (filename[j] == '.') {
            unsigned char received[10] = "-received";
            int k = 0;
            while (k < 9){
                final_file_name[i] = received[k];
                k++;
                i++;
            }
        }
        final_file_name[i] = filename[j];
    }
    // fopen for writing
    FILE *file;
    file = fopen((char *) final_file_name, "wb+");

    // fwrite to write to a file
    fwrite(filedata, sizeof(unsigned char), filesize, file);

    // fclose for writing
    fclose(file);
}
