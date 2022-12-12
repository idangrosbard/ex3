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
   	printk(KERN_ERR "OPEN: Start open file\n");
    minor = (unsigned int)iminor(inode);
    
    if (opened_slots[minor] == 0) {
		printk(KERN_ERR "OPEN: File wasn't opened yet\n");    
        INIT_RADIX_TREE(&slots[minor], GFP_ATOMIC);
		printk(KERN_ERR "OPEN: Initialized radix tree\n");        
        INIT_LIST_HEAD(&existing_slot_channels[minor]);\
		printk(KERN_ERR "OPEN: Initialied list\n");                
        opened_slots[minor]++;
    }

    file->private_data = (void *)kmalloc(sizeof(uint32_t), GFP_KERNEL);
	printk(KERN_ERR "Opened private data slot\n");            
    return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode *inode,
                          struct file *file)
{
    // As there's no requirement to release the tree once devices close,
    // we simply exit upon release
  	printk(KERN_ERR "RELEASE: Device release\n");
    kfree(file->private_data);
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
    uint32_t channel;
    struct channel_message * lookup_value;
  	printk(KERN_ERR "READ: Device read\n");
    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    channel = *((uint32_t *)file->private_data);
    if (channel == 0) {
        return -EINVAL;
    }

    lookup_value = (struct channel_message *)radix_tree_lookup(&slots[minor], channel);

