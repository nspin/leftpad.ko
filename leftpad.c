#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>

#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/slab.h>    

#include <linux/uaccess.h>

#include <linux/sched.h>
#include <linux/mutex.h>

#include <linux/string.h>


#define DEVICE_NAME "leftpad"
#define LEFT_PAD_WIDTH_MODULUS 1024
#define SUCCESS 0
#define BUF_SIZE 1024


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Spinale <spinalen@gmail.com>");
MODULE_DESCRIPTION("leftPad() implemented as a kernel module");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);


/* PARAMS */


static short leftpad_width = 80;
static short leftpad_fill = 32;

module_param(leftpad_width, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_width, "Lines are padded so that their width (including EOL) is the residue class modulo 1024 of the value of this parameter.");
module_param(leftpad_fill, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_fill, "The residue class modulo 128 of the value of this parameter is used to pad lines shorter than leftpad_width.");


/* BUFFERS */


struct buffer {
    wait_queue_head_t read_queue;
    struct mutex lock;
    char *start;
    unsigned long size, cursor, length;
};

static struct buffer *buffer_alloc(unsigned long size)
{
    struct buffer *buf = kzalloc(sizeof(*buf), GFP_KERNEL);
    if (unlikely(!buf)) {
        return NULL;
    }

    buf->start = kzalloc(size, GFP_KERNEL);
    if (unlikely(!buf->start)) {
        kfree(buf);
        return NULL;
    }

    init_waitqueue_head(&buf->read_queue);
    mutex_init(&buf->lock);

    buf->cursor = 0;
    buf->size = size;

    return buf;
}

static void buffer_free(struct buffer *buffer)
{
    kfree(buffer->start);
    kfree(buffer);
}


/* LOGIC */


static int leftpad_get_width(void)
{
    return leftpad_width % LEFT_PAD_WIDTH_MODULUS;
}

static char leftpad_get_fill(void)
{
    return leftpad_fill % 128;
}


/* CORE */


static int leftpad_in_use = 0;

static int leftpad_open(struct inode *, struct file *);
static int leftpad_release(struct inode *, struct file *);
static ssize_t leftpad_read(struct file *, char *, size_t, loff_t *);
static ssize_t leftpad_write(struct file *, const char *, size_t, loff_t *);


/* INIT+EXIT */


static struct file_operations fops = {
    .read = leftpad_read,
    .write = leftpad_write,
    .open = leftpad_open,
    .release = leftpad_release
};

static struct miscdevice leftpad_miscdevice = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "leftpad",
    .fops = &fops
};


static int __init leftpad_init(void)
{

    misc_register(&leftpad_miscdevice);
    printk(KERN_INFO "Init leftpad: width=%i, fill=ascii(%i)\n", leftpad_get_width(), leftpad_get_fill());
    return SUCCESS;
}

static void __exit leftpad_exit(void)
{
    misc_deregister(&leftpad_miscdevice);
}


module_init(leftpad_init);
module_exit(leftpad_exit);


/* IMPL */


static int leftpad_open(struct inode *inode, struct file *file)
{
    struct buffer *buf = buffer_alloc(BUF_SIZE);
    if (unlikely(!buf)) {
        return -ENOMEM;
    }

    file->private_data = buf;

    leftpad_in_use++;

    return SUCCESS;
}

static int leftpad_release(struct inode *inode, struct file *file)
{
    leftpad_in_use--;
	buffer_free(file->private_data);
    module_put(THIS_MODULE);
    return SUCCESS;
}

static ssize_t leftpad_read(struct file *file, char *buffer, size_t length, loff_t * offset)
{
    struct buffer *buf = file->private_data;
    size_t actual_length;

    if (mutex_lock_interruptible(&buf->lock)) {
        return -ERESTARTSYS;
    }

    while (!buf->length) {
        mutex_unlock(&buf->lock);
        if (file->f_flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        if (wait_event_interruptible (buf->read_queue, buf->length != 0)) {
            return -ERESTARTSYS;
        }
        if (mutex_lock_interruptible(&buf->lock)) {
            return -ERESTARTSYS;
        }
    }

    actual_length = min(length, buf->length);

    if (buf->cursor + actual_length > buf->size) {
        if (copy_to_user(buffer, buf->start + buf->cursor, buf->size - buf->cursor)) {
            mutex_unlock(&buf->lock);
            return -EFAULT;
        }
        if (copy_to_user(buffer, buf->start, actual_length - (buf->size - buf->cursor))) {
            mutex_unlock(&buf->lock);
            return -EFAULT;
        }
        buf->cursor = buf->cursor + actual_length - buf->size;
    } else {
        if (copy_to_user(buffer, buf->start + buf->cursor, actual_length)) {
            mutex_unlock(&buf->lock);
            return -EFAULT;
        }
        buf->cursor = buf->cursor + actual_length;
    }

    mutex_unlock(&buf->lock);
    return actual_length;
}

static ssize_t leftpad_write(struct file *file, const char *buffer, size_t length, loff_t * offset)
{
    struct buffer *buf = file->private_data;
    ssize_t actual_length;

    int written = 0;

    while (written < length) {

        if (mutex_lock_interruptible(&buf->lock)) {
            return -ERESTARTSYS;
        }

        while (buf->length == buf->size) {
            mutex_unlock(&buf->lock);
            if (file->f_flags & O_NONBLOCK) {
                return -EAGAIN;
            }
            if (wait_event_interruptible(buf->read_queue, buf->length != buf->size)) {
                return -ERESTARTSYS;
            }
            if (mutex_lock_interruptible(&buf->lock)) {
                return -ERESTARTSYS;
            }
        }

        actual_length = min(length, buf->size - buf->length);

        /* 3 cases (c = cursor, e = end of data, n = end of data after write):
         *     [ c e n ]
         *     [ n c e ]
         *     [ e n c ]
         */
        if (buf->cursor + buf->length + actual_length <= buf->size) {
            if (copy_from_user(buf->start + buf->cursor + buf->length, buffer + written, actual_length)) {
                mutex_unlock(&buf->lock);
                return -EFAULT;
            }
        } else if (buf->cursor + buf->length <= buf->size) {
            if (copy_from_user(buf->start + buf->cursor + buf->length, buffer + written, buf->size - buf->length)) {
                mutex_unlock(&buf->lock);
                return -EFAULT;
            }
            if (copy_from_user(buf->start, buffer + written + buf->size - buf->length, length - buf->size + buf->length)) {
                mutex_unlock(&buf->lock);
                return -EFAULT;
            }
        } else {
            if (copy_from_user(buf->start + buf->cursor + buf->length - buf->size, buffer + written, actual_length)) {
                mutex_unlock(&buf->lock);
                return -EFAULT;
            }
        }

        buf->length += actual_length;
        written += actual_length;

        wake_up_interruptible(&buf->read_queue);
    }

    mutex_unlock(&buf->lock);
    return actual_length;
}
