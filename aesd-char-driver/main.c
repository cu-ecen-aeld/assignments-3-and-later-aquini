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
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Rafael Aquini"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
	struct aesd_dev *device;

	PDEBUG("open");
	device = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = device;

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

static loff_t aesd_buffer_size(struct aesd_dev *device)
{
	loff_t size = 0;
	int index;

	for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++) {
		struct aesd_buffer_entry *entry = device->queue.entry[index];

		if (entry != NULL)
			size += entry->size;
	}

	return size;
}

static loff_t __aesd_llseek(struct aesd_dev *device, struct file *filp, loff_t off, int whence)
{
	return fixed_size_llseek(filp, off, whence, aesd_buffer_size(device));
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
	struct aesd_dev *device = filp->private_data;
	loff_t ret;

	if (!device)
		return -EBADF;

	mutex_lock(&device->lock);
	ret = __aesd_llseek(device, filp, off, whence);
	mutex_unlock(&device->lock);

	return ret;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct aesd_buffer_entry *entry;
	struct aesd_dev *device;
	ssize_t retval = 0;
	size_t byte = 0;
	loff_t size = 0;

	PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
	device = filp->private_data;

	if (!device)
		return -ENXIO;

	if (mutex_lock_interruptible(&device->lock))
		return -ERESTARTSYS;

	size = aesd_buffer_size(device);
	if (*f_pos > size)
		goto nothing;

	entry = aesd_circular_buffer_find_entry_offset_for_fpos(&device->queue, *f_pos, &byte);
	if (!entry)
		goto nothing;

	if (*f_pos + count > size)
		count = size - *f_pos;

	if (count > entry->size)
		count = entry->size;

	if (count + byte > entry->size)
		count -= byte;

	if (copy_to_user(buf, entry->buffptr+byte, count)) {
		retval = -EFAULT;
		goto nothing;
	}

	*f_pos += count;
	mutex_unlock(&device->lock);
	return count;
nothing:
	mutex_unlock (&device->lock);
	return retval;
}

static struct aesd_buffer_entry pending_cmd = {.buffptr = NULL, .size = 0};

static bool realloc_cmd_entry(struct aesd_buffer_entry *entry, char *new_cmd, size_t cmd_len)
{
	size_t size = entry->size + cmd_len;
	char *buff = kzalloc(size + 1, GFP_KERNEL);

	if (!buff)
		return false;

	memcpy(buff, entry->buffptr, entry->size);
	strcat(buff, new_cmd);
	kfree(entry->buffptr);
	entry->buffptr = buff;
	entry->size = size;

	return true;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	struct aesd_buffer_entry *entry;
	struct aesd_dev *device;
	char *new_cmd;
	ssize_t retval = -ENOMEM;

	PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
	device = filp->private_data;

	if (!device)
		return -ENXIO;

	if (mutex_lock_interruptible(&device->lock))
		return -ERESTARTSYS;

	new_cmd = kzalloc(count + 1, GFP_KERNEL);
	if (!new_cmd)
		goto nomem;

	entry = kzalloc(sizeof *entry, GFP_KERNEL);
	if (!entry) {
		kfree(new_cmd);
		goto nomem;
	}

	if (copy_from_user(new_cmd, buf, count)) {
		retval = -EFAULT;
		kfree(new_cmd);
		kfree(entry);
		goto nomem;
	}

	if (new_cmd[count-1] != '\n') {
		bool ret = realloc_cmd_entry(&pending_cmd, new_cmd, count);

		kfree(new_cmd);
		kfree(entry);
		entry = NULL;
		if (ret == false)
		       goto nomem;
	} else if (new_cmd[count-1] == '\n' && pending_cmd.size > 0) {
		bool ret = realloc_cmd_entry(&pending_cmd, new_cmd, count);

		kfree(new_cmd);
		if (ret == false)
		       goto nomem;

		entry->buffptr = pending_cmd.buffptr;
		entry->size = pending_cmd.size;
		entry = aesd_circular_buffer_add_entry(&device->queue, entry);
		memset(&pending_cmd, 0, sizeof pending_cmd);
	} else {
		entry->buffptr = new_cmd;
		entry->size = count;
		entry = aesd_circular_buffer_add_entry(&device->queue, entry);
	}

	if (entry != NULL) {
		kfree(entry->buffptr);
		kfree(entry);
	}

	*f_pos += count;
	mutex_unlock(&device->lock);
	return count;
nomem:
	mutex_unlock(&device->lock);
	return retval;
}

static long aesd_adjust_file_offset(struct file *filp, uint32_t cmd, uint32_t cmd_offset)
{
	struct aesd_dev *device = filp->private_data;
	struct aesd_buffer_entry *entry;
	loff_t ret, offset = 0;
	uint8_t index, i;

	if (!device)
		return -ENOTTY;

	if (cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
		return -EINVAL;

	mutex_lock(&device->lock);
	index = device->queue.out_offs;

	for (i = 0; ; i++) {
		entry = device->queue.entry[index];
		index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

		if (!entry)
			continue;

		if (i == cmd)
			break;

		offset += entry->size;
	}

	if (cmd_offset > entry->size) {
		mutex_unlock(&device->lock);
		return -EINVAL;
	}

	offset += cmd_offset;
	ret = __aesd_llseek(device, filp, offset, SEEK_SET);
	mutex_unlock(&device->lock);

	return ret;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct aesd_seekto seekto;

	switch (cmd) {
	case AESDCHAR_IOCSEEKTO:
		if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
			return -EFAULT;

		return aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
	}

	return -ENOTTY;
}

struct file_operations aesd_fops = {
    .owner	= THIS_MODULE,
    .read	= aesd_read,
    .write	= aesd_write,
    .open	= aesd_open,
    .release	= aesd_release,
    .llseek	= aesd_llseek,
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

	result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
	aesd_major = MAJOR(dev);
	if (result < 0) {
		printk(KERN_WARNING "Can't get major %d\n", aesd_major);
		return result;
	}

	memset(&aesd_device, 0, sizeof aesd_device);
	mutex_init(&aesd_device.lock);
	aesd_circular_buffer_init(&aesd_device.queue);

	result = aesd_setup_cdev(&aesd_device);
	if (result)
		unregister_chrdev_region(dev, 1);

	return result;
}

void aesd_cleanup_module(void)
{
	struct aesd_circular_buffer *buffer = &aesd_device.queue;
	dev_t devno = MKDEV(aesd_major, aesd_minor);

	cdev_del(&aesd_device.cdev);

	mutex_lock(&aesd_device.lock);
	for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
		struct aesd_buffer_entry *entry = buffer->entry[i];

		if (entry != NULL) {
			kfree(entry->buffptr);
			kfree(entry);
		}
	}
	mutex_unlock(&aesd_device.lock);

	if (pending_cmd.size > 0)
		kfree(pending_cmd.buffptr);

	unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
