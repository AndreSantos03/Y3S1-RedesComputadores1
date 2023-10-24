// Link layer protocol implementation

#include "../include/link_layer.h"
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>


// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source
struct termios oldtio;
struct termios newtio;


#define FLAG 0x7E
// Campo de Endereço
#define A_SET 0x03    // Comandos enviados pelo Emissor e Respostas enviadas pelo Receptor
#define A_UA 0x01    // Comandos enviados pelo Receptor e Respostas enviadas pelo Emissor
// Campo de Controlo
// Define o tipo de trama 
#define C_SET 0x03 
#define C_UA 0x07
#define C_DISC 0x0B
#define C_0 0x00
#define C_1 0x40
#define C_RR0 0x05
#define C_RR1 0x85
#define C_REJ 0x01
// Campo de Proteção
#define BCC_SET (A_SET ^ C_SET)
#define BCC_UA (A_UA ^ A_UA)
#define BUF_SIZE 256


int fd;
int alarmEnabled = FALSE;
int alarmCount = 0;
int trama_0 = TRUE;
unsigned char previous_trama[BUF_SIZE];

LinkLayer connection_parameters; 


// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Timeout number: %d\n", alarmCount);
    return;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    printf("-------llopen started-------");

    connection_parameters = connectionParameters;

    // Open serial port device for reading and writing
    fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    // Save current port settings
    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    // Clear struct for new port settings
    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    // Set input mode (non-canonical, no echo,...)
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 1; // Inter-character timer unused
    newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

    // VTIME e VMIN should be changed in order to protect with a
    // timeout the reception of the following character(s)

    // Now clean the line and activate the settings for the port
    // tcflush() discards data written to the object referred to
    // by fd but not transmitted, or data received but not read,
    // depending on the value of queue_selector:
    //   TCIFLUSH - flushes data received but not read.
    tcflush(fd, TCIOFLUSH);

    // Set new port settings
    if (tcsetattr(fd, TCSANOW, &newtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }
    printf("New termios structure set\n");

    //transmitter
    if(connectionParameters.role == LlTx){

        //Create the Set Message
        unsigned char setMessage[BUF_SIZE];
        setMessage[0] = FLAG;
        setMessage[1] = A_SET;
        setMessage[2] = C_SET;
        setMessage[3] = BCC_SET;
        setMessage[4] = FLAG;

        // Set alarm function handler
        (void)signal(SIGALRM, alarmHandler);

        // while loop for the ammount of times it tries to send message
        while (alarmCount <= connectionParameters.nRetransmissions)
        {
            // sends the Set Messag
            int bytes = write(fd, setMessage, 5);
            printf("Sent Set Message with %d bytes being written\n", bytes);

            // salarm setter
            if (alarmEnabled == FALSE)
            {
                alarm(connectionParameters.timeout); // alarm trigger
                alarmEnabled = TRUE;
            }

            // reads back the Ua Message
            unsigned char buf[BUF_SIZE];
            int counter = 0;
            while (counter != 5)
            {
                int bytes = read(fd, buf + counter, 1);
                if (bytes == -1){
                    break;
                    }
                if (bytes > 0) {
                    //state machine
                    switch (counter){
                        case 0:
                            if (buf[counter] == FLAG) 
                            counter = 1;
                            break;   
                        case 1:
                            if (buf[counter] == FLAG) counter = 1;
                            if (buf[counter] == A_UA) counter = 2;
                            else counter = 0;
                            break;
                        case 2:
                            if (buf[counter] == FLAG) counter = 1;
                            if (buf[counter] == C_UA) counter = 3;
                            else counter = 0;
                            break;
                        case 3:
                            if (buf[counter] == FLAG) counter = 1;
                            if (buf[counter] == BCC_UA) counter = 4;
                            else {
                                printf("bcc doesn't match\n");
                                counter = 0;
                            }
                            break;
                        case 4:
                            if (buf[counter] == FLAG) counter = 5;
                            else counter = 0;
                            break;
                        
                        default:
                            break;
                    }
                }
                // timeout
                if (alarmEnabled == FALSE) break;
            }
            
            // received ua message
            if (counter == 5) {
                alarmCount = 0;
                printf("Ua Message was received\n");
                break;
            }
        }
    }

    //receiver
    else if(connectionParameters.role == LlRx){
        //reads the Set Message
        unsigned char buf[BUF_SIZE];
        int counter = 0;
        while (counter != 5)
        {
            int bytes = read(fd, buf + counter, 1);
            if (bytes > 0) {
                // State Machine
                switch (counter){
                    case 0:
                        if (buf[counter] == FLAG) counter = 1;
                        break;
                    case 1:
                        if (buf[counter] == A_SET) counter = 2;
                        else counter = 0;
                        break;
                    case 2:
                        if (buf[counter] == FLAG) counter = 1;
                        if (buf[counter] == C_SET) counter = 3;
                        else counter = 0;
                        break;
                    case 3:
                        if (buf[counter] == FLAG) counter = 1;
                        if (buf[counter] == BCC_SET) counter = 4;
                        else {
                            printf("error in the protocol\n");
                            counter = 0;
                        }
                        break;
                    case 4:
                        if (buf[counter] == FLAG) counter = 5;
                        else counter = 0;
                        break;
                    
                    default:
                        break;
                }
            }
        }
        printf("SET RECEIVED\n");

        // Screates the Ua Message
        unsigned char ua_message[BUF_SIZE];
        ua_message[0] = FLAG;
        ua_message[1] = A_UA;
        ua_message[2] = C_UA;
        ua_message[3] = BCC_UA;
        ua_message[4] = FLAG;

        //sends the Ua Message
        int bytes = write(fd, ua_message, 5);
        printf("Sent UA Message with %d bytes being written\n", bytes);
    }
    printf("-------llopen finished-------\n");

    //timed out
    if (alarmCount > connection_parameters.nRetransmissions) {
        printf("Time out number was exceded");
        alarmCount = 0;
        return -1;
    }

    //all worked well!
    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    printf("S-------llwrite started-------\n");
    printf("Sending trama %d\n", !trama_0);

    // GENERATE BCC2
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }

    // BYTE STUFFING 
    int counter = 0;
    for (int i = 0; i < bufSize; i++) {
        // find out how many 0x7E or 0x7D are present in buf
        if (buf[i] == 0x7E || buf[i] == 0x7D) counter++;
    }
    // check bcc2
    if (BCC2 == 0x7E || BCC2 == 0x7D) counter++;
    unsigned char buf_after_byte_stuffing[bufSize+counter];
    // replace 0x7E and 0x7D
    for (int i = 0, j = 0; i < bufSize; i++) {
        if (buf[i] == 0x7E) {
            buf_after_byte_stuffing[j] = 0x7D;
            buf_after_byte_stuffing[j+1] = 0x5E;
            j+=2;
        }
        else if (buf[i] == 0x7D) {
            buf_after_byte_stuffing[j] = 0x7D;
            buf_after_byte_stuffing[j+1] = 0x5D;
            j+=2;
        }
        else {
            buf_after_byte_stuffing[j] = buf[i];
            j++;
        }
    }

    // CREATE INFORMATION MESSAGE
    unsigned char packet_to_send[bufSize+counter+6];
    packet_to_send[0] = FLAG;
    packet_to_send[1] = A_SET;
    switch (trama_0)
    {
    case TRUE:
        packet_to_send[2] = C_0;
        break;
    case FALSE:
        packet_to_send[2] = C_1;
        break;
    default:
        break;
    }
    packet_to_send[3] = packet_to_send[1] ^ packet_to_send[2];

    // add buf to information message
    for (int i = 4, j = 0; i < bufSize+counter+4; i++, j++) {
        packet_to_send[i] = buf_after_byte_stuffing[j];
    }
    // byte stuffing for bcc2
    if (BCC2 == 0x7E) {
        packet_to_send[bufSize+counter+3] = 0x7D;
        packet_to_send[bufSize+counter+4] = 0x5E;
        packet_to_send[bufSize+counter+5] = FLAG;
    }
    else if (BCC2 == 0x7D) {
        packet_to_send[bufSize+counter+3] = 0x7D;
        packet_to_send[bufSize+counter+4] = 0x5D;
        packet_to_send[bufSize+counter+5] = FLAG;
    }
    else {
        packet_to_send[bufSize+counter+4] = BCC2;
        packet_to_send[bufSize+counter+5] = FLAG;
    }

    // next trama to send
    if (trama_0 == TRUE) trama_0 = FALSE;
    else trama_0 = TRUE;

    // SEND INFORMATION MESSAGE
    // Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    // sends message at most 3 times
    while (alarmCount <= connection_parameters.nRetransmissions)
    {
        // SEND INFORMATION PACKET
        int bytes = write(fd, packet_to_send, sizeof(packet_to_send));
        printf("INFORMATION PACKET SENT - %d bytes written\n", bytes);

        // sets alarm of 3 seconds
        if (alarmEnabled == FALSE)
        {
            alarm(connection_parameters.timeout); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }

        // READ RESPONSE
        unsigned char response[BUF_SIZE];
        int i = 0;
        int STATE = 0;
        while (STATE != 5)
        {
            int bytes = read(fd, response + i, 1);
            //printf("%hx %d\n", response[i], STATE);
            if (bytes == -1) break;
            if (bytes > 0) {
                // STATE MACHINE
                switch (STATE)
                {
                case 0:
                    if (response[i] == FLAG) STATE = 1;
                    break;
                case 1:
                    if (response[i] == FLAG) STATE = 1;
                    if (response[i] == A_UA) STATE = 2;
                    else STATE = 0;
                    break;
                case 2:
                    if (response[i] == FLAG) STATE = 1;
                    if (response[i] == C_REJ) {
                        STATE = 0;
                        printf("REJ received\n");
                        break;
                    }
                    if (trama_0 == TRUE && response[i] == C_RR0) STATE = 3;
                    else if (trama_0 == FALSE && response[i] == C_RR1) STATE = 3;
                    else {
                        // SEND PREVIOUS TRAMA
                        // SEND INFORMATION PACKET
                        int bytes = write(fd, previous_trama, sizeof(previous_trama));
                        printf("INFORMATION PACKET SENT - %d bytes written\n", bytes);
                        STATE = 0;
                    }
                    break;
                case 3:
                    if (response[i] == FLAG) STATE = 1;
                    if (response[i] == (response[i-1] ^ response[i-2])) STATE = 4;
                    else STATE = 0;
                    break;
                case 4:
                    if (response[i] == FLAG) STATE = 5;
                    else STATE = 0;
                    break;
                
                default:
                    break;
                }
                i++; 
            }
            // timeout
            if (alarmEnabled == FALSE) break;
        }
        
        // RECEIVED RR MESSAGE
        if (STATE == 5) {
            alarmCount = 0;
            printf("RR RECEIVED\n");
            break;
        }
    }

    // SAVE TRAMA SENT
    for (int i = 0; i < BUF_SIZE; i++) {
        previous_trama[i] = packet_to_send[i];
    }

    printf("-------llwrite finished-------\n");

    // TIMEOUT ERROR
    if (alarmCount > connection_parameters.nRetransmissions) {
        alarmCount = 0;
        return -1;
    }

    // SUCCESSFUL
    return sizeof(packet_to_send);
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    printf("-------llread started-------\n");
    printf("Receiving trama %d\n", !trama_0);

    // READ INFORMATION PACKET
    unsigned char buf[BUF_SIZE];
    int i = 0, size_of_buf = 0, error = FALSE;
    int STATE = 0;
    while (STATE != 5)
    {
        int bytes = read(fd, buf + i, 1);
        //printf("%hx %d\n", buf[i], STATE);
        if (bytes > 0) {
            // STATE MACHINE
            switch (STATE)
            {
            case 0:
                if (buf[i] == FLAG) STATE = 1;
                break;
            case 1:
                if (buf[i] == A_SET) STATE = 2;
                else STATE = 0;
                break;
            case 2:
                if (buf[i] == FLAG) STATE = 1;
                if (trama_0 == TRUE && buf[i] == C_0) STATE = 3;
                else if (trama_0 == FALSE && buf[i] == C_1) STATE = 3;
                // trama 1 not expected
                else if (trama_0 == TRUE && buf[i] == C_1) {
                    STATE = 5;
                    error = TRUE;
                }
                // trama 0 not expected
                else if (trama_0 == TRUE && buf[i] == C_0) {
                    STATE = 5;
                    error = TRUE;
                }
                else STATE = 0;
                break;
            case 3:
                if (buf[i] == FLAG) STATE = 1;
                if (buf[i] == (buf[i-1] ^ buf[i-2])) {
                    STATE = 4;
                    i = -1;
                }
                else {
                    STATE = 0;
                    printf("error in the protocol\n");
                }
                break;
            case 4:
                if (buf[i] == FLAG) STATE = 5;
                else {
                    size_of_buf++;
                }
                break;
            
            default:
                break;
            }
            i++; 
        }
    }

    // BYTE DESTUFFING 
    int size_of_packet = size_of_buf;
    for (int i = 0, j = 0; i < size_of_buf; i++, j++) {
        if (buf[i] == 0x7D && buf[i+1] == 0x5E) {
            packet[j] = 0x7E;
            i++; size_of_packet--;
        }
        else if (buf[i] == 0x7D && buf[i+1] == 0x5D) {
            packet[j] = 0x7D;
            i++; size_of_packet--;
        }
        else {
            packet[j] = buf[i];
        }
    }

    // CHECK BCC2
    size_of_packet--;
    unsigned char BCC2 = packet[0];
    for (int i = 1; i < size_of_packet; i++) {
        BCC2 ^= packet[i];
    }

    printf("PACKET RECEIVED\n");

    // CREATE RR MESSAGE
    unsigned char rr_message[5];
    rr_message[0] = FLAG;
    rr_message[1] = A_UA;
    // BCC ERROR 
    if (BCC2 != packet[size_of_packet]) {
        printf("error in the data\n");
        // SEND REJ
        rr_message[2] = C_REJ;
        rr_message[3] = rr_message[1] ^ rr_message[2];
        rr_message[4] = FLAG;

        int bytes = write(fd, rr_message, 5);
        printf("REJ MESSAGE SENT - %d bytes written\n", bytes);

        printf("-------llread finished-------\n");
        return -1;
    }
    if (trama_0 == TRUE) {
        // DUPLICATED FRAME
        if (error) {
            rr_message[2] = C_RR0;
            printf("duplicate frame \n");
        }
        // NO ERROR
        else {
            rr_message[2] = C_RR1;
            trama_0 = FALSE;
        }
    }
    else {
        // DUPLICATED FRAME
        if (error) {
            rr_message[2] = C_RR1;
            printf("duplicate frame \n");
        }
        // NO ERROR
        else {
            rr_message[2] = C_RR0;
            trama_0 = TRUE;
        }
    }
    rr_message[3] = rr_message[1] ^ rr_message[2];
    rr_message[4] = FLAG;

    // SEND RR MESSAGE
    int bytes = write(fd, rr_message, 5);
    printf("RR MESSAGE SENT - %d bytes written\n", bytes);

    printf("FINNISHED LLREAD ---------------------------------------\n");

    // ERROR DUPLICATED TRAMA RECEIVED
    if (error) return -1;

    // SUCCESSFUL
    return size_of_packet;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(LinkLayer connectionParameters)
{
    printf("-------llclose started-------\n\n");


    // SEND/RECEIVE MESSAGES
    switch (connectionParameters.role)
    {
    // TRANSMITTER
    case LlTx:
        {
            // CREATE DISC MESSAGE
            unsigned char disc_message[BUF_SIZE];
            disc_message[0] = FLAG;
            disc_message[1] = A_SET;
            disc_message[2] = C_DISC;
            disc_message[3] = (A_SET ^ C_DISC);
            disc_message[4] = FLAG;

            // Set alarm function handler
            (void)signal(SIGALRM, alarmHandler);

            // sends message at most 3 times
            while (alarmCount <= connectionParameters.nRetransmissions)
            {
                // SEND DISC MESSAGE
                int bytes = write(fd, disc_message, 5);
                printf("DISC MESSAGE SENT - %d bytes written\n", bytes);

                // sets alarm of 3 seconds
                if (alarmEnabled == FALSE)
                {
                    alarm(connectionParameters.timeout); // Set alarm to be triggered in 3s
                    alarmEnabled = TRUE;
                }

                // READ DISC MESSAGE
                unsigned char buf[BUF_SIZE];
                int i = 0;
                int STATE = 0;
                while (STATE != 5)
                {
                    int bytes = read(fd, buf + i, 1);
                    //printf("%hx %d\n", buf[i], bytes);
                    if (bytes == -1) break;
                    if (bytes > 0) {
                        // STATE MACHINE
                        switch (STATE)
                        {
                        case 0:
                            if (buf[i] == FLAG) STATE = 1;
                            break;
                        case 1:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == A_UA) STATE = 2;
                            else STATE = 0;
                            break;
                        case 2:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == C_DISC) STATE = 3;
                            else STATE = 0;
                            break;
                        case 3:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == (A_UA ^ C_DISC)) STATE = 4;
                            else {
                                printf("error in the protocol\n");
                                STATE = 0;
                            }
                            break;
                        case 4:
                            if (buf[i] == FLAG) STATE = 5;
                            else STATE = 0;
                            break;
                        
                        default:
                            break;
                        }
                        i++; 
                    }
                    // timeout
                    if (alarmEnabled == FALSE) break;
                }
                
                // RECEIVED DISC MESSAGE
                if (STATE == 5) {
                    alarmCount = 0;
                    printf("DISC RECEIVED\n");
                    break;
                }
            }

            // SEND UA MESSAGE
            unsigned char ua_message[BUF_SIZE];
            ua_message[0] = FLAG;
            ua_message[1] = A_UA;
            ua_message[2] = C_UA;
            ua_message[3] = BCC_UA;
            ua_message[4] = FLAG;

            int bytes = write(fd, ua_message, 5);
            printf("UA MESSAGE SENT - %d bytes written\n", bytes);
        }
        break;
    // RECEIVER
    case LlRx:
        {
            // READ DISC MESSAGE
            unsigned char buf[BUF_SIZE];
            int i = 0;
            int STATE = 0;
            while (STATE != 5)
            {
                int bytes = read(fd, buf + i, 1);
                //printf("%hx %d\n", buf[i], STATE);
                if (bytes > 0) {
                    // STATE MACHINE
                    switch (STATE)
                    {
                    case 0:
                        if (buf[i] == FLAG) STATE = 1;
                        break;
                    case 1:
                        if (buf[i] == A_SET) STATE = 2;
                        else STATE = 0;
                        break;
                    case 2:
                        if (buf[i] == FLAG) STATE = 1;
                        if (buf[i] == C_DISC) STATE = 3;
                        else STATE = 0;
                        break;
                    case 3:
                        if (buf[i] == FLAG) STATE = 1;
                        if (buf[i] == (A_SET ^ C_DISC)) STATE = 4;
                        else {
                            printf("error in the protocol\n");
                            STATE = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) STATE = 5;
                        else STATE = 0;
                        break;
                    
                    default:
                        break;
                    }
                    i++; 
                }
            }
            printf("DISC RECEIVED\n");

            // SEND DISC MESSAGE
            unsigned char disc_message[BUF_SIZE];
            disc_message[0] = FLAG;
            disc_message[1] = A_UA;
            disc_message[2] = C_DISC;
            disc_message[3] = (A_UA ^ C_DISC);
            disc_message[4] = FLAG;

            int bytes = write(fd, disc_message, 5);
            printf("DISC MESSAGE SENT - %d bytes written\n", bytes);

            // READ UA MESSAGE
            i = 0;
            STATE = 0;
            while (STATE != 5)
            {
                int bytes = read(fd, buf + i, 1);
                //printf("%hx %d\n", buf[i], bytes);
                if (bytes == -1) break;
                if (bytes > 0) {
                    // STATE MACHINE
                    switch (STATE)
                    {
                    case 0:
                        if (buf[i] == FLAG) STATE = 1;
                        break;
                    case 1:
                        if (buf[i] == FLAG) STATE = 1;
                        if (buf[i] == A_UA) STATE = 2;
                        else STATE = 0;
                        break;
                    case 2:
                        if (buf[i] == FLAG) STATE = 1;
                        if (buf[i] == C_UA) STATE = 3;
                        else STATE = 0;
                        break;
                    case 3:
                        if (buf[i] == FLAG) STATE = 1;
                        if (buf[i] == BCC_UA) STATE = 4;
                        else {
                            printf("error in the protocol\n");
                            STATE = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) STATE = 5;
                        else STATE = 0;
                        break;
                    
                    default:
                        break;
                    }
                    i++; 
                }
            }
            printf("UA RECEIVED\n");
        }
        break;
    default:
        break;
    }

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    printf("-------llclose finished-------\n");

    return 1;
}
