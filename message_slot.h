#ifndef MESSAGESLOT_H
#define MESSAGESLOT_H

#include <linux/ioctl.h>

// The major device number.
#define MAJOR_NUM 235

// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned int)

#define DEVICE_RANGE_NAME "message_slot"
#define BUF_LEN 128
#define MAX_SLOTS 256
#define DEVICE_FILE_NAME "message_slot"
#define SUCCESS 0

#endif
