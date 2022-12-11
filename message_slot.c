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

MODULE_LICENSE("GPL");

// Our custom definitions of IOCTL operations
#include "message_slot.h"


struct channel_list {
    struct list_head list,
    unsigned int channel
};


static uint32_t opened_slots[MAX_SLOTS];

static struct channel_list existing_slot_channels[MAX_SLOTS];

// a radix tree for the channels of each file (access files according to the minor)
static struct radix_tree_root slots[MAX_SLOTS];


struct channel_message {
    ssize_t size,
    char * message
};



//================== DEVICE FUNCTIONS ===========================
static int device_open(struct inode *inode,
                       struct file *file)
{
    unsigned int minor;
    minor = (unsigned int)iminor(inode);
    
    if (opened_slots[minor] == 0) {
        slots[minor] = RADIX_TREE_INIT(GFP_ATOMIC);
        
        existing_slot_channels[minor] = kmalloc(sizeof(struct channel_list), GFP_KERNEL);
        existing_slot_channels[minor]->channel = 0;
        INIT_LIST_HEAD(&existing_slot_channels[minor]->list);
        opened_slots[minor]++;
    }
    
    file->private_data = 0;

    return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode *inode,
                          struct file *file)
{
    // As there's no requirement to release the tree once devices close,
    // we simply exit upon release   
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
    ssize_t message_size, number_of_err_bytes;
    uint32_t channel;
    struct channel_message * lookup_value;

    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    channel = (uint32_t)file->private_data;
    if (channel == 0) {
        return -EINVAL;
    }

    lookup_value = (struct channel_message *)radix_tree_lookup(slots[minor], channel);

    if (lookup_value == NULL) {
        // If the radix tree doesn't have the channel stored, we return errno = EINVAL (invalid channel)
        return -EWOULDBLOCK;
    } else {
        // The channel exists, and we can copy the message from it (byte by byte)
        
        if (access_ok(VERIFY_WRITE, buffer, lookup_value->size) == 0) {
            // If we can't write to the buffer, we return an EFAULT
            return -EFAULT;
        }
        else {
            number_of_err_bytes = copy_to_user(buffer, lookup_value->message, lookup_value->size)
            // If the buffer isn't large enough for the message
            if (number_of_err_bytes > 0) {
                return -ENOSPC;
            }
            
            // Finally, if everything went as expected:
            return message_size;
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
    ssize_t message_size, number_of_err_bytes;
    uint32_t channel;
    struct channel_message * new_message, old_message;
    struct channel_list * new_channel;
    

    // We get the minor, and the channel number
    minor = (uint8_t)iminor(file_inode(file));
    channel = (uint32_t)file->private_data;
    if (channel == 0) {
        return -EINVAL;
    }

    // If the length we are requested to write is too long, we return EINVAL
    if ((length >= BUF_LEN) || (length == 0)) {
        return -EMSGSIZE;
    }
    if (access_ok(VERIFY_READ, buffer, length) == 0) {
        // If we can't read from the buffer, we return an EFAULT
        return -EFAULT;
    }
    
    // Create the new message
    new_message = kalloc(sizeof(struct channel_message), GFP_KERNEL);
    if (new_message == NULL) {
        // We couldn't allocate space for the message
        return -ENOSPC;
    }
    new_message->message = kmalloc(length, GFP_KERNEL);
    if (new_message->messaeg == NULL) {
        // We couldn't allocate space for the message
        kfree(new_message);
        return -ENOSPC;
    }

    new_message->size = length;

    number_of_err_bytes = copy_from_user(new_message->message, buffer, new_message->size)
    // If any bytes could not pass for some reason
    if (number_of_err_bytes > 0) {
        kfree(new_message->message);
        kfree(new_message);
        return -ENOSPC;
    }
    
    old_message = (struct channel_message *)radix_tree_lookup(slots[minor], channel);

    if (old_message != NULL) {
        // If the channel was initialized, we free the existing message
        kfree(old_message->message);
        kfree(old_message);
        radix_tree_delete(slots[minor], channel);
    } else {
        // We add the channel to the list of open channels:
        minor = (uint8_t)iminor(file_inode(file));
        new_channel = kmalloc(sizeof(struct channel_list), GFP_KERNEL);
        new_channel->channel = 0;
        INIT_LIST_HEAD(&new_channel->list);
        list_add_tail(&new_channel->list, &existing_slot_channels[minor]->list);
    }

    // We insert the new message to the channel
    radix_tree_action_status = radix_tree_insert(slots[minor], channel, new_message)
    if (radix_tree_action_status != 0) {
        // If we couldn't insert the message, free the memory it takes
        kfree(new_message->message);
        kfree(new_message);
        return radix_tree_action_status;
    }
    
    // Finally, if everything went as expected...
    return message_size;

}

//----------------------------------------------------------------
static long device_ioctl(struct file *file,
                         unsigned int ioctl_command_id,
                         unsigned int ioctl_param)
{
    // Make sure the correct ioctl function was called
    if (MSG_SLOT_CHANNEL == ioctl_command_id)
    {
        // Make sure the that the channel is valid
        if (ioctl_param != 0) {
            // Set the active channel in the file's private data
            file->private_data = *(void)ioctl_param;

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
    uint8_t i;
    // init dev struct
    memset(&device_info, 0, sizeof(struct chardev_info));

    // Register driver capabilities. Obtain major num
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);

    // Negative values signify an error
    if (rc < 0)
    {
        printk(KERN_ERR "%s registraion failed for  %d\n",
               DEVICE_FILE_NAME, MAJOR_NUM);
        return rc;
    }

    // We set all slots as uninitialized
    for (i = 0; i < MAX_SLOTS; i++) {
        opened_slots[i] = 0;
    }

    printk("Registeration is successful. ");
    printk("If you want to talk to the device driver,\n");
    printk("you have to create a device file:\n");
    printk("mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
    printk("You can echo/cat to/from the device file.\n");
    printk("Dont forget to rm the device file and "
           "rmmod when you're done\n");

    return 0;
}

//---------------------------------------------------------------
static void __exit simple_cleanup(void)
{
    // Unregister the device
    struct channel_list *cursor, *temp;
    struct channel_message *open_message;
    uint8_t i;

    // For each open slot:
    for (i = 0; i < MAX_SLOTS; i++) {
        if (opened_slots[MAX_SLOTS] > 0) {
            list_for_each_entry(temp, &existing_slot_channels[i], list) {
                open_message = (struct channel_message *)radix_tree_lookup(slots[minor], channel);

                if (open_message != NULL) {
                    // If the channel was initialized, we free the existing message
                    // and delete from the tree
                    kfree(open_message->message);
                    kfree(open_message);
                    radix_tree_delete(slots[minor], channel);
                }
            }
            // Finaly we delete the open channels list
            list_for_each_entry_safe(cursor, temp, &existing_slot_channels[i], list) {
                list_del(&cursor->list);
                kfree(cursor);
            }
        }
    }

    // Should always succeed
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
