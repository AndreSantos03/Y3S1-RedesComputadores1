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

#define BUF_SIZE 256

//flag
#define FLAG 0x7E
//used in commands sent by the transmitter and the replies sent by the receiver
#define A_SET 0x03
//used in commands sent by the receiver and the replies sent by the transmitter
#define A_UA 0x01   
#define CONTROL_SET 0x03 
#define CONTROL_UA 0x07
#define CONTROL_DIS 0x0B
#define CONTROL_0 0x00
#define CONTROL_1 0x40
#define CONTROL_RR0 0x05
#define CONTROL_RR1 0x85
#define CONTROL_REJ 0x01
#define BCC_CONTROL_SET (A_SET ^ CONTROL_SET)
#define BCC_CONTROL_UA (A_UA ^ A_UA)


int fd;
int alarmEnabled = FALSE;
int alarmCount = 0;
int tramaType = TRUE;
unsigned char previousTrama[BUF_SIZE];

LinkLayer connection_parameters; 


// Alarm function handler
void alarmHandler(int signal)
{
    printf("alarm enabled = %d \n",alarmEnabled);
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
    printf("\n-------BEGINNING OF LLOPEN-------\n\n");

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
    if(connectionParameters.role == Transmitter){

        //Create the Set Message
        unsigned char setMessage[BUF_SIZE];
        setMessage[0] = FLAG;
        setMessage[1] = A_SET;
        setMessage[2] = CONTROL_SET;
        setMessage[3] = BCC_CONTROL_SET;
        setMessage[4] = FLAG;

        // Set alarm function handler
        (void)signal(SIGALRM, alarmHandler);

        // while loop for the ammount of times it tries to send message
        while (alarmCount <= connectionParameters.nRetransmissions)
        {
            // sends the Set Messag
            int bytes = write(fd, setMessage, 5);
            printf("Sent Set Message with %d bytes being written\n", bytes);

            // alarm setter
            if (alarmEnabled == FALSE)
            {
                alarm(connectionParameters.timeout); // alarm trigger
                alarmEnabled = TRUE;
            }

            // reads back the Ua Message
            unsigned char buf[BUF_SIZE];

            //for tracking which bites to read
            int i = 0;
            //STATE for the state
            int STATE = 0;
            while (STATE != 5)
            {
                int bytes = read(fd, buf + i, 1);
                if (bytes == -1){
                    break;
                    }
                if (bytes > 0) {
                    //state machine
                    switch (STATE){
                        case 0:
                            if (buf[i] == FLAG) 
                            STATE = 1;
                            break;   
                        case 1:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == A_UA) STATE = 2;
                            else STATE = 0;
                            break;
                        case 2:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == CONTROL_UA) STATE = 3;
                            else STATE = 0;
                            break;
                        case 3:
                            if (buf[i] == FLAG) STATE = 1;
                            if (buf[i] == BCC_CONTROL_UA) STATE = 4;
                            else {
                                printf("BCC doesn't match\n");
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
                if (alarmEnabled == FALSE) {
                    break;
                }
            }
            

            // received ua message
            if (STATE == 5) {
                printf("Ua Message was received\n");
                alarmCount = 0;
                break;
            }
        }
    }

    //receiver
    else if(connectionParameters.role == Receiver){
        //reads the Set Message
        unsigned char buf[BUF_SIZE];
        int i = 0;
        int STATE = 0;
        while (STATE != 5)
        {
            int bytes = read(fd, buf + i,1);
            if (bytes > 0) {
                printf("we're here");
                // state machine
                switch (STATE){
                    case 0:
                        if (buf[STATE] == FLAG) STATE = 1;
                        break;
                    case 1:
                        if (buf[STATE] == A_SET) STATE = 2;
                        else STATE = 0;
                        break;
                    case 2:
                        if (buf[STATE] == FLAG) STATE = 1;
                        if (buf[STATE] == CONTROL_SET) STATE = 3;
                        else STATE = 0;
                        break;
                    case 3:
                        if (buf[STATE] == FLAG) STATE = 1;
                        if (buf[STATE] == BCC_CONTROL_SET) STATE = 4;
                        else {
                            printf("Error in the protocol\n");
                            STATE = 0;
                        }
                        break;
                    case 4:
                        if (buf[STATE] == FLAG) STATE = 5;
                        else STATE = 0;
                        break;
                    
                    default:
                        break;
                }
                i++;
            }
        }
        printf("SET RECEIVED\n");

        // Screates the Ua Message
        unsigned char ua_message[BUF_SIZE];
        ua_message[0] = FLAG;
        ua_message[1] = A_UA;
        ua_message[2] = CONTROL_UA;
        ua_message[3] = BCC_CONTROL_UA;
        ua_message[4] = FLAG;

        //sends the Ua Message
        int bytes = write(fd, ua_message, 5);
        printf("Sent UA Message with %d bytes being written\n", bytes);
    }
    printf("\n-------END OF LLOPEN-------\n\n");

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
    printf("\n-------BEGINNING OF LLWRITE------\n\n");
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
            packetSend[2] = CONTROL_0;
            break;
        case FALSE:
            packetSend[2] = CONTROL_1;
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
        printf("Information Packet Sent: %d bytes being written\n", bytes);

        // sets alarm of 3 seconds
        if (alarmEnabled == FALSE)
        {
            alarm(connection_parameters.timeout); // Set alarm to be triggered in 3s
            alarmEnabled = TRUE;
        }

        // reads the response
        unsigned char response[BUF_SIZE];
        int i = 0;
        int STATE = 0;
        while (STATE != 5)
        {
            int bytes = read(fd, response + i, 1);
            //wasn't able to read
            if (bytes == -1) break;
            if (bytes > 0) {
                // state machine
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
                    if (response[i] == CONTROL_REJ) {
                        STATE = 0;
                        printf("REJ received\n");
                        break;
                    }
                    if (tramaType == TRUE && response[i] == CONTROL_RR0) STATE = 3;
                    else if (tramaType == FALSE && response[i] == CONTROL_RR1) STATE = 3;
                    else {
                        // SEND PREVIOUS TRAMA
                        // SEND INFORMATION PACKET
                        int bytes = write(fd, previousTrama, sizeof(previousTrama));
                        printf("Information Packet Sent:  %d bytes written\n", bytes);
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
        
        if (STATE == 5) {
            alarmCount = 0;
            printf("RR received! All good!\n");
            break;
        }
    }

    // saveTrama that was sent
    for (int i = 0; i < BUF_SIZE; i++) {
        previousTrama[i] = packetSend[i];
    }

    printf("\n-------END OF LLWRITE-------\n\n");

    // timeout error
    if (alarmCount > connection_parameters.nRetransmissions) {
        printf("Timeout ammount exceded!");
        alarmCount = 0;
        return -1;
    }

    return sizeof(packetSend);
}






// ======================
//LLREAD
// ======================

int llread(unsigned char *packet)
{
    printf("\n-------BEGINNING OF LLREAD-------\n\n");
    printf("Receiving trama of type: %d!\n", !tramaType);

    unsigned char buf[BUF_SIZE];
    int sizeBuf = 0;
    int errorTramaType = FALSE;
    //to keep track of which buf to read
    int i = 0;
    //keeps track of state
    int STATE = 0;
    while (STATE != 5){

        int bytes = read(fd, buf + i, 1);

        if (bytes > 0) {
            //state machine
            switch (STATE)
            {
            case 0:
                if (buf[i] == FLAG){
                    STATE = 1;
                }
                break;
            case 1:
                if (buf[i] == A_SET) {
                    STATE = 2;
                }
                else {
                    STATE = 0;
                }
                break;
            case 2:
                if (buf[i] == FLAG) {
                    STATE = 1;
                }
                if (tramaType == TRUE && buf[i] == CONTROL_0) {
                    STATE = 3;
                }
                else if (tramaType == FALSE && buf[i] == CONTROL_1) {
                    STATE = 3;
                }

                // wrong typpe of trauma
                else if (tramaType == TRUE && buf[i] == CONTROL_1) {
                    STATE = 5;
                    errorTramaType = TRUE;
                }
                else if (tramaType == TRUE && buf[i] == CONTROL_0) {
                    STATE = 5;
                    errorTramaType = TRUE;
                }

                else {
                    STATE = 0;
                }
                break;
            case 3:
                if (buf[i] == FLAG) {
                    STATE = 1;
                }
                else if (buf[i] == (buf[i-1] ^ buf[i-2])) {
                    STATE = 4;
                    i = -1;
                }
                else {
                    STATE = 0;
                    printf("error in the bcc checking\n");
                }
                break;
            case 4:
                if (buf[i] == FLAG) {
                    STATE = 5;
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

    printf("Packet was received!\n");

    // creates the RR message
    unsigned char rrMessage[5];
    rrMessage[0] = FLAG;
    rrMessage[1] = A_UA;

    // checks for bcc2 error
    if (BCC2 != packet[size_of_packet]) {
        printf("error bcc2\n");

        rrMessage[2] = CONTROL_REJ;
        rrMessage[3] = rrMessage[1] ^ rrMessage[2];
        rrMessage[4] = FLAG;
        int bytes = write(fd, rrMessage, 5);
        printf("REJ reject message was sent written with %d bytes\n", bytes);

        printf("\n-------END OF LLREAD-------\n\n");
        return -1;
    }


    if (tramaType == TRUE) {
        // same frame twice
        if (errorTramaType) {
            rrMessage[2] = CONTROL_RR0;
            printf("Same frame twice\n");
        }
        else {
            rrMessage[2] = CONTROL_RR1;
            tramaType = FALSE;
        }
    }
    else {
        // same frame twice
        if (errorTramaType) {
            rrMessage[2] = CONTROL_RR1;
            printf("Duplicate frame \n");
        }
        // NO ERROR
        else {
            rrMessage[2] = CONTROL_RR0;
            tramaType = TRUE;
        }
    }


    rrMessage[3] = rrMessage[1] ^ rrMessage[2];
    rrMessage[4] = FLAG;

    // sends the rr message
    int bytes = write(fd, rrMessage, 5);
    printf("Sending RR message, everything is good, %d bytes written \n", bytes);

    printf("\n-------END OF LLREAD-------\n\n");

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
    printf("\n-------BEGINNING OF LLCLOSE-------\n\n");


    // transmitter
    if (connectionParameters.role == Transmitter){
        // CREATE DISC MESSAGE
        unsigned char disconnectMessage[BUF_SIZE];
        disconnectMessage[0] = FLAG;
        disconnectMessage[1] = A_SET;
        disconnectMessage[2] = CONTROL_DIS;
        disconnectMessage[3] = (A_SET ^ CONTROL_DIS);
        disconnectMessage[4] = FLAG;

        // Set alarm function handler
        (void)signal(SIGALRM, alarmHandler);

        // sets cutoff for ammount of timeouts
        while (alarmCount <= connectionParameters.nRetransmissions)
        {
            // sends disc message
            int bytes = write(fd, disconnectMessage, 5);
            printf("Sent disconnect message, %d bytes written\n", bytes);

            // alarm triggering
            if (alarmEnabled == FALSE)
            {
                alarm(connectionParameters.timeout);
                alarmEnabled = TRUE;
            }

            // READ DISC MESSAGE
            unsigned char buf[BUF_SIZE];
            //keep tracks of which part of buf to read
            int i = 0;
            //keeps track of the state
            int STATE = 0;
            while (STATE != 5)
            {
                int bytes = read(fd, buf + i, 1);
                //printf("%hx %d\n", buf[i], bytes);
                if (bytes == -1) break;
                if (bytes > 0) {
                    // state machine
                    switch (STATE){
                        case 0:
                            if (buf[i] == FLAG) {
                                STATE = 1;
                            }
                            break;
                        case 1:
                            if (buf[i] == FLAG) {
                                STATE = 1;
                                }
                            if (buf[i] == A_UA) {
                                STATE = 2;
                            }
                            else {
                                STATE = 0;
                            }
                            break;
                        case 2:
                            if (buf[i] == FLAG){ STATE = 1;
                            }
                            else if (buf[i] == CONTROL_DIS) {
                                STATE = 3;
                            }
                            else {
                                STATE = 0;
                            }
                            break;
                        case 3:
                            if (buf[i] == FLAG) {
                                STATE = 1;
                            }
                            if (buf[i] == (A_UA ^ CONTROL_DIS)) {
                                STATE = 4;
                            }
                            else {
                                printf("Error in the bcc!\n");
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
                if (alarmEnabled == FALSE) {
                    break;
                }
            }
            
            // received the DISC message
            if (STATE == 5) {
                alarmCount = 0;
                printf("Disconnect received\n");
                break;
            }
        }

        // sends UA message
        unsigned char ua_message[BUF_SIZE];
        ua_message[0] = FLAG;
        ua_message[1] = A_UA;
        ua_message[2] = CONTROL_UA;
        ua_message[3] = BCC_CONTROL_UA;
        ua_message[4] = FLAG;

        int bytes = write(fd, ua_message, 5);
        printf("Sent UA message, %d bytes written\n", bytes);
    }


    // receiver
    else if(connectionParameters.role == Receiver){
        // reads the DISC message
        unsigned char buf[BUF_SIZE];
        //keep tracks of which párt of buf to read
        int i = 0;
        //keeps track of the state
        int STATE = 0;
        while (STATE != 5)
        {
            int bytes = read(fd, buf + i, 1);
            if (bytes > 0) {
                //state machine
                switch (STATE){
                    case 0:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        break;
                    case 1:
                        if (buf[i] == A_SET) {
                            STATE = 2;
                        }
                        else{
                         STATE = 0;
                        }
                        break;
                    case 2:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        if (buf[i] == CONTROL_DIS) {
                            STATE = 3;
                        }
                        else {
                            STATE = 0;
                        }
                        break;
                    case 3:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        if (buf[i] == (A_SET ^ CONTROL_DIS)) {
                            STATE = 4;
                        }
                        else {
                            printf("Wrong bcc!\n");
                            STATE = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) {
                            STATE = 5;
                        }
                        else {
                            STATE = 0;
                        }
                        break;
                    
                    default:
                        break;
                }
                i++; 
            }
        }
        printf("Received the disconnect message \n");

        // sends the DISC message
        unsigned char disconnectMessage[BUF_SIZE];
        disconnectMessage[0] = FLAG;
        disconnectMessage[1] = A_UA;
        disconnectMessage[2] = CONTROL_DIS;
        disconnectMessage[3] = (A_UA ^ CONTROL_DIS);
        disconnectMessage[4] = FLAG;

        int bytes = write(fd, disconnectMessage, 5);
        printf("Sent disconnect message, %d bytes written\n", bytes);

        // reads the UA message
        //keep tracks of which párt of buf to read
        i = 0;
        //keeps track of the state
        STATE = 0;
        while (STATE != 5)
        {
            int bytes = read(fd, buf + i, 1);
            //printf("%hx %d\n", buf[i], bytes);
            if (bytes == -1) break;
            if (bytes > 0) {
                // state machine
                switch (STATE){
                    case 0:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        break;
                    case 1:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        if (buf[i] == A_UA) {
                            STATE = 2;
                        }
                        else {
                            STATE = 0;
                        }
                        break;
                    case 2:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        if (buf[i] == CONTROL_UA) {
                            STATE = 3;
                        }
                        else {
                            STATE = 0;
                        }
                        break;
                    case 3:
                        if (buf[i] == FLAG) {
                            STATE = 1;
                        }
                        if (buf[i] == BCC_CONTROL_UA) {
                            STATE = 4;
                        }
                        else {
                            printf("Wrong bcc\n");
                            STATE = 0;
                        }
                        break;
                    case 4:
                        if (buf[i] == FLAG) {
                            STATE = 5;
                        }
                        else {
                            STATE = 0;
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
        printf("Neither transmitter nor receiver!");
    }

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    printf("\n-------END OF LLCLOSE-------\n\n");

    return 1;
}
