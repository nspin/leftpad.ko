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
#define FAILURE -1


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Spinale <spinalen@gmail.com>");
MODULE_DESCRIPTION("leftPad() implemented as a kernel module");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);


/* PARAMS */


static short leftpad_width = 80;
static short leftpad_fill = 32;
static unsigned long leftpad_buffer_size = 1024;

module_param(leftpad_width, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_width, "Lines are padded so that their width (not including EOL) is the residue class modulo 1024 of the value of this parameter.");
module_param(leftpad_fill, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_fill, "The residue class modulo 128 of the value of this parameter is used to pad lines shorter than leftpad_width.");
module_param(leftpad_buffer_size, ulong, 0000);
MODULE_PARM_DESC(leftpad_buffer_size, "Size of internal ring buffer.");


static unsigned long leftpad_get_width(void)
{
    return leftpad_width % LEFT_PAD_WIDTH_MODULUS;
}

static char leftpad_get_fill(void)
{
    return leftpad_fill % 128;
}


/* STATE */


struct newline {
    unsigned long ix;
    struct newline *prev, *next;
};

struct buffer {
    wait_queue_head_t read_queue;
    struct mutex lock;
    char *start;
    unsigned long size, cursor, length, padding_left;
    struct newline *head, *tail;
};

static int append_newline(unsigned long ix, struct buffer *buf)
{
    struct newline *nl = kmalloc(sizeof(*nl), GFP_KERNEL);
    if (unlikely(!nl)) {
        return FAILURE;
    }
    nl->ix = ix;
    nl->prev = buf->tail->prev;
    nl->next = buf->tail;
    buf->tail->prev->next = nl;
    buf->tail->prev = nl;
    return SUCCESS;
}

static struct buffer *buffer_alloc(unsigned long size)
{
    struct buffer *buf = kmalloc(sizeof(*buf), GFP_KERNEL);
    if (unlikely(!buf)) {
        return NULL;
    }

    buf->start = kmalloc(size, GFP_KERNEL);
    if (unlikely(!buf->start)) {
        kfree(buf);
        return NULL;
    }

    init_waitqueue_head(&buf->read_queue);
    mutex_init(&buf->lock);

    buf->cursor = 0;
    buf->size = size;
    buf->length = 0;
    buf->padding_left = -1;

    buf->head = kmalloc(sizeof(*buf->head), GFP_KERNEL);
    if (unlikely(!buf->head)) {
        return NULL;
    }

    buf->tail = kmalloc(sizeof(*buf->tail), GFP_KERNEL);
    if (unlikely(!buf->tail)) {
        return NULL;
    }

    buf->head->prev = NULL;
    buf->head->ix = -1;
    buf->head->next = buf->tail;
    buf->tail->prev = buf->head;
    buf->tail->ix = -1;
    buf->tail->next = NULL;

    return buf;
}

static void buffer_free(struct buffer *buf)
{
    struct newline *cur;
    for (cur = buf->head->next; cur != NULL; cur = cur->next) {
        kfree(cur->prev);
    }
    kfree(buf->tail);
    kfree(buf->start);
    kfree(buf);
}

static void buffer_show(struct buffer *buf)
{
    int i;
    char *str = kmalloc(buf->length + 1, GFP_KERNEL);

    for (i = 0; i < buf->length; i++) {
        str[i] = buf->start[buf->cursor + i % buf->size];
    }
    str[i] = 0;

    printk(KERN_INFO "Showing leftpad buffer at %p:\n", buf);
    printk(KERN_CONT "   padding_left: %lu\n", buf->padding_left);
    printk(KERN_CONT "   contents: \"%s\"\n", str);
}


/* CORE */


static char *leftpad_padding;

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
    int i;
    leftpad_padding = kmalloc(leftpad_get_width(), GFP_KERNEL);
    for (i = 0; i < leftpad_width; i++) {
        leftpad_padding[i] = leftpad_fill;
    }

    misc_register(&leftpad_miscdevice);

    printk(KERN_INFO "Init leftpad: width=%lu, fill=ascii(%i), buffer_size=(%lu)",
            leftpad_get_width(), leftpad_get_fill(), leftpad_buffer_size);

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
    struct buffer *buf = buffer_alloc(leftpad_buffer_size);
    if (unlikely(!buf)) {
        return -ENOMEM;
    }
    file->private_data = buf;
    try_module_get(THIS_MODULE);
    return SUCCESS;
}

static int leftpad_release(struct inode *inode, struct file *file)
{
    module_put(THIS_MODULE);
	buffer_free(file->private_data);
    return SUCCESS;
}

static ssize_t leftpad_read(struct file *file, char *buffer, size_t length, loff_t * offset)
{
    struct buffer *buf = file->private_data;
    size_t actual_length;
    unsigned long line_length;
    int finished_line;
    ssize_t result;
    printk("read size: %zu", length);

    if (mutex_lock_interruptible(&buf->lock)) {
        return -ERESTARTSYS;
    }

    while (buf->head->next == buf->tail) {
        mutex_unlock(&buf->lock);
        if (file->f_flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        if (wait_event_interruptible(buf->read_queue, buf->head->next != buf->tail)) {
            return -ERESTARTSYS;
        }
        if (mutex_lock_interruptible(&buf->lock)) {
            return -ERESTARTSYS;
        }
    }

    line_length = buf->head->next->ix - buf->cursor % leftpad_buffer_size;

    printk(KERN_INFO "padding_left = %zu", buf->padding_left);
    if (buf->padding_left == -1) {
        buf->padding_left = max((unsigned long) 0, leftpad_get_width() - line_length);
    }
    printk(KERN_INFO "padding_left = %zu", buf->padding_left);

    if (buf->padding_left >= length) {

        if (copy_to_user(buffer, leftpad_padding, length)) {
            result = -EFAULT;
            goto ret;
        }
        buf->padding_left -= length;
        result = length;
        goto ret;

    } else {

        if (copy_to_user(buffer, leftpad_padding, buf->padding_left)) {
            result = -EFAULT;
            goto ret;
        }

        buffer += buf->padding_left;
        length -= buf->padding_left;
        buf->padding_left = 0;

        if (length >= line_length + 1) {
            actual_length = line_length + 1;
            finished_line = 1;
        } else {
            actual_length = length;
            finished_line = 0;
        }

        if (buf->cursor + actual_length > buf->size) {
            if (copy_to_user(buffer, buf->start + buf->cursor, buf->size - buf->cursor)) {
                result = -EFAULT;
                goto ret;
            }
            if (copy_to_user(buffer, buf->start, actual_length - (buf->size - buf->cursor))) {
                result = -EFAULT;
                goto ret;
            }
            buf->cursor = buf->cursor + actual_length - buf->size;
        } else {
            if (copy_to_user(buffer, buf->start + buf->cursor, actual_length)) {
                result = -EFAULT;
                goto ret;
            }
            buf->cursor = buf->cursor + actual_length;
        }

        if (finished_line) {
            buf->head->next = buf->head->next->next;
            buf->padding_left = -1;
        }

        result = actual_length;
    }

    ret:
        mutex_unlock(&buf->lock);
        return result;
}

static ssize_t leftpad_write(struct file *file, const char *buffer, size_t length, loff_t * offset)
{
    int i;
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

        for (i = 0; i < actual_length; i++) {
            if (buf->start[buf->cursor + i % buf->size] == '\n') {
                append_newline(i % buf->size, buf);
            }
        }

        buf->length += actual_length;
        written += actual_length;

        wake_up_interruptible(&buf->read_queue);
    }

    buffer_show(buf);

    mutex_unlock(&buf->lock);
    return actual_length;
}
