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
int tramaType = TRUE;
unsigned char previousTrama[BUF_SIZE];

LinkLayer connection_parameters; 


// Alarm function handler
void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;

    printf("Timeout number: %d\n", alarmCount);
    return;
}
// ======================
// LLOPEN
// ======================

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

            //for tracking which bites to read
            int i = 0;
            //counter for the state
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
                            if (buf[i] == FLAG) 
                            counter = 1;
                            break;   
                        case 1:
                            if (buf[i] == FLAG) counter = 1;
                            if (buf[i] == A_UA) counter = 2;
                            else counter = 0;
                            break;
                        case 2:
                            if (buf[i] == FLAG) counter = 1;
                            if (buf[i] == C_UA) counter = 3;
                            else counter = 0;
                            break;
                        case 3:
                            if (buf[i] == FLAG) counter = 1;
                            if (buf[i] == BCC_UA) counter = 4;
                            else {
                                printf("bcc doesn't match\n");
                                counter = 0;
                            }
                            break;
                        case 4:
                            if (buf[i] == FLAG) counter = 5;
                            else counter = 0;
                            break;
                        
                        default:
                            break;
                    }
                    i++;
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
                // state machine
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



// ======================
// LLWRITE
// ======================

int llwrite(const unsigned char *buf, int bufSize)
{
    printf("-------llwrite started-------\n");
    printf("Sending trama  of type %d\n", !tramaType);

    /* // GENERATE BCC2
    unsigned char BCC2 = buf[0];
    for (int i = 1; i < bufSize; i++) {
        BCC2 ^= buf[i];
    }
 */

    //initializes BCC2 to 0
    unsigned char BCC2 = 0; 

    // bitwise XOR with the next byte in buf
    for (int i = 0; i < bufSize; i++) {
        BCC2 = BCC2 ^ buf[i]; 
    }

    // byte stuffing, we're finding out how many times the escape character is found!
    int counter = 0;
    for (int i = 0; i < bufSize; i++) {
        if (buf[i] == 0x7E || buf[i] == 0x7D) {
            counter++;
        }
    }


    // checks if the  bcc2 itself is a escape character
    if (BCC2 == 0x7E || BCC2 == 0x7D) counter++;

    // adds the extra bites needed for byte stuffing
    unsigned char buf_after_byte_stuffing[bufSize+counter];

    // replaces 0x7E and 0x7D
    for (int originalIndex = 0, afterIndex = 0; originalIndex < bufSize; originalIndex++) {
        if (buf[originalIndex] == 0x7E) {
            buf_after_byte_stuffing[afterIndex] = 0x7D;
            buf_after_byte_stuffing[afterIndex+1] = 0x5E;
            afterIndex+=2;
        }
        else if (buf[originalIndex] == 0x7D) {
            buf_after_byte_stuffing[afterIndex] = 0x7D;
            buf_after_byte_stuffing[afterIndex+1] = 0x5D;
            afterIndex+=2;
        }
        else {
            buf_after_byte_stuffing[afterIndex] = buf[originalIndex];
            afterIndex++;
        }
    }

    // creates the packet to be sent

    //6 extra bites are for htte control characters
    unsigned char packetSend[bufSize+counter+6];
    packetSend[0] = FLAG;
    packetSend[1] = A_SET;
    switch (tramaType){
        case TRUE:
            packetSend[2] = C_0;
            break;
        case FALSE:
            packetSend[2] = C_1;
            break;
        default:
            break;
    }
    //bcc
    packetSend[3] = packetSend[1] ^ packetSend[2];

    // add buf to the packet information
    for (int i = 4, j = 0; i < bufSize+counter+4; i++, j++) {
        packetSend[i] = buf_after_byte_stuffing[j];
    }

    // byte stuffing for bcc2
    if (BCC2 == 0x7E) {
        packetSend[bufSize+counter+3] = 0x7D;
        packetSend[bufSize+counter+4] = 0x5E;
        packetSend[bufSize+counter+5] = FLAG;
    }
    else if (BCC2 == 0x7D) {
        packetSend[bufSize+counter+3] = 0x7D;
        packetSend[bufSize+counter+4] = 0x5D;
        packetSend[bufSize+counter+5] = FLAG;
    }
    else {
        packetSend[bufSize+counter+4] = BCC2;
        packetSend[bufSize+counter+5] = FLAG;
    }

    // next trama to send
    tramaType ^= 1;

    // Set alarm function handler
    (void)signal(SIGALRM, alarmHandler);

    // sets the alarm count for the ammount of nRetransmissions
    while (alarmCount <= connection_parameters.nRetransmissions)
    {
        // sends the actual information packet
        int bytes = write(fd, packetSend, sizeof(packetSend));
        printf("Sent Information Packet with %d bytes being written\n", bytes);

        // sets alarm of 3 seconds
        if (alarmEnabled == FALSE)
        {
            alarm(connection_parameters.timeout); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }

        // reads the response
        unsigned char response[BUF_SIZE];
        int i = 0;
        int counter = 0;
        while (counter != 5)
        {
            int bytes = read(fd, response + i, 1);
            //wasn't able to read
            if (bytes == -1) break;
            if (bytes > 0) {
                // state machine
                switch (counter)
                {
                case 0:
                    if (response[i] == FLAG) counter = 1;
                    break;
                case 1:
                    if (response[i] == FLAG) counter = 1;
                    if (response[i] == A_UA) counter = 2;
                    else counter = 0;
                    break;
                case 2:
                    if (response[i] == FLAG) counter = 1;
                    if (response[i] == C_REJ) {
                        counter = 0;
                        printf("REJ received\n");
                        break;
                    }
                    if (tramaType == TRUE && response[i] == C_RR0) counter = 3;
                    else if (tramaType == FALSE && response[i] == C_RR1) counter = 3;
                    else {
                        // SEND PREVIOUS TRAMA
                        // SEND INFORMATION PACKET
                        int bytes = write(fd, previousTrama, sizeof(previousTrama));
                        printf("INFORMATION PACKET SENT - %d bytes written\n", bytes);
                        counter = 0;
                    }
                    break;
                case 3:
                    if (response[i] == FLAG) counter = 1;
                    if (response[i] == (response[i-1] ^ response[i-2])) counter = 4;
                    else counter = 0;
                    break;
                case 4:
                    if (response[i] == FLAG) counter = 5;
                    else counter = 0;
                    break;
                
                default:
                    break;
                }
                i++; 
            }
            // timeout
            if (alarmEnabled == FALSE) break;
        }
        
        if (counter == 5) {
            alarmCount = 0;
            printf("RR was received!\n");
            break;
        }
    }

    // saveTrama that was sent
    for (int i = 0; i < BUF_SIZE; i++) {
        previousTrama[i] = packetSend[i];
    }

    printf("-------llwrite finished-------\n");

    // timeout error
    if (alarmCount > connection_parameters.nRetransmissions) {
        printf("Timeout ammount exceded!");
        alarmCount = 0;
        return -1;
    }

    printf("llwrite was successful my g!");
    return sizeof(packetSend);
}






