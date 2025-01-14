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
#include <linux/slab.h> // kmalloc
#include "aesdchar.h"
#include "aesd_ioctl.h"

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0)
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(type, arg, cmd)
#else
#define access_ok_wrapper(type,arg,cmd) \
	access_ok(arg, cmd)
#endif

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Matheus Henrique"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
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
    /**
     * TODO: handle read
     */
    size_t offset = *f_pos;
    size_t offset_rtn = 0;
    struct aesd_dev *dev = filp->private_data;

    if (mutex_lock_interruptible(&dev->lock)) {
        return -ERESTARTSYS;
    }

    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, offset, &offset_rtn);
    mutex_unlock(&dev->lock);
    if (!entry) {
        retval = 0;
        goto out;
    }

    if (entry->size - offset_rtn < count) {
        retval = entry->size - offset_rtn;
    } else {
        retval = count;
    }

    if (copy_to_user(buf, entry->buffptr + offset_rtn, retval)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += retval;
    
out:
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    char *kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf) {
        goto out;
    }
    if (mutex_lock_interruptible(&dev->lock)) {
        retval = -ERESTARTSYS;
        goto out;
    }

    if (copy_from_user(kbuf, buf, count)) {
        retval = -EFAULT;
        kfree(kbuf);
        goto out;
    }

    size_t combined_buffer_size = dev->working_entry.size + count;
    char *combined_buffer = kmalloc(combined_buffer_size, GFP_KERNEL);
    if (!combined_buffer) {
        kfree(kbuf);
        retval = -ENOMEM;
        goto out;
    }

    // copy current working buffer to combined buffer
    if (dev->working_entry.buffptr) {
        memcpy(combined_buffer, dev->working_entry.buffptr, dev->working_entry.size);
        kfree(dev->working_entry.buffptr);
    }

    // copy new buffer to combined buffer
    memcpy(combined_buffer + dev->working_entry.size, kbuf, count);
    dev->working_entry.buffptr = combined_buffer;
    dev->working_entry.size = combined_buffer_size;

    if (memchr(combined_buffer, '\n', combined_buffer_size)) {
        // if newline is found, add entry to circular buffer and reset working entry
        // if there is no newline in the buffer, keep the buffer in the working entry until a newline is found
        const char *replaced_entry = aesd_circular_buffer_add_entry(&dev->buffer, &dev->working_entry);
        if (replaced_entry) {
            kfree(replaced_entry);
        }
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }
    mutex_unlock(&dev->lock);

    *f_pos += count;
    retval = count;

out:
    if (retval != count) {
        kfree(kbuf);
    }
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    loff_t result;
    size_t buffer_size = 0;
    int index;

    if (mutex_lock_interruptible(&aesd_device.lock)) {
        return -ERESTARTSYS;
    }

    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr != NULL) {
            buffer_size += entry->size;
        }
    }

    result = fixed_size_llseek(filp, offset, whence, buffer_size);
    
    mutex_unlock(&aesd_device.lock);

    return result;
}

/**
 * Adist the file offset (f_pos) parameter of @param filp based on the location specified by
 * @param write_cmd (the zero referenced write command to locate)
 * and @param write_cmd_offset (the zero referenced offset into the command)
 * @return 0 if successful, negative if error occurred
 *      -ERESTARTSYS if mutex could not be obtained
 *      -EINVEL if write command or write_cmd_offset was out of range
 */
static long aesd_adjust_file_offset (struct file *filp, uint32_t write_cmd, uint32_t write_cmd_offset) {
    int retval = 0;
    struct aesd_buffer_entry *entry;
    size_t offset = 0;

    PDEBUG("write_cmd: %d\n", write_cmd);
    PDEBUG("write_cmd_offset: %d\n", write_cmd_offset);

    
    if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED || 
        write_cmd_offset >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        return -EINVAL;
    }

    if (write_cmd_offset >= aesd_device.buffer.entry[write_cmd].size) {
        return -EINVAL;
    }

    // havent written this command yet
    if (aesd_device.buffer.entry[write_cmd].size == 0) {
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&aesd_device.lock)) {
        return -ERESTARTSYS;
    }

    struct aesd_dev *dev = filp->private_data;

    // PDEBUG("write_cmd: %d\n", write_cmd);
    // PDEBUG("write_cmd_offset: %d\n", write_cmd_offset);

    // calculate the start offset to write_cmd
    // add length of each write between the output pointer and write_cmd
    entry = &aesd_device.buffer.entry[write_cmd];
    if (!entry->buffptr || write_cmd_offset >= entry->size) {
        retval = -EINVAL;
        goto out;
    }
    int i;
    for (i = 0; i < write_cmd; i++) {
        offset += aesd_device.buffer.entry[i].size;
    }

    retval = offset + write_cmd_offset;

out:
    mutex_unlock(&aesd_device.lock);
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0, tmp;
    int retval = 0;

    PDEBUG("aesd_ioctl\n");
    PDEBUG("cmd: %d\n", _IOC_NR(cmd));

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
        err = !access_ok_wrapper(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
        err = !access_ok_wrapper(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err) return -EFAULT;

    switch(cmd) {
        case AESDCHAR_IOCSEEKTO:
            PDEBUG("AESDCHAR_IOCSEEKTO\n");
            struct aesd_seekto seekto;
            if (copy_from_user(&seekto, (struct aesd_seekto *)arg, sizeof(seekto)) != 0) {
                retval = -EFAULT;
            } else {
                retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            }
            break;
        default:  /* redundant, as cmd was checked against MAXNR */
            return -ENOTTY;
    }

    PDEBUG("ioctl retval: %d\n", retval);
    filp->f_pos = retval;
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .llseek =   aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
    aesd_circular_buffer_init(&aesd_device.buffer);
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
