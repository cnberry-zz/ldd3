#include <linux/module.h> 
#include <linux/printk.h> 
#include <linux/kobject.h> 
#include <linux/sysfs.h> 
#include <linux/init.h> 
#include <linux/fs.h> 
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/time64.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>

static struct kobject *example_kobject;
static int foo;

static ssize_t foo_show(struct kobject *kobj, struct kobj_attribute *attr,
                      char *buf)
{
        return sprintf(buf, "%d\n", foo);
}

static ssize_t foo_store(struct kobject *kobj, struct kobj_attribute *attr,
                      const char *buf, size_t count)
{
        sscanf(buf, "%du", &foo);
        return count;
}

static struct kobj_attribute foo_attribute =__ATTR_RW(foo);

static int __init example_init (void)
{
        int status = 0;

        printk("example: Module init entered \n");

        example_kobject = kobject_create_and_add("example",kernel_kobj);
                                                 
        if(!example_kobject)
                return -ENOMEM;

        status = sysfs_create_file(example_kobject, &foo_attribute.attr);
        if (status) {
                pr_debug("failed to create the foo file in /sys/kernel/example \n");
        }

        return status;
}

static void __exit example_exit (void)
{
        printk("example: Module un initialized successfully \n");
        kobject_put(example_kobject);
}

module_init(example_init);
module_exit(example_exit);

MODULE_AUTHOR("Chris Berry <cnberry@gmail.com");
MODULE_LICENSE("GPL");
