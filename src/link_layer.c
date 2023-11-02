// Link layer protocol implementation

#include "link_layer.h"


#define _POSIX_SOURCE 1
#define BAUDRATE 38400

#define BUF_SIZE 256
#define FALSE 0
#define TRUE 1

#define FLAG 0x7E
#define ESC 0x7D
#define A_SET 0x03
#define A_UA 0x01
#define C_SET 0x03
#define C_DISC 0x0B
#define C_UA 0x07
#define C_RR(Nr) ((Nr << 7) | 0x05) //sets based on who's receiving or sending
#define C_REJ(Nr) ((Nr << 7) | 0x01)
#define C_N(Ns) (Ns << 6) 

int alarmTriggered = FALSE;
int alarmCount = 0;
int timeout = 0;
int retransmitions = 0;


void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
}


int tramaTransmitter = 0;
int tramaReceiver = 1;

int STOP = FALSE;

int controlFrame(int fd, unsigned char A, unsigned char C){
    unsigned char FRAME[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, FRAME, 5);
}

int llopen(connectionParameters connectionParameters) {

    int fd = open(connectionParameters.serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(connectionParameters.serialPort);
        return -1; 
    }

    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1)
    {
        perror("tcgetattr");
        exit(-1);
    }

    memset(&newtio, 0, sizeof(newtio));

    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME] = 0;
    newtio.c_cc[VMIN] = 0;

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &newtio) == -1) {
        perror("tcsetattr");
        return -1;
    }

    
    if (fd < 0) {
        printf("Couldnt't establish connection!\n");
        return -1;
    }

    unsigned char byte;
    timeout = connectionParameters.timeout;
    retransmitions = connectionParameters.nRetransmissions;

    connectionParametersState state = START;

    switch (connectionParameters.role) {

        case LlTx: {

            (void) signal(SIGALRM, alarmHandler);
            while (connectionParameters.nRetransmissions != 0 && state != EXIT) {
                //sends SET frame
                controlFrame(fd, A_SET, C_SET);

                //start Alarm
                alarm(connectionParameters.timeout);
                alarmTriggered = FALSE;
                

                //STATE MACHINE
                while (state != EXIT && alarmTriggered == FALSE) {
                    int bytes = read(fd, &byte, 1);
                    if (bytes > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RECEIVED;
                                }
                                break;
                            case FLAG_RECEIVED:
                                if (byte == A_SET) {
                                    state = A_SETCEIVED;
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                }
                                break;
                            case A_SETCEIVED:
                                if (byte == C_UA) {
                                    state = C_RECEIVED;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RECEIVED;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            case C_RECEIVED:
                                if (byte == (A_SET ^ C_UA)) {
                                    state = BCC1_OK;
                                }
                                else if (byte == FLAG) {
                                    state = FLAG_RECEIVED;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            case BCC1_OK:
                                if (byte == FLAG) {
                                    state = EXIT;
                                }
                                else {
                                    state = START;
                                }
                                break;
                            default: 
                                break;
                        }
                    }
                } 
                connectionParameters.nRetransmissions--;
            }
            if (state != EXIT) return -1;
            break;  
        }

        case LlRx: {
            //doesnt't need alarm
            //STATE MACHINE
            while (state != EXIT) {
                int bytes = read(fd, &byte, 1);
                if (bytes > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG){
                                state = FLAG_RECEIVED;
                            }
                            break;
                        case FLAG_RECEIVED:
                            if (byte == A_SET) {
                                state = A_SETCEIVED;
                            }
                            else if (byte != FLAG) {
                                state = START;
                            }
                            break;
                        case A_SETCEIVED:
                            if (byte == C_SET) {
                                state = C_RECEIVED;
                            }
                            else if (byte == FLAG){ 
                                state = FLAG_RECEIVED;
                            }
                            else state = START;
                            break;
                        case C_RECEIVED:
                            if (byte == (A_SET ^ C_SET)) {
                                state = BCC1_OK;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RECEIVED;
                            }
                            else {
                                state = START;
                            }
                            break;
                        case BCC1_OK:
                            if (byte == FLAG) state = EXIT;
                            else {
                                state = START;
                            }
                            break;
                        default: 
                            break;
                    }
                }
            }  
            controlFrame(fd, A_SET, C_UA);
            break; 
        }
    }
    return fd;
}



