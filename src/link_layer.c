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


int tramaTransmitter = 0;
int tramaReceiver = 1;

int STOP = FALSE;

int sendSmallFrame(int fd, unsigned char A, unsigned char C){
    unsigned char FRAME[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, FRAME, 5);
}

int llopen(LinkLayer connectionParameters) {

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

    LinkLayerState state = START;

    switch (connectionParameters.role) {

        case LlTx: {

            (void) signal(SIGALRM, alarmHandler);
            while (connectionParameters.nRetransmissions != 0 && state != EXIT) {
                //sends SET frame
                sendSmallFrame(fd, A_ER, C_SET);

                //start Alarm
                alarm(connectionParameters.timeout);
                alarmTriggered = FALSE;
                

                //STATE MACHINE
                while (state != EXIT && alarmTriggered == FALSE) {
                    int bytes = read(fd, &byte, 1) > 0;
                    if (bytes > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) {
                                    state = FLAG_RECEIVED;
                                }
                                break;
                            case FLAG_RECEIVED:
                                if (byte == A_RECEIVED) {
                                    state = A_RECEIVEDCEIVED;
                                }
                                else if (byte != FLAG) {
                                    state = START;
                                }
                                break;
                            case A_RECEIVEDCEIVED:
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
                                if (byte == (A_RECEIVED ^ C_UA)) {
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
                            if (byte == FLAG) state = FLAG_RECEIVED;
                            break;
                        case FLAG_RECEIVED:
                            if (byte == A_ER) state = A_RECEIVEDCEIVED;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RECEIVEDCEIVED:
                            if (byte == C_SET) state = C_RECEIVED;
                            else if (byte == FLAG) state = FLAG_RECEIVED;
                            else state = START;
                            break;
                        case C_RECEIVED:
                            if (byte == (A_ER ^ C_SET)) state = BCC1_OK;
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
            sendSmallFrame(fd, A_RECEIVED, C_UA);
            break; 
        }
    }
    return fd;
}



void alarmHandler(int signal) {
    alarmTriggered = TRUE;
    alarmCount++;
}

int llwrite(int fd, const unsigned char *buf, int bufSize) {

    int frameSize = 6+bufSize;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_ER;
    frame[2] = C_N(tramaTransmitter);
    frame[3] = frame[1] ^frame[2];
    memcpy(frame+4,buf, bufSize);
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1 ; i < bufSize ; i++) BCC2 ^= buf[i];

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
    int rejected = 0, accepted = 0;

    while (currentTransmition < retransmitions) { 
        alarmTriggered = FALSE;
        alarm(timeout);
        rejected = 0;
        accepted = 0;
        while (alarmTriggered == FALSE && !rejected && !accepted) {

            write(fd, frame, j);
            unsigned char result = readControlFrame(fd);
            
            if(!result){
                continue;
            }
            else if(result == C_REJ(0) || result == C_REJ(1)) {
                rejected = 1;
            }
            else if(result == C_RR(0) || result == C_RR(1)) {
                accepted = 1;
                tramaTransmitter = (tramaTransmitter+1) % 2;
            }
            else continue;

        }
        if (accepted) break;
        currentTransmition++;
    }
    
    free(frame);
    if(accepted) return frameSize;
    else{
        llclose(fd);
        return -1;
    }
}

int llread(int fd, unsigned char *packet) {

    unsigned char byte, cField;
    int i = 0;
    LinkLayerState state = START;

    while (state != EXIT) {  
        if (read(fd, &byte, 1) > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RECEIVED;
                    break;
                case FLAG_RECEIVED:
                    if (byte == A_ER) state = A_RECEIVEDCEIVED;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RECEIVEDCEIVED:
                    if (byte == C_N(0) || byte == C_N(1)){
                        state = C_RECEIVED;
                        cField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else if (byte == C_DISC) {
                        sendSmallFrame(fd, A_RECEIVED, C_DISC);
                        return 0;
                    }
                    else state = START;
                    break;
                case C_RECEIVED:
                    if (byte == (A_ER ^ cField)) state = PACKET;
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
                            sendSmallFrame(fd, A_RECEIVED, C_RR(tramaReceiver));
                            tramaReceiver = (tramaReceiver + 1)%2;
                            return i; 
                        }
                        else{
                            printf("Error: retransmition\n");
                            sendSmallFrame(fd, A_RECEIVED, C_REJ(tramaReceiver));
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

    LinkLayerState state = START;
    unsigned char byte;
    (void) signal(SIGALRM, alarmHandler);
    
    while (retransmitions != 0 && state != EXIT) {
                
        sendSmallFrame(fd, A_ER, C_DISC);
        alarm(timeout);
        alarmTriggered = FALSE;
                
        while (alarmTriggered == FALSE && state != EXIT) {
            if (read(fd, &byte, 1) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RECEIVED;
                        break;
                    case FLAG_RECEIVED:
                        if (byte == A_RECEIVED) state = A_RECEIVEDCEIVED;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RECEIVEDCEIVED:
                        if (byte == C_DISC) state = C_RECEIVED;
                        else if (byte == FLAG) state = FLAG_RECEIVED;
                        else state = START;
                        break;
                    case C_RECEIVED:
                        if (byte == (A_RECEIVED ^ C_DISC)) state = BCC1_OK;
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
    sendSmallFrame(fd, A_ER, C_UA);
    return close(fd);
}

unsigned char readControlFrame(int fd){

    unsigned char byte, cField = 0;
    LinkLayerState state = START;
    
    while (state != EXIT && alarmTriggered == FALSE) {  
        if (read(fd, &byte, 1) > 0 || 1) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RECEIVED;
                    break;
                case FLAG_RECEIVED:
                    if (byte == A_RECEIVED) state = A_RECEIVEDCEIVED;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RECEIVEDCEIVED:
                    if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1) || byte == C_DISC){
                        state = C_RECEIVED;
                        cField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else state = START;
                    break;
                case C_RECEIVED:
                    if (byte == (A_RECEIVED ^ cField)) state = BCC1_OK;
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else state = START;
                    break;
                case BCC1_OK:
                    if (byte == FLAG){
                        state = EXIT;
                    }
                    else state = START;
                    break;
                default: 
                    break;
            }
        } 
    } 
    return cField;
}
