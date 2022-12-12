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

    if (argc != 3) {
        perror(strerror(EINVAL));
        exit(1);
    }
    message_slot_file_path = argv[1];
    channel = strtoul(argv[2], NULL, 10);
    
    if (channel == 0) {
        perror(strerror(EINVAL));
        exit(1);
    }
	
    message_slot = open( message_slot_file_path, O_RDONLY );
    if( message_slot < 0 ) {
        perror(strerror(errno));
        exit(1);
    }
    
    ret_val = ioctl( message_slot, MSG_SLOT_CHANNEL, channel);
    if (ret_val < 0) {
        perror(strerror(errno));
        exit(1);
    }
    
    message = malloc(129 * sizeof(char));
    message[128] = '\0';
    ret_val = read( message_slot, message, 128);
    if (ret_val < 0) {
        perror(strerror(errno));
        exit(1);
    }
	
    ret_val = close(message_slot);
    if (ret_val < 0) {
        perror(strerror(errno));
        exit(1);
    }
	
    ret_val = write(1, message, 128);
    if (ret_val < 0) {
    	perror(strerror(errno));
    }
    free(message);
	
    return 0;
}
