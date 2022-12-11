#include "meesage_slot.h"    

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char ** argv) {
    FILE * message_slot;
    unsigned long channel;
    int ret_val;
    char * message_slot_file_path, * message;

    if (argc != 4) {
        fprintf(stderr, "\n%s\n", strerror(EINVAL));
        exit(1);
    }
    message_slot_file_path = argv[1];
    channel = strtoul(argv[2], NULL, 10);
    
    if (channel == 0) {
        fprintf(stderr, "\n%s\n", strerror(EINVAL));
        exit(1);
    }

    message = argv[3];

    message_slot = open( message_slot_file_path, O_RDWR );
    if( message_slot < 0 ) {
        fprintf(stderr, "ioctl error: \n%s\n", strerror(errno));
        exit(1);
    }

    ret_val = ioctl( file_desc, MSG_SLOT_CHANNEL, channel);
    if (ret_val < 0) {
        fprintf(stderr, "ioctl error: \n%s\n", strerror(errno));
        exit(1);
    }

    ret_val = write( file_desc, message, strlen(message));
    if (ret_val < 0) {
        fprintf(stderr, "write error: \n%s\n", strerror(errno));
        exit(1);
    }

    ret_val = close(message_slot);
    if (ret_val < 0) {
        fprintf(stderr, "close error: \n%s\n", strerror(errno));
        exit(1);
    }

    return 0;
}