int llwrite(int fd, const unsigned char *buf, int bufSize) {
    //creates frame
    int frameSize = 6+bufSize;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_SET;
    frame[2] = C_N(tramaTransmitter);
    frame[3] = frame[1] ^frame[2];
    memcpy(frame+4,buf, bufSize);


    // GENERATE BCC2
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1 ; i < bufSize ; i++){

     BCC2 ^= buf[i];
    }


     // BYTE STUFFING 
    int j = 4;
    for (unsigned int i = 0 ; i < bufSize ; i++) {
        if(buf[i] == FLAG || buf[i] == ESC) {
            frame = realloc(frame,++frameSize);
            frame[j++] = ESC;
        }
        frame[j++] = buf[i];
    }
    frame[j++] = BCC2;
    frame[j++] = FLAG;

    int currentTransmition = 0;

    while (currentTransmition < retransmitions) { 
        int rejReceived = 0;
        int responseAccepted = 0;
        alarmTriggered = FALSE;
        alarm(timeout);
        
        while (alarmTriggered == FALSE && !rejReceived && !responseAccepted) {

            write(fd, frame, j);

            unsigned char byte, cField = 0;

            connectionParametersState state = START;

            //STATE MACHINE
            while (state != EXIT && alarmTriggered == FALSE) {  
                if (read(fd, &byte, 1) > 0 || 1) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RECEIVED;
                            break;
                        case FLAG_RECEIVED:
                            if (byte == A_SET) state = A_SETCEIVED;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_SETCEIVED:
                            if(byte == C_REJ(0) || byte == C_REJ(1)){
                                //RECEIVED REJECT MESSAGE
                                rejReceived = 1;
                                state = C_RECEIVED;
                            }
                            else if(byte == C_RR(0) || byte == C_RR(1)){
                                responseAccepted = 1;
                                tramaTransmitter = (tramaTransmitter+1) % 2;
                            }
                            else if (byte == FLAG) {
                                state = FLAG_RECEIVED;
                            }
                            else state = START;
                            break;
                        case C_RECEIVED:
                            if (byte == (A_SET ^ cField)) {
                                state = BCC1_OK;
                            }
                            else if (byte == FLAG){
                                 state = FLAG_RECEIVED;
                            }
                            else {
                                state = START;
                            }
                            break;
                        case BCC1_OK:
                            if (byte == FLAG){
                                state = EXIT;
                            }
                            else {
                                state = START;
                            }
                            break;
                        default: 
                            break;
                        if(responseAccepted){
                            free(frame);
                            break;
                            return frameSize;
                        }
                    }
                } 
            } 
            currentTransmition++;
        }
    }

    llclose(fd);
    return -1;
}

int llread(int fd, unsigned char *packet) {

    unsigned char byte, cField;
    int i = 0;
    connectionParametersState state = START;

    while (state != EXIT) {  
        if (read(fd, &byte, 1) > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RECEIVED;
                    break;
                case FLAG_RECEIVED:
                    if (byte == A_SET) state = A_SETCEIVED;
                    else if (byte != FLAG) state = START;
                    break;
                case A_SETCEIVED:
                    if (byte == C_N(0) || byte == C_N(1)){
                        state = C_RECEIVED;
                        cField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else if (byte == C_DISC) {
                        controlFrame(fd, A_SET, C_DISC);
                        return 0;
                    }
                    else state = START;
                    break;
                case C_RECEIVED:
                    if (byte == (A_SET ^ cField)) state = PACKET;
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else state = START;
                    break;
                case PACKET:
                    if (byte == ESC) state = ESCAPE_FOUND;
                    else if (byte == FLAG){
                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char acc = packet[0];

                        for (unsigned int j = 1; j < i; j++)
                            acc ^= packet[j];

                        if (bcc2 == acc){
                            state = EXIT;
                            controlFrame(fd, A_SET, C_RR(tramaReceiver));
                            tramaReceiver = (tramaReceiver + 1)%2;
                            return i; 
                        }
                        else{
                            printf("Error: retransmition\n");
                            controlFrame(fd, A_SET, C_REJ(tramaReceiver));
                            return -1;
                        };

                    }
                    else{
                        packet[i++] = byte;
                    }
                    break;
                case ESCAPE_FOUND:
                    state = PACKET;
                    if (byte == ESC || byte == FLAG) packet[i++] = byte;
                    else{
                        packet[i++] = ESC;
                        packet[i++] = byte;
                    }
                    break;
                default: 
                    break;
            }
        }
    }
    return -1;
}

int llclose(int fd){

    connectionParametersState state = START;
    unsigned char byte;
    (void) signal(SIGALRM, alarmHandler);
    
    while (retransmitions != 0 && state != EXIT) {
                
        controlFrame(fd, A_SET, C_DISC);
        alarm(timeout);
        alarmTriggered = FALSE;
                
        while (alarmTriggered == FALSE && state != EXIT) {
            if (read(fd, &byte, 1) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RECEIVED;
                        break;
                    case FLAG_RECEIVED:
                        if (byte == A_SET) state = A_SETCEIVED;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_SETCEIVED:
                        if (byte == C_DISC) state = C_RECEIVED;
                        else if (byte == FLAG) state = FLAG_RECEIVED;
                        else state = START;
                        break;
                    case C_RECEIVED:
                        if (byte == (A_SET ^ C_DISC)) state = BCC1_OK;
                        else if (byte == FLAG) state = FLAG_RECEIVED;
                        else state = START;
                        break;
                    case BCC1_OK:
                        if (byte == FLAG) state = EXIT;
                        else state = START;
                        break;
                    default: 
                        break;
                }
            }
        } 
        retransmitions--;
    }

    if (state != EXIT) return -1;
    controlFrame(fd, A_SET, C_UA);
    return close(fd);
}
