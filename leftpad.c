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


#define LEFTPAD_DEVICE_NAME "leftpad"
#define LEFTPAD_MAJOR 1337
#define LEFTPAD_WIDTH_MODULUS 1024
#define SUCCESS 0
#define FAILURE -1


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Spinale <spinalen@gmail.com>");
MODULE_DESCRIPTION("leftPad() implemented as a kernel module");
MODULE_SUPPORTED_DEVICE(LEFTPAD_DEVICE_NAME);


/* PARAMS */


static int leftpad_width = 32;
static int leftpad_fill = 32;
static int leftpad_buffer_size = 1024;

module_param(leftpad_width, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_width, "Lines are padded so that their width (not including EOL) is the residue class modulo 1024 of the value of this parameter.");
module_param(leftpad_fill, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_fill, "The residue class modulo 128 of the value of this parameter is used to pad lines shorter than leftpad_width.");
module_param(leftpad_buffer_size, int, 0000);
MODULE_PARM_DESC(leftpad_buffer_size, "Size of internal ring buffer.");


static size_t leftpad_get_width(void)
{
    return leftpad_width % LEFTPAD_WIDTH_MODULUS;
}

static char leftpad_get_fill(void)
{
    return leftpad_fill % 128;
}

static size_t leftpad_get_buffer_size(void)
{
    return leftpad_buffer_size;
}


/* STATE */


struct newline {
    size_t ix;
    struct newline *prev, *next;
};

struct buffer {
    wait_queue_head_t read_queue;
    struct mutex lock;
    char *start;
    size_t cursor, size, length;
    ssize_t padding_left;
    struct newline *head, *tail;
};

static int append_newline(size_t ix, struct buffer *buf)
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

static struct buffer *buffer_alloc(size_t size)
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

#ifdef LEFTPAD_DEBUG
static void buffer_show(struct buffer *buf)
{
    size_t i;
    struct newline *cur;
    char *str = kmalloc(buf->length + 1, GFP_KERNEL);

    for (i = 0; i < buf->length; i++) {
        str[i] = buf->start[buf->cursor + i % buf->size];
    }
    str[i] = 0;

    printk(KERN_INFO "Showing leftpad buffer at %p:\n", buf);
    printk(KERN_CONT "   padding_left: %zd\n", buf->padding_left);

    printk(KERN_CONT "   newlines:\n");
    for (cur = buf->head->next; cur != buf->tail; cur = cur->next) {
        printk(KERN_CONT "     +%zd\n", cur->ix - buf->cursor % buf->size);
    }

    printk(KERN_CONT "   contents: \"%s\"\n", str);
}
#endif


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

static int __init leftpad_init(void)
{
    size_t i;
    leftpad_padding = kmalloc(leftpad_get_width(), GFP_KERNEL);
    for (i = 0; i < leftpad_width; i++) {
        leftpad_padding[i] = leftpad_get_fill();
    }

    if (register_chrdev(LEFTPAD_MAJOR, "leftpad", &fops)) {
        return FAILURE;
    }

#ifdef LEFTPAD_DEBUG
    printk(KERN_INFO "Init leftpad: width=%zu, fill=ascii(%d), buffer_size=%zu\n",
            leftpad_get_width(), leftpad_get_fill(), leftpad_get_buffer_size());
#endif

    return SUCCESS;
}

static void __exit leftpad_exit(void)
{
    unregister_chrdev(LEFTPAD_MAJOR, "leftpad");
}


module_init(leftpad_init);
module_exit(leftpad_exit);


/* IMPL */


static int leftpad_open(struct inode *inode, struct file *file)
{
    struct buffer *buf = buffer_alloc(leftpad_get_buffer_size());
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
    size_t line_length, actual_length;
    int finished_line;
    ssize_t ret = 0;

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

    line_length = buf->head->next->ix - buf->cursor % leftpad_get_buffer_size();

    if (buf->padding_left == -1) {
        buf->padding_left = max((size_t) 0, leftpad_get_width() - line_length);
    }

    if (buf->padding_left >= length) {

        if (copy_to_user(buffer, leftpad_padding, length)) {
            ret = -EFAULT;
            goto cleanup;
        }
        buf->padding_left -= length;
        ret = length;
        goto cleanup;

    } else {

        if (copy_to_user(buffer, leftpad_padding, buf->padding_left)) {
            ret = -EFAULT;
            goto cleanup;
        }

        ret += buf->padding_left;
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
                ret = -EFAULT;
                goto cleanup;
            }
            if (copy_to_user(buffer, buf->start, actual_length - (buf->size - buf->cursor))) {
                ret = -EFAULT;
                goto cleanup;
            }
            buf->cursor = buf->cursor + actual_length - buf->size;
            length -= actual_length;
        } else {
            if (copy_to_user(buffer, buf->start + buf->cursor, actual_length)) {
                ret = -EFAULT;
                goto cleanup;
            }
            buf->cursor = buf->cursor + actual_length;
            length -= actual_length;
        }

        if (finished_line) {
            buf->head->next = buf->head->next->next;
            buf->padding_left = -1;
        }

        ret += actual_length;
    }

    cleanup:
        mutex_unlock(&buf->lock);
        printk(KERN_INFO "ret: %zd", ret);
        buffer_show(buf);
        return ret;
}

static ssize_t leftpad_write(struct file *file, const char *buffer, size_t length, loff_t * offset)
{
    size_t i;
    struct buffer *buf = file->private_data;
    ssize_t actual_length;
    ssize_t ret;

    if (mutex_lock_interruptible(&buf->lock)) {
        return -ERESTARTSYS;
    }

    if (buf->length + length > buf->size) {
        ret = -ENOBUFS;
        goto cleanup;
    }

    actual_length = min(length, buf->size - buf->length);

    /* 3 cases (c = cursor, e = end of data, n = end of data after write):
     *     [ c e n ]
     *     [ n c e ]
     *     [ e n c ]
     */
    if (buf->cursor + buf->length + actual_length <= buf->size) {
        if (copy_from_user(buf->start + buf->cursor + buf->length, buffer, actual_length)) {
            ret = -EFAULT;
            goto cleanup;
        }
    } else if (buf->cursor + buf->length <= buf->size) {
        if (copy_from_user(buf->start + buf->cursor + buf->length, buffer, buf->size - buf->length)) {
            ret = -EFAULT;
            goto cleanup;
        }
        if (copy_from_user(buf->start, buffer + buf->size - buf->length, length - buf->size + buf->length)) {
            ret = -EFAULT;
            goto cleanup;
        }
    } else {
        if (copy_from_user(buf->start + buf->cursor + buf->length - buf->size, buffer, actual_length)) {
            ret = -EFAULT;
            goto cleanup;
        }
    }

    for (i = 0; i < actual_length; i++) {
        if (buf->start[buf->cursor + buf->length + i % buf->size] == '\n') {
            append_newline(buf->cursor + buf->length + i % buf->size, buf);
        }
    }

    wake_up_interruptible(&buf->read_queue);

    buf->length += actual_length;
    ret = actual_length;

#ifdef LEFTPAD_DEBUG
    buffer_show(buf);
#endif

    cleanup:
        mutex_unlock(&buf->lock);
        return ret;
}
