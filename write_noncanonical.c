// Write to serial port in non-canonical mode
//
// Modified by: Eduardo Nuno Almeida [enalmeida@fe.up.pt]

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>


// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define flag 0x7E
#define addr_s 0x03
#define addr_r 0x01
#define c_set 0x03
#define c_ua 0x07

#define BUF_SIZE 256

volatile int STOP = FALSE;


int main(int argc, char *argv[])
{
    // Program usage: Uses either COM1 or COM2
    const char *serialPortName = argv[1];

    if (argc < 2)
    {
        printf("Incorrect program usage\n"
               "Usage: %s <SerialPort>\n"
               "Example: %s /dev/ttyS1\n",
               argv[0],
               argv[0]);
        exit(1);
    }

    // Open serial port device for reading and writing, and not as controlling tty
    // because we don't want to get killed if linenoise sends CTRL-C.
    int fd = open(serialPortName, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror(serialPortName);
        exit(-1);
    }

    struct termios oldtio;
    struct termios newtio;

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
    newtio.c_cc[VTIME] = 0; // Inter-character timer unused
    newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

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

    // Create string to send
    unsigned char buf[BUF_SIZE] = {0};
    unsigned char setT[BUF_SIZE] = {0};
    unsigned char uaT[BUF_SIZE] = {0};




    // In non-canonical mode, '\n' does not end the writing.
    // Test this condition by placing a '\n' in the middle of the buffer.
    // The whole buffer must be sent even with the '\n'.
    buf[5] = '\n';
    setT[5] = '\n';
    uaT[5] = '\n';
    
    setT[0] = flag;
    setT[1] = addr_s;
    setT[2] = c_set;
    setT[3] = setT[1] ^ setT[2];
    setT[4] = flag;
    
    uaT[0] = flag;
    uaT[1] = addr_s;
    uaT[2] = c_ua;
    uaT[3] = uaT[1] ^ uaT[2];
    uaT[4] = flag;
    
    int alarmCountMax = 4;
    int alarmTimer = 3;
    int alarmCount = 0;
    int alarmEnabled = FALSE;

    int bytes_received = 0;
    
    void alarmHandler(){
        if(bytes_received != BUF_SIZE){
            alarmEnabled = FALSE;
            alarmCount++;
            int bytes = write(fd, setT,         BUF_SIZE);
    printf("%d bytes written\n", bytes);
        }
        printf("Alarm #%d\n",alarmCount);
    }
    
    int bytes = write(fd, setT,     BUF_SIZE);
    printf("%d bytes written\n", bytes);
    
    while(alarmCount <4 || bytes_received == BUF_SIZE){
        (void)signal(SIGALRM, alarmHandler);
        if(alarmEnabled = FALSE){
            alarm(3);
            alarmEnabled = 3;
        }
    }
    bytes_received = read(fd,buf,BUF_SIZE);
    
    if(buf[1] == uaT[1] && buf[2] == uaT[2]) {
        printf("received successfully!");
        printf("%d bytes received\n", bytes_received);
    }
    else{
        printf("a = 0x%02X\n", buf[1]);
        printf("c= 0x%02X\n", buf[2]);
        printf("ua is wrong!");
    }
    

    // Wait until all bytes have been written to the serial port
    sleep(1);

    // Restore the old port settings
    if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
    {
        perror("tcsetattr");
        exit(-1);
    }

    close(fd);

    return 0;
}
