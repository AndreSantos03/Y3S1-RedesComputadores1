// Link layer protocol implementation

#include "link_layer.h"

volatile int STOP = FALSE;
int fd = 0;
int alarmEnabled = FALSE;
int alarmCount = 0;
int timeout = 0;
int retransmissions = 0;
unsigned char tramaTx = 0;
unsigned char tramaRx = 1;
clock_t start_time;

void alarmHandler(int signal) {
    alarmEnabled = TRUE;
    alarmCount++;
	printf("Alarm #%d\n", alarmCount);
}

int connection(const char *serialPort) {

    int fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(serialPort);
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

    return fd;
}

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters) {
    
    llState state = START;
    fd = connection(connectionParameters.serialPort);
    if (fd < 0) return -1;

    unsigned char byte;
    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;
    switch (connectionParameters.role) {

        case LlTx: {
			start_time = clock();
            (void) signal(SIGALRM, alarmHandler);
            while (connectionParameters.nRetransmissions != 0 && state != STOP_RCV) {
                
                if(sendFrame(A_TX, C_SET) < 0){printf("Send Frame Error\n"); return -1;}
                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;
                
                while (alarmEnabled == FALSE && state != STOP_RCV) {
                    if (read(fd, &byte, 1) > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) state = FLAG_RCV;
                                break;
                            case FLAG_RCV:
                                if (byte == A_RX) state = A_RCV;
                                else if (byte != FLAG) state = START;
                                break;
                            case A_RCV:
                                if (byte == C_UA) state = C_RCV;
                                else if (byte == FLAG) state = FLAG_RCV;
                                else state = START;
                                break;
                            case C_RCV:
                                if (byte == (A_RX ^ C_UA)) state = BCC1_CHECK;
                                else if (byte == FLAG) state = FLAG_RCV;
                                else state = START;
                                break;
                            case BCC1_CHECK:
                                if (byte == FLAG) state = STOP_RCV;
                                else state = START;
                                break;
                            default: 
                                break;
                        }
                    }
                } 
                connectionParameters.nRetransmissions--;
            }
            if (state != STOP_RCV) return -1;
            break;  
        }

        case LlRx: {

            while (state != STOP_RCV) {
                if (read(fd, &byte, 1) > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RCV;
                            break;
                        case FLAG_RCV:
                            if (byte == A_TX) state = A_RCV;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RCV:
                            if (byte == C_SET) state = C_RCV;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case C_RCV:
                            if (byte == (A_TX ^ C_SET)) state = BCC1_CHECK;
                            else if (byte == FLAG) state = FLAG_RCV;
                            else state = START;
                            break;
                        case BCC1_CHECK:
                            if (byte == FLAG) state = STOP_RCV;
                            else state = START;
                            break;
                        default: 
                            break;
                    }
                }
            }  
            if(sendFrame(A_RX, C_UA) < 0){printf("Send Frame Error\n"); return -1;}
            break; 
        }
        default:
            return -1;
            break;
    }
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize) {

    int frameSize = 6 + bufSize;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = C_N(tramaTx);
    frame[3] = (frame[1] ^ frame[2]);
    memcpy(frame + 4, buf, bufSize);

    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1 ; i < bufSize ; i++) BCC2 ^= buf[i];

    //Byte stuffing
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

    while (currentTransmition < retransmissions) { 
        alarmEnabled = FALSE;
        alarm(timeout);
        rejected = 0;
        accepted = 0;
        while (alarmEnabled == FALSE && !rejected && !accepted) {

            if(write(fd, frame, j) < 0) return -1;
            unsigned char controlField = readSupervisionFrame();
            
            if(!controlField){
                continue;
            }
            else if(controlField == C_REJ(0) || controlField == C_REJ(1)) {
                rejected = 1;
            }
            else if(controlField == C_RR(0) || controlField == C_RR(1)) {
                accepted = 1;
                tramaTx = (tramaTx + 1) % 2;  //Nr module-2 counter (enables to distinguish frame 0 and frame 1)
            }
            else continue;

        }
        if (accepted) break;
        currentTransmition++;
    }
    
    free(frame);
    if(accepted) return frameSize;
    else{
        llclose(1);
        return -1;
    }
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet) {

    unsigned char byte, ctrlField;
    int i = 0;
    llState state = START;

    while (state != STOP_RCV) {  
        if (read(fd, &byte, 1) > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_TX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_N(0) || byte == C_N(1)){
                        state = C_RCV;
                        ctrlField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else if (byte == C_DISC) {
                        if(sendFrame(A_RX, C_DISC) < 0){printf("Send Frame Error\n"); return -1;}
                        return 0;
                    }
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (A_TX ^ ctrlField)) state = READING_DATA;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case READING_DATA:
                    if (byte == ESC) state = DATA_FOUND;
                    else if (byte == FLAG){
                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char bcc2Check = packet[0];

                        for (unsigned int j = 1; j < i; j++)
                            bcc2Check ^= packet[j];

                        if (bcc2 == bcc2Check){
                            state = STOP_RCV;
                            if(sendFrame(A_RX, C_RR(tramaRx)) < 0){printf("Send Frame Error\n");}
                            tramaRx = (tramaRx + 1) % 2; //Ns module-2 counter (enables to distinguish frame 0 and frame 1)
                            return i; 
                        }
                        else{
                            printf("Retransmission Error\n");
                            sendFrame(A_RX, C_REJ(tramaRx));
                            return -1;
                        };

                    }
                    else{
                        packet[i++] = byte;
                    }
                    break;
                case DATA_FOUND:
                    state = READING_DATA;
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

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics) {

    llState state = START;
    unsigned char byte;
    (void) signal(SIGALRM, alarmHandler);
    
    while (retransmissions != 0 && state != STOP_RCV) {
                
        if(sendFrame(A_TX, C_DISC) < 0){printf("Send Frame Error\n"); return -1;}
        alarm(timeout);
        alarmEnabled = FALSE;
                
        while (alarmEnabled == FALSE && state != STOP_RCV) {
            if (read(fd, &byte, 1) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (byte == A_RX) state = A_RCV;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RCV:
                        if (byte == C_DISC) state = C_RCV;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case C_RCV:
                        if (byte == (A_RX ^ C_DISC)) state = BCC1_CHECK;
                        else if (byte == FLAG) state = FLAG_RCV;
                        else state = START;
                        break;
                    case BCC1_CHECK:
                        if (byte == FLAG) state = STOP_RCV;
                        else state = START;
                        break;
                    default: 
                        break;
                }
            }
        } 
        retransmissions--;
    }

    if (state != STOP_RCV) return -1;
    if(sendFrame(A_TX, C_UA) < 0) return -1;
	if (showStatistics == 1) {
		clock_t end_time = clock();

		double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
		printf("Elapsed time: %f seconds\n", elapsed_time);
	}
    return close(fd);
}

unsigned char readSupervisionFrame() {

    unsigned char byte, ctrlField = 0;
    llState state = START;
    
    while (state != STOP_RCV && alarmEnabled == FALSE) {  
        if (read(fd, &byte, 1) > 0 || 1) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byte == A_RX) state = A_RCV;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RCV:
                    if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1) || byte == C_DISC){
                        state = C_RCV;
                        ctrlField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case C_RCV:
                    if (byte == (A_RX ^ ctrlField)) state = BCC1_CHECK;
                    else if (byte == FLAG) state = FLAG_RCV;
                    else state = START;
                    break;
                case BCC1_CHECK:
                    if (byte == FLAG){
                        state = STOP_RCV;
                    }
                    else state = START;
                    break;
                default: 
                    break;
            }
        } 
    } 
    return ctrlField;
}

// Send Supervision Frame
int sendFrame(unsigned char A, unsigned char C) {
    unsigned char buffer[5] = {FLAG, A, C, A ^ C, FLAG};
    return write(fd, buffer, 5);
}