#include "message_slot.h"    

#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(int argc, char ** argv) {
    int message_slot;
    unsigned long channel;
    int ret_val;
    char * message_slot_file_path, * message;

    // Check arguments
    if (argc != 4) {
    	errno = EINVAL;
    	perror("");
        exit(1);
    }
    message_slot_file_path = argv[1];
    channel = strtoul(argv[2], NULL, 10);
    
    // Check channel
    if (channel == 0) {
    	errno = EINVAL;
       	perror("");
        exit(1);
    }

    message = argv[3];

    // Open message slot
    message_slot = open( message_slot_file_path, O_RDWR );
    if( message_slot < 0 ) {
       	perror("");
        exit(1);
    }

    // Set channel
    ret_val = ioctl( message_slot, MSG_SLOT_CHANNEL, channel);
    if (ret_val < 0) {
      	perror("");
        exit(1);
    }

    // Write message
    ret_val = write( message_slot, message, strlen(message));
    if (ret_val < 0) {
       	perror("");
        exit(1);
    }

    // Close message slot
    ret_val = close( message_slot );
    if (ret_val < 0) {
    	perror("");
        exit(1);
    }

    return 0;
}
