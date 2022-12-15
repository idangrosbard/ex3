// Declare what kind of code we want
// from the header files. Defining __KERNEL__
// and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/kernel.h>     /* We're doing kernel work */
#include <linux/module.h>     /* Specifically, a module */
#include <linux/fs.h>         /* for register_chrdev */
#include <linux/uaccess.h>    /* for get_user and put_user */
#include <linux/string.h>     /* for memset. NOTE - not string.h!*/
#include <linux/errno.h>      /* for returning errno */
#include <linux/radix-tree.h> /* to access a double linked list*/
#include <linux/radix-tree.h> /* to access a double linked list*/
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

// Our custom definitions of IOCTL operations
#include "message_slot.h"


struct channel_list {
    struct list_head list;
    uint32_t channel;
};


static uint32_t opened_slots[MAX_SLOTS];

static struct list_head existing_slot_channels[MAX_SLOTS];

// a radix tree for the channels of each file (access files according to the minor)
static struct radix_tree_root slots[MAX_SLOTS];


struct channel_message {
    ssize_t size;
    char * message;
};



//================== DEVICE FUNCTIONS ===========================
static int device_open(struct inode *inode,
                       struct file *file)
{
    unsigned int minor;
    minor = (unsigned int)iminor(inode);
    
    if (opened_slots[minor] == 0) {
        INIT_RADIX_TREE(&slots[minor], GFP_ATOMIC);
        INIT_LIST_HEAD(&existing_slot_channels[minor]);           
        opened_slots[minor]++;
    }

    file->private_data = NULL;
    return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode *inode,
                          struct file *file)
{
    // As there's no requirement to release the tree once devices close,
    // we simply exit upon release
    file->private_data = NULL;
    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read(struct file *file,
                           char __user *buffer,
                           size_t length,
                           loff_t *offset)
{
    uint8_t minor;
    ssize_t number_of_err_bytes;
    struct channel_message * lookup_value;
    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    if (file->private_data == NULL) {
        return -EINVAL;
    }

    lookup_value = (struct channel_message *)file->private_data;

    if (lookup_value->size == 0) {
        // Content of the channel wasn't initialized
        return -EWOULDBLOCK;
    } else {
        // The channel exists, and we can copy the message from it (byte by byte)
        
        if (access_ok(buffer, lookup_value->size) == 0) {
            // If we can't write to the buffer, we return an EFAULT
            return -EFAULT;
        }
        else {
            number_of_err_bytes = copy_to_user(buffer, lookup_value->message, lookup_value->size);

            // If the buffer isn't large enough for the message
            if (number_of_err_bytes > 0) {
                return -ENOSPC;
            }
            
            // Finally, if everything went as expected:
            return (ssize_t)(lookup_value->size);
        }
    }
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write(struct file *file,
                            const char __user *buffer,
                            size_t length,
                            loff_t *offset)
{
    uint8_t minor;
    uint8_t * message_content;
    ssize_t number_of_err_bytes;
    struct channel_message * message;
    
    if (file->private_data == NULL) {
        return -EINVAL;
    }

    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    message = (struct channel_message *)file->private_data;
    if (message == NULL) {
        return -EINVAL;
    }

    // If the length we are requested to write is too long, we return EINVAL
    if ((length >= BUF_LEN) || (length == 0)) {
        return -EMSGSIZE;
    }
    if (access_ok(buffer, length) == 0) {
        // If we can't read from the buffer, we return an EFAULT
        return -EFAULT;
    }
    
    // Create the new message
    
    message_content = kmalloc(length, GFP_KERNEL);
    if (message_content == NULL) {
        // We couldn't allocate space for the message
        return -ENOSPC;
    }

    number_of_err_bytes = copy_from_user(message_content, buffer, length);
    // If any bytes could not pass for some reason
    if (number_of_err_bytes > 0) {
        kfree((void *)message_content);
        return -ENOSPC;
    }
    kfree(message->message);
    message->message = message_content;
    message->size = length;
    return length;
}

//----------------------------------------------------------------
static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         long unsigned int ioctl_param)
{
	uint8_t minor;
	int radix_tree_action_status;
	struct channel_message * message;	
    struct channel_list * new_channel;
    
   	minor = (uint8_t)iminor(file_inode(file));
    // Make sure the correct ioctl function was called
    if (MSG_SLOT_CHANNEL == ioctl_command_id)
    {
        // Make sure the that the channel is valid
        if (ioctl_param != 0) {
        	message = (struct channel_message *)radix_tree_lookup(&slots[minor], ioctl_param);
    		if (message == (struct channel_message *)NULL) {
				// The channel wasn't initialized yet, so we initialize it
    			message = kmalloc(sizeof(struct channel_message), GFP_KERNEL);
    			if (message == NULL) {
	    			// Couldn't alocate a channel
    				return -ENOSPC;
    			}
    			message->message = NULL;
    			message->size = 0;
    			
    			radix_tree_action_status = radix_tree_insert(&slots[minor], ioctl_param, message);
    			if (radix_tree_action_status != 0) {
        			// Couldn't insert the channel to the tree
        			kfree((void *)message);
        			return radix_tree_action_status;
    			}
    		}
            // Set the active channel in the file's private data
            file->private_data = message;
            
            // We add the channel to the list of open channels:
        	new_channel = kmalloc(sizeof(struct channel_list), GFP_KERNEL);
        	new_channel->channel = ioctl_param;
        	INIT_LIST_HEAD(&(new_channel->list));
        	list_add_tail(&(new_channel->list), &existing_slot_channels[minor]);
            return SUCCESS;
        }
    }
    // If we don't get the correct command, or the channel id is 0, we set errno=EINVAL
    return -EINVAL;
}

//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .release = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void)
{
    int rc = -1; 
    
    memset( &opened_slots, 0, MAX_SLOTS * sizeof(uint32_t) );
    memset( &existing_slot_channels, 0, MAX_SLOTS * sizeof(struct list_head) );
    memset( &slots, 0, MAX_SLOTS * sizeof(struct radix_tree_root) );

    // Register driver capabilities. Obtain major num
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);
    
    // Negative values signify an error
    if (rc < 0)
    {
        printk(KERN_ERR "%s registraion failed for  %d\n",
               DEVICE_FILE_NAME, MAJOR_NUM);
        return rc;
    }

    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
    // Unregister the device
    uint32_t channel;
    struct channel_list *cursor, *temp;
    struct channel_message *open_message;
    uint16_t i = 0;
	
    // For each slot:
    for (i = 0; i < MAX_SLOTS; i++) {
        if (opened_slots[i] > 0) {
	        // If we found an open slot
	        opened_slots[i] = 0;
            list_for_each_entry(temp, &existing_slot_channels[i], list) {
            	// We iterate over all channels in the slot
                channel = temp->channel;
                open_message = (struct channel_message *)radix_tree_lookup(&slots[i], channel);

                if (open_message != NULL) {
                    // If the channel was initialized, we free the existing message
                    // and delete from the tree
                    if (open_message->size > 0) {
	                    kfree((void *)(open_message->message));
                    }
                    kfree((void *)open_message);
                    radix_tree_delete(&slots[i], channel);
                }
            }
            temp = NULL;
            // Finaly we delete the open channels list
            list_for_each_entry_safe(cursor, temp, &existing_slot_channels[i], list) {
                list_del(&(cursor->list));
                kfree((void *)cursor);
            }
        }
    }

    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}
	
//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
