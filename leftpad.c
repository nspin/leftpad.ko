#include <linux/init.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#define LEFT_PAD_WIDTH_MODULUS 1024

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nick Spinale <spinalen@gmail.com>");
MODULE_DESCRIPTION("leftPad() implemented as a kernel module");
MODULE_SUPPORTED_DEVICE("leftpad");


/* PARAMS */


static short leftpad_width = 80;
static short leftpad_fill = 32;

module_param(leftpad_width, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_width, "Lines are padded so that their width (including EOL) is the residue class modulo 1024 of the value of this parameter.");
module_param(leftpad_fill, short, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(leftpad_fill, "The residue class modulo 128 of the value of this parameter is used to pad lines shorter than leftpad_width.");


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


/* TODO */


/* INIT+EXIT */


static int __init leftpad_init(void)
{
    printk(KERN_INFO "Init leftpad with width=%i and fill=ascii(%i)\n", leftpad_get_width(), leftpad_get_fill());
    return 0;
}

static void __exit leftpad_exit(void)
{
    printk(KERN_INFO "Exit leftpad\n");
}

module_init(leftpad_init);
module_exit(leftpad_exit);