// ======================
//LLREAD
// ======================

int llread(unsigned char *packet)
{
    printf("-------llread started-------\n");
    printf("Receiving trama of type: %d!\n", !tramaType);

    unsigned char buf[BUF_SIZE];
    int sizeBuf = 0;
    int errorTramaType = FALSE;
    //to keep track of which buf to read
    int i = 0;
    //keeps track of state
    int counter = 0;
    while (counter != 5){

        int bytes = read(fd, buf + i, 1);

        if (bytes > 0) {
            //state machine
            switch (counter)
            {
            case 0:
                if (buf[i] == FLAG){
                    counter = 1;
                }
                break;
            case 1:
                if (buf[i] == A_SET) {
                    counter = 2;
                }
                else {
                    counter = 0;
                }
                break;
            case 2:
                if (buf[i] == FLAG) {
                    counter = 1;
                }
                if (tramaType == TRUE && buf[i] == C_0) {
                    counter = 3;
                }
                else if (tramaType == FALSE && buf[i] == C_1) {
                    counter = 3;
                }

                // wrong typpe of trauma
                else if (tramaType == TRUE && buf[i] == C_1) {
                    counter = 5;
                    errorTramaType = TRUE;
                }
                else if (tramaType == TRUE && buf[i] == C_0) {
                    counter = 5;
                    errorTramaType = TRUE;
                }

                else {
                    counter = 0;
                }
                break;
            case 3:
                if (buf[i] == FLAG) {
                    counter = 1;
                }
                if (buf[i] == (buf[i-1] ^ buf[i-2])) {
                    counter = 4;
                    i = -1;
                }
                else {
                    counter = 0;
                    printf("error in the bcc checking\n");
                }
                break;
            case 4:
                if (buf[i] == FLAG) {
                    counter = 5;
                }
                else {
                    sizeBuf++;
                }
                break;
            
            default:
                break;
            }
            i++; 
        }
    }

    // byte destuffing
    int size_of_packet = sizeBuf;
    for (int originalSize = 0, destuffedSize = 0; originalSize < sizeBuf; originalSize++, destuffedSize++) {
        if (buf[originalSize] == 0x7D && buf[originalSize+1] == 0x5E) {
            packet[destuffedSize] = 0x7E;
            originalSize++; size_of_packet--;
        }
        else if (buf[originalSize] == 0x7D && buf[originalSize+1] == 0x5D) {
            packet[destuffedSize] = 0x7D;
            originalSize++; size_of_packet--;
        }
        else {
            packet[destuffedSize] = buf[originalSize];
        }
    }

    // checks if bcc2 is working correctly
    size_of_packet--;
    unsigned char BCC2 = packet[0];
    for (int i = 1; i < size_of_packet; i++) {
        BCC2 = BCC2 ^ packet[i];
    }

    printf("Packet was received\n");

    // creates the RR message
    unsigned char rrMessage[5];
    rrMessage[0] = FLAG;
    rrMessage[1] = A_UA;

    // checks for bcc2 error
    if (BCC2 != packet[size_of_packet]) {
        printf("error bcc2\n");

        rrMessage[2] = C_REJ;
        rrMessage[3] = rrMessage[1] ^ rrMessage[2];
        rrMessage[4] = FLAG;
        int bytes = write(fd, rrMessage, 5);
        printf("REJ reject message was sent written with %d bytes\n", bytes);

        printf("-------llread finished-------\n");
        return -1;
    }


    /* if (tramaType == TRUE) {
        // same frame twice
        if (errorTramaType) {
            rrMessage[2] = C_RR0;
            printf("same frame twice\n");
        }
        else {
            rrMessage[2] = C_RR1;
            tramaType = FALSE;
        }
    }
    else {
        // same frame twice
        if (errorTramaType) {
            rrMessage[2] = C_RR1;
            printf("duplicate frame \n");
        }
        // NO ERROR
        else {
            rrMessage[2] = C_RR0;
            tramaType = TRUE;
        }
    } */

    if (errorTramaType) {
        if (tramaType == TRUE) {
            rrMessage[2] = C_RR0;
            printf("same frame twice\n");
        } else {
            rrMessage[2] = C_RR1;
            printf("duplicate frame\n");
        }
    } else {
        if (tramaType == TRUE) {
            rrMessage[2] = C_RR1;
            printf("duplicate frame\n");
        } else {
            rrMessage[2] = C_RR0;
            tramaType = TRUE;
        }
    }
    rrMessage[3] = rrMessage[1] ^ rrMessage[2];
    rrMessage[4] = FLAG;

    // sends the rr message
    int bytes = write(fd, rrMessage, 5);
    printf("Sending RR message, writes %d bytes\n", bytes);

    printf("-------llread finished-------\n");

    // same trama twice error
    if (errorTramaType) return -1;

    // works!
    return size_of_packet;
}