    if (lookup_value == NULL) {
        // If the radix tree doesn't have the channel stored, we return errno = EINVAL (invalid channel)
      	printk(KERN_ERR "READ: Coudlnt find the value in the tree\n");
        return -EWOULDBLOCK;
    } else {
        // The channel exists, and we can copy the message from it (byte by byte)
        
        if (access_ok(buffer, lookup_value->size) == 0) {
           	printk(KERN_ERR "READ: Can't write to the buffer\n");
            // If we can't write to the buffer, we return an EFAULT
            return -EFAULT;
        }
        else {
           	printk(KERN_ERR "READ: Trying to write to the buffer\n");
            number_of_err_bytes = copy_to_user(buffer, lookup_value->message, lookup_value->size);

            // If the buffer isn't large enough for the message
            if (number_of_err_bytes > 0) {
	           	printk(KERN_ERR "READ: Error in %ld bytes\n", number_of_err_bytes);
                return -ENOSPC;
            }
            
            // Finally, if everything went as expected:
           	printk(KERN_ERR "READ: Finished reading\n");
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
    int radix_tree_action_status;
    ssize_t number_of_err_bytes;
    uint32_t channel;
    struct channel_message * new_message, * old_message;
    struct channel_list * new_channel;
    

    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    channel = *((uint32_t *)file->private_data);
	printk(KERN_ERR "WRITE: Identified channel %d\n", channel);
    if (channel == 0) {
        return -EINVAL;
    }

    // If the length we are requested to write is too long, we return EINVAL
    if ((length >= BUF_LEN) || (length == 0)) {
       	printk(KERN_ERR "WRITE: Invalid message of size %ld\n", length);
        return -EMSGSIZE;
    }
    if (access_ok(buffer, length) == 0) {
        // If we can't read from the buffer, we return an EFAULT
       	printk(KERN_ERR "WRITE: Can't write to the buffer\n");
        return -EFAULT;
    }
    
    // Create the new message
    new_message = kmalloc(sizeof(struct channel_message), GFP_KERNEL);
	printk(KERN_ERR "WRITE: Attempting to write to addr %p\n", (void *)(new_message));
    if (new_message == NULL) {
       	printk(KERN_ERR "WRITE: Couldn't write message\n");
        // We couldn't allocate space for the message
        return -ENOSPC;
    }
    new_message->message = kmalloc(length, GFP_KERNEL);
   	printk(KERN_ERR "WRITE: Attempting to write message content to addr %p\n", (void *)(new_message->message));
    if (new_message->message == NULL) {
        // We couldn't allocate space for the message
        kfree((void *)new_message);
        return -ENOSPC;
    }

    new_message->size = length;

    number_of_err_bytes = copy_from_user(new_message->message, buffer, new_message->size);
    printk(KERN_ERR "WRITE: Invalid write bytes %ld\n", number_of_err_bytes);
    // If any bytes could not pass for some reason
    if (number_of_err_bytes > 0) {
        kfree((void *)(new_message->message));
        kfree((void *)new_message);
        return -ENOSPC;
    }
    
    old_message = (struct channel_message *)radix_tree_lookup(&slots[minor], channel);

    if (old_message != (struct channel_message *)NULL) {
	    printk(KERN_ERR "WRITE: Found old message at %p\n", old_message);
        // If the channel was initialized, we free the existing message
        kfree((void *)(old_message->message));
        kfree((void *)old_message);
        radix_tree_delete(&slots[minor], channel);
    } else {
        // We add the channel to the list of open channels:
		printk(KERN_ERR "WRITE: Adding a new channel to the list %d\n", channel);
        minor = (uint8_t)iminor(file_inode(file));
        new_channel = kmalloc(sizeof(struct channel_list), GFP_KERNEL);
        new_channel->channel = channel;
        INIT_LIST_HEAD(&(new_channel->list));
        list_add_tail(&(new_channel->list), &existing_slot_channels[minor]);
    }

    // We insert the new message to the channel
    radix_tree_action_status = radix_tree_insert(&slots[minor], channel, new_message);
	printk(KERN_ERR "WRITE: Inserting to tree with status %d\n", radix_tree_action_status);
    if (radix_tree_action_status != 0) {
        // If we couldn't insert the message, free the memory it takes
        kfree((void *)(new_message->message));
        kfree((void *)new_message);
        return radix_tree_action_status;
    }
    
    // Finally, if everything went as expected...
    return (ssize_t)(new_message->size);

}

//----------------------------------------------------------------
static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         long unsigned int ioctl_param)
{
   	printk(KERN_ERR "IOCTL\n");
    // Make sure the correct ioctl function was called
    if (MSG_SLOT_CHANNEL == ioctl_command_id)
    {
        // Make sure the that the channel is valid
        if (ioctl_param != 0) {
       	   	printk(KERN_ERR "IOCTL: Setting channel\n");
            // Set the active channel in the file's private data
            *((uint32_t *)file->private_data) = (uint32_t)ioctl_param;

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
    // uint8_t i;
    // init dev struct
    // memset(&device_info, 0, sizeof(struct chardev_info));    
    
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

    // // We set all slots as uninitialized
    // for (i = 0; i < MAX_SLOTS; i++) {
    //     opened_slots[i] = 0;
    // }

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
	
    // For each open slot:
    for (i = 0; i < MAX_SLOTS; i++) {
        if (opened_slots[i] > 0) {
	        opened_slots[i] = 0;
	        printk(KERN_ERR "CLEANUP: Slot %d was opened in the file\n", i);
            list_for_each_entry(temp, &existing_slot_channels[i], list) {
              	printk(KERN_ERR "CLEANUP: Iterating open channels\n");
                channel = temp->channel;
               	printk(KERN_ERR "CLEANUP: Encountered channel %d\n", channel);
                open_message = (struct channel_message *)radix_tree_lookup(&slots[i], channel);

                if (open_message != NULL) {
       				printk(KERN_ERR "CLEANUP: Found the open message\n");
                    // If the channel was initialized, we free the existing message
                    // and delete from the tree
                    kfree((void *)(open_message->message));
                    kfree((void *)open_message);
                    radix_tree_delete(&slots[i], channel);
                }
            }
            temp = NULL;
            // Finaly we delete the open channels list
			printk(KERN_ERR "CLEANUP: Deleting the list\n");
            list_for_each_entry_safe(cursor, temp, &existing_slot_channels[i], list) {
                list_del(&(cursor->list));
                kfree((void *)cursor);
            }
        }
    }

	printk(KERN_ERR "CLEANUP: Second to last line\n");
    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
   	printk(KERN_ERR "CLEANUP: last line\n");
}
	
//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
