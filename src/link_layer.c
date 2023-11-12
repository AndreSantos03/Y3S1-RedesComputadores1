// Link layer protocol implementation

#include "link_layer.h"

// Global variables to manage the state and parameters of the link layer.
volatile int STOP = FALSE;            // Flag to control program execution
int fd = 0;                            // File descriptor for the serial port
int alarmEnabled = FALSE;              // Flag to indicate if an alarm is active
int alarmCount = 0;                    // Counter for the number of alarms triggered
int timeout = 0;                       // Timeout value for communication
int retransmissions = 0;               // Maximum number of retransmissions allowed
unsigned char tramaTx = 0;             // Counter for transmitted frames
unsigned char tramaRx = 1;             // Counter for received frames
clock_t start_time;                     // Start time for measuring elapsed time

// Function to handle the alarm signal.
void alarmHandler(int signal) {
    alarmEnabled = TRUE;
    alarmCount++;
    printf("Alarm #%d\n", alarmCount);
}

// Function to establish a connection on the specified serial port.
// Returns the file descriptor on success or -1 on error.
int connection(const char *serialPort) {

    // Open the serial port
    int fd = open(serialPort, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(serialPort);
        return -1; 
    }

    // Configure the serial port settings
    struct termios oldtio;
    struct termios newtio;

    if (tcgetattr(fd, &oldtio) == -1) {
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
// Function to establish a connection using the specified link layer parameters.
// Returns the file descriptor on success or -1 on error.
int llopen(LinkLayer connectionParameters) {
    
    // Initialize link layer state and open the serial port
    llState state = START;
    fd = connection(connectionParameters.serialPort);
    if (fd < 0) return -1;

    unsigned char byte;
    timeout = connectionParameters.timeout;
    retransmissions = connectionParameters.nRetransmissions;
    
    // Switch based on the role (transmitter or receiver)
    switch (connectionParameters.role) {

        case transmitter: {
			// Record the start time for elapsed time calculation
			start_time = clock();
            
            // Set up the alarm signal handler
            (void) signal(SIGALRM, alarmHandler);

            // Loop until either successful communication or maximum retransmissions reached
            while (connectionParameters.nRetransmissions != 0 && state != STOP_RECEIVED) {
                
                 // Construct and send the SET frame
                unsigned char setFrame[5] = {FLAG, A_TX, C_SET, A_TX ^ C_SET, FLAG};
                // Send the SET frame
                if(write(fd, setFrame, 5) < 0){
                    printf("Send Frame Error\n");
                    return -1;
                }
                
                // Set the alarm and reset alarm flag
                alarm(connectionParameters.timeout);
                alarmEnabled = FALSE;
                
                // Inner loop for receiving frames and transitioning through states
                while (alarmEnabled == FALSE && state != STOP_RECEIVED) {
                    if (read(fd, &byte, 1) > 0) {
                        switch (state) {
                            case START:
                                if (byte == FLAG) state = FLAG_RECEIVED;
                                break;
                            case FLAG_RECEIVED:
                                if (byte == A_RX) state = A_RECEIVED;
                                else if (byte != FLAG) state = START;
                                break;
                            case A_RECEIVED:
                                if (byte == C_UA) state = C_RECEIVED;
                                else if (byte == FLAG) state = FLAG_RECEIVED;
                                else state = START;
                                break;
                            case C_RECEIVED:
                                if (byte == (A_RX ^ C_UA)) state = BCC_CHECK;
                                else if (byte == FLAG) state = FLAG_RECEIVED;
                                else state = START;
                                break;
                            case BCC_CHECK:
                                if (byte == FLAG) state = STOP_RECEIVED;
                                else state = START;
                                break;
                            default: 
                                break;
                        }
                    }
                } 
                // Decrement retransmission counter
                connectionParameters.nRetransmissions--;
            }
            
            // Check if the connection was successfully established
            if (state != STOP_RECEIVED) return -1;
            break;  
        }

        case receiver: {
            // Loop until a STOP frame is received
            while (state != STOP_RECEIVED) {
                if (read(fd, &byte, 1) > 0) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RECEIVED;
                            break;
                        case FLAG_RECEIVED:
                            if (byte == A_TX) state = A_RECEIVED;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RECEIVED:
                            if (byte == C_SET) state = C_RECEIVED;
                            else if (byte == FLAG) state = FLAG_RECEIVED;
                            else state = START;
                            break;
                        case C_RECEIVED:
                            if (byte == (A_TX ^ C_SET)) state = BCC_CHECK;
                            else if (byte == FLAG) state = FLAG_RECEIVED;
                            else state = START;
                            break;
                        case BCC_CHECK:
                            if (byte == FLAG) state = STOP_RECEIVED;
                            else state = START;
                            break;
                        default: 
                            break;
                    }
                }
            }  

            // Construct and send the UA frame in response to SET frame reception
            unsigned char uaFrame[5] = {FLAG, A_RX, C_UA, A_RX ^ C_UA, FLAG};
            // Send UA frame in response to SET frame reception
            if(write(fd, uaFrame, 5) < 0){
                printf("Send Frame Error\n");
                return -1;
            }
            break; 
        }
    
    // Return the file descriptor for the established connection
    return fd;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
// Function to write data to the link layer.
// Returns the number of bytes written or -1 on error.
int llwrite(const unsigned char *buf, int bufSize) {

    // Calculate frame size and allocate memory for the frame
    int frameSize = 6 + bufSize;
    unsigned char *frame = (unsigned char *) malloc(frameSize);
    
    // Construct the frame
    frame[0] = FLAG;
    frame[1] = A_TX;
    frame[2] = C_N(tramaTx);
    frame[3] = (frame[1] ^ frame[2]);
    memcpy(frame + 4, buf, bufSize);

    // Calculate and append BCC2
    unsigned char BCC2 = buf[0];
    for (unsigned int i = 1; i < bufSize; i++) BCC2 ^= buf[i];

    // Byte stuffing
    int j = 4;
    for (unsigned int i = 0; i < bufSize; i++) {
        if (buf[i] == FLAG || buf[i] == ESC) {
            frame = realloc(frame, ++frameSize);
            frame[j++] = ESC;
        }
        frame[j++] = buf[i];
    }
    frame[j++] = BCC2;
    frame[j++] = FLAG;

    int currentTransmission = 0;
    int rejected = 0, accepted = 0;

    // Loop until successful transmission or maximum retransmissions reached
    while (currentTransmission < retransmissions) { 
        alarmEnabled = FALSE;
        alarm(timeout);
        rejected = 0;
        accepted = 0;

        // Inner loop for waiting acknowledgment frames
        while (alarmEnabled == FALSE && !rejected && !accepted) {
            int bytes = write(fd, frame, j);
            if (bytes < 0) return -1;

            // Read supervision frame and check control field
            unsigned char byte, ctrlField = 0;
            llState state = START;
            
            // Loop until the end of frame is received or an alarm is triggered
            while (state != STOP_RECEIVED && alarmEnabled == FALSE) {  
                if (read(fd, &byte, 1) > 0 || 1) {
                    switch (state) {
                        case START:
                            if (byte == FLAG) state = FLAG_RECEIVED;
                            break;
                        case FLAG_RECEIVED:
                            if (byte == A_RX) state = A_RECEIVED;
                            else if (byte != FLAG) state = START;
                            break;
                        case A_RECEIVED:
                            if (byte == C_RR(0) || byte == C_RR(1) || byte == C_REJ(0) || byte == C_REJ(1) || byte == C_DISC){
                                state = C_RECEIVED;
                                ctrlField = byte;   
                            }
                            else if (byte == FLAG) state = FLAG_RECEIVED;
                            else state = START;
                            break;
                        case C_RECEIVED:
                            if (byte == (A_RX ^ ctrlField)) state = BCC_CHECK;
                            else if (byte == FLAG) state = FLAG_RECEIVED;
                            else state = START;
                            break;
                        case BCC_CHECK:
                            if (byte == FLAG){
                                state = STOP_RECEIVED;
                            }
                            else state = START;
                            break;
                        default: 
                            break;
                    }
                } 
            } 

            if (!ctrlField) {
                continue;
            } else if (ctrlField == C_REJ(0) || ctrlField == C_REJ(1)) {
                rejected = 1;
            } else if (ctrlField == C_RR(0) || ctrlField == C_RR(1)) {
                accepted = 1;
                tramaTx = (tramaTx + 1) % 2;  // Nr module-2 counter (enables to distinguish frame 0 and frame 1)
            } else {
                continue;
            }
        }

        // Break the loop if the frame is accepted
        if (accepted) break;
        currentTransmission++;
    }
    
    // Free allocated memory
    free(frame);
    
    // Return the number of bytes written or -1 on failure
    if (accepted) return frameSize;
    else {
        // If transmission is not successful, close the connection
        llclose(1);
        return -1;
    }
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
// Function to read data from the link layer.
// Returns the number of bytes read or -1 on error.
int llread(unsigned char *packet) {

    unsigned char byte, ctrlField;
    int i = 0;
    llState state = START;

    // Loop until the end of frame is received
    while (state != STOP_RECEIVED) {  
        int bytes = read(fd, &byte, 1);
        if (bytes > 0) {
            switch (state) {
                case START:
                    if (byte == FLAG) state = FLAG_RECEIVED;
                    break;
                case FLAG_RECEIVED:
                    if (byte == A_TX) state = A_RECEIVED;
                    else if (byte != FLAG) state = START;
                    break;
                case A_RECEIVED:
                    if (byte == C_N(0) || byte == C_N(1)){
                        state = C_RECEIVED;
                        ctrlField = byte;   
                    }
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else if (byte == C_DISC) {
                        unsigned char discFrame[5] = {FLAG, A_RX, C_DISC, A_RX ^ C_DISC, FLAG};
                        if (write(fd, discFrame, 5) < 0) {
                            printf("Send Frame Error\n");
                            return -1;
                        }
                        return 0;
                    }
                    else state = START;
                    break;
                case C_RECEIVED:
                    if (byte == (A_TX ^ ctrlField)) state = BYTE_DESTUFFING;
                    else if (byte == FLAG) state = FLAG_RECEIVED;
                    else state = START;
                    break;
                case BYTE_DESTUFFING:
                    if (byte == ESC) state = DATA_FOUND;
                    else if (byte == FLAG){
                        unsigned char bcc2 = packet[i-1];
                        i--;
                        packet[i] = '\0';
                        unsigned char bcc2Check = packet[0];

                        // Check BCC2 for error detection
                        for (unsigned int j = 1; j < i; j++)
                            bcc2Check ^= packet[j];

                        // If BCC2 is correct, stop receiving and send acknowledgment
                        if (bcc2 == bcc2Check){
                            state = STOP_RECEIVED;
                            unsigned char rrFrame[5] = {FLAG, A_RX, C_RR(tramaRx), A_RX ^ C_RR(tramaRx), FLAG};
                            if (write(fd, rrFrame, 5) < 0) {
                                printf("Send Frame Error\n");
                            }
                            tramaRx = (tramaRx + 1) % 2; // Ns module-2 counter (enables to distinguish frame 0 and frame 1)
                            printf("-----------------------\n");
                            printf("Received %d bytes\n", i);
                            return i; 
                        }
                        // If BCC2 is incorrect, request retransmission
                        else{
                            printf("Retransmission Error\n");
                            unsigned char rejFrame[5] = {FLAG, A_RX, C_REJ(tramaRx), A_RX ^ C_REJ(tramaRx), FLAG};
                            if (write(fd, rejFrame, 5) < 0) {
                                printf("Send Frame Error\n");
                            }
                            return -1;
                        };

                    }
                    else{
                        packet[i++] = byte;
                    }
                    break;
                case DATA_FOUND:
                    state = BYTE_DESTUFFING;
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

    // Return -1 if an error occurs
    return -1;
}



////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
// Function to close the link layer connection.
// Returns 1 on success, -1 on error.
int llclose(int showStatistics) {

    llState state = START;
    unsigned char byte;
    (void) signal(SIGALRM, alarmHandler);
    
    // Loop until the maximum number of retransmissions is reached or the connection is closed
    while (retransmissions != 0 && state != STOP_RECEIVED) {
                
        // Construct and send DISC frame
        unsigned char discFrame[5] = {FLAG, A_TX, C_DISC, A_TX ^ C_DISC, FLAG};
        if (write(fd, discFrame, 5) < 0) {
            printf("Send Frame Error\n");
            return -1;
        }

        alarm(timeout);
        alarmEnabled = FALSE;
                
        // Wait for response
        while (alarmEnabled == FALSE && state != STOP_RECEIVED) {

            if (read(fd, &byte, 1) > 0) {
                switch (state) {
                    case START:
                        if (byte == FLAG) state = FLAG_RECEIVED;
                        break;
                    case FLAG_RECEIVED:
                        if (byte == A_RX) state = A_RECEIVED;
                        else if (byte != FLAG) state = START;
                        break;
                    case A_RECEIVED:
                        if (byte == C_DISC) state = C_RECEIVED;
                        else if (byte == FLAG) state = FLAG_RECEIVED;
                        else state = START;
                        break;
                    case C_RECEIVED:
                        if (byte == (A_RX ^ C_DISC)) state = BCC_CHECK;
                        else if (byte == FLAG) state = FLAG_RECEIVED;
                        else state = START;
                        break;
                    case BCC_CHECK:
                        if (byte == FLAG) state = STOP_RECEIVED;
                        else state = START;
                        break;
                    default: 
                        break;
                }
            }
        } 
        retransmissions--;
    }

    // Check if the connection is closed
    if (state != STOP_RECEIVED) return -1;

    // Construct and send UA frame to acknowledge the DISC frame
    unsigned char uaFrame[5] = {FLAG, A_TX, C_UA, A_TX ^ C_UA, FLAG};
    if (write(fd, uaFrame, 5) < 0) {
        printf("Send Frame Error\n");
        return -1;
    }

    // Print statistics if required
    if (showStatistics == 1) {
        clock_t end_time = clock();

        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("Elapsed time: %f seconds\n", elapsed_time);
    }
    
    // Close the file descriptor
    return close(fd);
}