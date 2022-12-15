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
    if (argc != 3) {
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
	
    // Open message slot
    message_slot = open( message_slot_file_path, O_RDONLY );
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
    
    // Read message
    message = malloc(129 * sizeof(char));
    message[128] = '\0';
    ret_val = read( message_slot, message, 128);
    if (ret_val < 0) {
        perror("");
        exit(1);
    }
	
    // Close message slot
    ret_val = close(message_slot);
    if (ret_val < 0) {
        perror("");
        exit(1);
    }
	
    // Print message
    ret_val = write(1, message, 128);
    if (ret_val < 0) {
    	perror("");
    }
    free(message);
	
    return 0;
}