// ======================
// LLCLOSE
// ======================

int llclose(LinkLayer connectionParameters)
{
    printf("-------llclose started-------\n\n");


    // transmitter
    if (connectionParameters.role == LlTx){
        // CREATE DISC MESSAGE
        unsigned char disconnectMessage[BUF_SIZE];
        disconnectMessage[0] = FLAG;
        disconnectMessage[1] = A_SET;
        disconnectMessage[2] = C_DISC;
        disconnectMessage[3] = (A_SET ^ C_DISC);
        disconnectMessage[4] = FLAG;

        // Set alarm function handler
        (void)signal(SIGALRM, alarmHandler);

        // sets cutoff for ammount of timeouts
        while (alarmCount <= connectionParameters.nRetransmissions)
        {
            // sends disc message
            int bytes = write(fd, disconnectMessage, 5);
            printf("Sent DISC message,wrote %d bytes\n", bytes);

            // alarm triggering
            if (alarmEnabled == FALSE)
            {
                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
            }

            // READ DISC MESSAGE
            unsigned char buf[BUF_SIZE];
            //keep tracks of which párt of buf to read
            int i = 0;
            //keeps track of the state
            int counter = 0;
            while (counter != 5)
            {
                int bytes = read(fd, buf + i, 1);
                //printf("%hx %d\n", buf[i], bytes);
                if (bytes == -1) break;
                if (bytes > 0) {
                    // state machine
                    switch (counter){
                        case 0:
                            if (buf[i] == FLAG) {
                                counter = 1;
                            }
                            break;
                        case 1:
                            if (buf[i] == FLAG) {
                                counter = 1;
                                }
                            if (buf[i] == A_UA) {
                                counter = 2;
                            }
                            else {
                                counter = 0;
                            }
                            break;
                        case 2:
                            if (buf[i] == FLAG){ counter = 1;
                            }
                            else if (buf[i] == C_DISC) {
                                counter = 3;
                            }
                            else {
                                counter = 0;
                            }
                            break;
                        case 3:
                            if (buf[i] == FLAG) {
                                counter = 1;
                            }
                            if (buf[i] == (A_UA ^ C_DISC)) {
                                counter = 4;
                            }
                            else {
                                printf("error in the bcc!\n");
                                counter = 0;
                            }
                            break;
                        case 4:
                            if (buf[i] == FLAG) counter = 5;
                            else counter = 0;
                            break;
                        
                        default:
                            break;
                    }
                    i++; 
                }
                // timeout
                if (alarmEnabled == FALSE) {
                    break;
                }
            }
            
            // received the DISC message
            if (counter == 5) {
                alarmCount = 0;
                printf("DISC RECEIVED\n");
                break;
            }
        }

        // sends UA message
        unsigned char ua_message[BUF_SIZE];
        ua_message[0] = FLAG;
        ua_message[1] = A_UA;
        ua_message[2] = C_UA;
        ua_message[3] = BCC_UA;
        ua_message[4] = FLAG;

        int bytes = write(fd, ua_message, 5);
        printf("Sent UA message, wrote %d bytes\n", bytes);
    }


    // receiver
    else if(connectionParameters.role == LlRx){
        // reads the DISC message
        unsigned char buf[BUF_SIZE];
        //keep tracks of which párt of buf to read
        int i = 0;
        //keeps track of the state
        int counter = 0;
        while (counter != 5)
        {
            int bytes = read(fd, buf + i, 1);
            if (bytes > 0) {
                //state machine
                switch (counter){
                    case 0:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        break;
                    case 1:
                        if (buf[i] == A_SET) {
                            counter = 2;
                        }
                        else{
                         counter = 0;
                        }
                        break;
                    case 2:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        if (buf[i] == C_DISC) {
                            counter = 3;
                        }
                        else {
                            counter = 0;
                        }
                        break;
                    case 3:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        if (buf[i] == (A_SET ^ C_DISC)) {
                            counter = 4;
                        }
                        else {
                            printf("wrong bcc!\n");
                            counter = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) {
                            counter = 5;
                        }
                        else {
                            counter = 0;
                        }
                        break;
                    
                    default:
                        break;
                }
                i++; 
            }
        }
        printf("Received the DISC message \n");

        // sends the DISC message
        unsigned char disconnectMessage[BUF_SIZE];
        disconnectMessage[0] = FLAG;
        disconnectMessage[1] = A_UA;
        disconnectMessage[2] = C_DISC;
        disconnectMessage[3] = (A_UA ^ C_DISC);
        disconnectMessage[4] = FLAG;

        int bytes = write(fd, disconnectMessage, 5);
        printf("Sent DISC message,wrote %d bytes\n", bytes);

        // reads the UA message
        //keep tracks of which párt of buf to read
        i = 0;
        //keeps track of the state
        counter = 0;
        while (counter != 5)
        {
            int bytes = read(fd, buf + i, 1);
            //printf("%hx %d\n", buf[i], bytes);
            if (bytes == -1) break;
            if (bytes > 0) {
                // state machine
                switch (counter){
                    case 0:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        break;
                    case 1:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        if (buf[i] == A_UA) {
                            counter = 2;
                        }
                        else {
                            counter = 0;
                        }
                        break;
                    case 2:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        if (buf[i] == C_UA) {
                            counter = 3;
                        }
                        else {
                            counter = 0;
                        }
                        break;
                    case 3:
                        if (buf[i] == FLAG) {
                            counter = 1;
                        }
                        if (buf[i] == BCC_UA) {
                            counter = 4;
                        }
                        else {
                            printf("wrong bcc\n");
                            counter = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) {
                            counter = 5;
                        }
                        else {
                            counter = 0;
                        }
                        break;
                    default:
                        break;
                }
                i++; 
            }
        }
        printf("Received UA message\n");
    }
    else{
        printf("neither transmitter nor receiver!");
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
