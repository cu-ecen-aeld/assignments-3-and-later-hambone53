/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/slab.h>		/* kmalloc() */
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Ryan Hamor"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    // Get a pointer to our aesd_device structure
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    // Set the file's private data to point to the device data
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);

    // Will read from our circular buffer a single buffer entry.  Entry will be refernced by f_pos
    if (mutex_lock_interruptible(&aesd_device.lock))
        return -ERESTARTSYS;
    
    // Get the buffer entry that corresponds to the f_pos
    size_t entry_offset_byte;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&aesd_device.circular_buffer, *f_pos, &entry_offset_byte);

    // Copy the bytes from the buffer entry to the user space
    if (entry != NULL) {
        size_t bytes_to_copy = entry->size - entry_offset_byte;
        if (bytes_to_copy > count) {
            bytes_to_copy = count;
        }
        if (copy_to_user(buf, entry->buffptr + entry_offset_byte, bytes_to_copy)) {
            retval = -EFAULT;
            goto exit_return;
        }
        *f_pos += bytes_to_copy;
        retval = bytes_to_copy;
    } else {
        // If we get here then we have reached the end of the buffer
        retval = 0;
    }

    exit_return:
    mutex_unlock(&aesd_device.lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    size_t previous_count = 0;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    if (mutex_lock_interruptible(&aesd_device.lock))
        return -ERESTARTSYS;

    // check if aesd_dev.working_entry is NULL and allocate memory if it is
    if (aesd_device.working_entry == NULL) {
        aesd_device.working_entry = kmalloc(count, GFP_KERNEL);
        if (aesd_device.working_entry == NULL) {
            PDEBUG("Failed to allocate memory for working_entry");
            retval = -ENOMEM;
            goto exit_return;
        }
        memset(aesd_device.working_entry, 0, count);
    } else {
        // If it is not NULL that means we are still appending to the buffer so reallocate more memory
        previous_count = strlen(aesd_device.working_entry);
        char *temp = krealloc(aesd_device.working_entry, count, GFP_KERNEL);
        if (temp == NULL) {
            PDEBUG("Failed to reallocate memory for working_entry");
            retval = -ENOMEM;
            goto exit_cleanup_working_entry;
        }
        aesd_device.working_entry = temp;
        memset(aesd_device.working_entry + previous_count, 0, count);
    }

    // copy the data from the user space to the kernel space
    if (copy_from_user(aesd_device.working_entry + previous_count, buf, count)) {
        PDEBUG("Failed to copy data from user space to kernel space");
        retval = -EFAULT;
        goto exit_cleanup_working_entry;
    }

    // Check if the last character is a newline
    if (aesd_device.working_entry[(count + previous_count) - 1] == '\n') {
        // If it is a newline then we can add the entry to the circular buffer
        // If the buffer does not return a null pointer we need to free the pointer it returns
        if (aesd_circular_buffer_add_entry(&aesd_device.circular_buffer, &(struct aesd_buffer_entry) {
            .buffptr = aesd_device.working_entry,
            .size = count + previous_count
        }) != NULL) {
            kfree(aesd_device.working_entry);
        }
    }

    // If we get here then we have successfully written to the buffer so return count
    retval = count;

    goto exit_return;

    exit_cleanup_working_entry:
    kfree(aesd_device.working_entry);

    exit_return:
    mutex_unlock(&aesd_device.lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    struct aesd_buffer_entry *entry;
    uint8_t index;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    //free all the pointers that are still in the circular buffer
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }

    //free the working entry if it is not null
    if (aesd_device.working_entry != NULL) {
        kfree(aesd_device.working_entry);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
