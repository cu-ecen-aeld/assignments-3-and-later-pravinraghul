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
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include <linux/slab.h>

#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("pravinraghul");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *aesd_data;

    PDEBUG("open");
    aesd_data = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = aesd_data;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *device;
    struct aesd_buffer_entry *entry;
    ssize_t offset;

    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    device = filp->private_data;

    if (mutex_lock_interruptible(&device->mlock))
	return -ERESTARTSYS;

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&device->cbuffer, *f_pos, &offset);
    if (!entry) {
	goto read_cleanup;
    }

    // remaining bytes in the buffer
    size_t rem = entry->size - offset;
    // update the size wrt requested bytes
    size_t size = (count > rem) ? rem : count;

    if (copy_to_user(buf, entry->buffptr + offset, size)) {
	retval = -EFAULT;
	goto read_cleanup;
    }

    // update the values
    retval = size;
    *f_pos += size;
    
 read_cleanup:
	mutex_unlock(&device->mlock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *device;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    device = filp->private_data;

    if (mutex_lock_interruptible(&device->mlock))
	return -ERESTARTSYS;

    char *newmem = kzalloc(device->bentry.size + count, GFP_KERNEL);
    if (!newmem) {
	PDEBUG("kmalloc failed\n");
	goto write_cleanup;
    }

    // copy the existing data to newmem if any
    if (device->bentry.size) {
	memcpy(newmem, device->bentry.buffptr, device->bentry.size);
    }

    // copy the rest of the data to newmem
    if (copy_from_user(newmem + device->bentry.size, buf, count)) {
	retval = -EFAULT;
	PDEBUG("copy_from_user failed\n");
	kfree(newmem);
    }

    // remove the existing data
    kfree(device->bentry.buffptr);

    // update the buffer entry and size
    device->bentry.buffptr = newmem;
    device->bentry.size += count;
    retval = count;

    // adding the command to the circular buffer
    if (device->bentry.buffptr[device->bentry.size - 1] == '\n') {
	PDEBUG("Recieved the termination character\n");
	const char *oldmem = aesd_circular_buffer_add_entry(&device->cbuffer,
							    &device->bentry);
	if (oldmem) {
	    PDEBUG("release the memory of the old value\n");
	    kfree(oldmem);
	}
	// clear the values
	device->bentry.buffptr = NULL;
	device->bentry.size = 0;
    }

write_cleanup:
    mutex_unlock(&device->mlock);
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

    mutex_init(&aesd_device.mlock);
    aesd_circular_buffer_init(&aesd_device.cbuffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    PDEBUG("ased module loaded %d\n", result);
    return result;
}

void aesd_cleanup_module(void)
{
    int index;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);
  
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.cbuffer, index) {
	kfree(entry->buffptr);
    }

    cdev_del(&aesd_device.cdev);
    unregister_chrdev_region(devno, 1);
    PDEBUG("ased module unloaded\n");
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
