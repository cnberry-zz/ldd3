/*
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 * Copyright (C) 2011 Vigith Maurice
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/mutex.h>	/* mutex_init() */
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>	/* copy_*_user */
#include <asm/io.h>

#define REG_BASE 0xFED940000
#define REG_MAX  0xFED94FFFF
		
struct my_dev {
	struct mutex mutex;     /* mutual exclusion semaphore     */
	void __iomem *regs;
	struct resource *mem;
	unsigned long buffer;
	unsigned long volatile head;
	volatile unsigned long tail;
	struct cdev cdev;	  /* Char device structure		*/
	
};


/* file scoped globals */
static int my_major =   0;
static int my_minor =   0;
static struct my_dev *q_dev;	

/*
 * Atomicly increment an index into dev->buffer
 */
static inline void circ_incr_bp(volatile unsigned long *index, const unsigned long *buf, int delta)
{
	unsigned long new = *index + delta;
	barrier();  /* Don't optimize these two together */
	*index = (new >= (*buf + PAGE_SIZE)) ? *buf : new;
}

/*
 * Interrupt handler 
 */
irqreturn_t irq_handler(int irq, void *this) {

	struct my_dev *dev = (struct my_dev *) this;
	struct timeval tv;
	int written, status = 0;

	// get status
	status = ioread32(dev->regs);
	
	do_gettimeofday(&tv);
	
   	/* Write a 16 byte record. Assume PAGE_SIZE is a multiple of 16 */
	written = sprintf((char *)dev->head,"%08u.%06u\n",(int)(tv.tv_sec % 100000000), (int)(tv.tv_usec));
	printk(KERN_INFO "scullq: interrupt at with status %x\n", status);

	circ_incr_bp(&dev->head, &dev->buffer,written);

	// read status with : int status = ioread32(dev->regs+offset);
	// check status with : if (status & 0x0000_0001)
	// return IRQ_NONE;
	// else do_work
	// clear status with : iowrite32(dev->regs+offset, status & 0xffff_fff7)
	// return IRQ_HANDLED;

	return IRQ_NONE;
}



/*
 * Open and close
 */

int my_open(struct inode *inode, struct file *filp)
{
	struct my_dev *dev; /* device information */

	dev = container_of(inode->i_cdev, struct my_dev, cdev);
	filp->private_data = dev; /* for other methods */

	return 0;          /* success */
}

int my_release(struct inode *inode, struct file *filp)
{
	return 0;
}

/*
 * Data management: read and write
 */

ssize_t my_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct my_dev *dev = filp->private_data;

	/* count0 is the number of readable data bytes */
	int count0 = dev->head - dev->tail;

	if (count0 < 0) /* wrapped */
		count0 = dev->buffer + PAGE_SIZE - dev->tail;
	if (count0 < count) count = count0;

	if (copy_to_user(buf, (char *)dev->tail, count))
		return -EFAULT;

	circ_incr_bp (&(dev->tail), &(dev->buffer), count);

	return count;

}

ssize_t my_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{

	return count;
}


struct file_operations my_fops = {
	.owner =    THIS_MODULE,
	.read =     my_read,
	.write =    my_write,
	.open =     my_open,
	.release =  my_release,
};

/*
 * Finally, the module stuff
 */

/*
 * The cleanup function is used to handle initialization failures as well.
 * Thefore, it must be careful to work correctly even if some of the items
 * have not been initialized
 */
void my_cleanup_module(void)
{
	dev_t devno = MKDEV(my_major, my_minor);

	/* Get rid of our char dev entries */
	if (q_dev) {
 		free_irq (19, q_dev);
		free_page(q_dev->buffer);
		release_mem_region(REG_BASE,REG_MAX-REG_BASE);
		iounmap(q_dev->regs);
		cdev_del(&(q_dev->cdev));
		kfree(q_dev);
	}

	/* cleanup_module is never called if registering failed */
	unregister_chrdev_region(devno, 1);

	printk(KERN_INFO "scullq: module unloaded\n");
}


int my_init_module(void)
{
	int result, err;
	dev_t devno = 0;

	result = alloc_chrdev_region(&devno, my_minor, 1,"scullq");
	my_major = MAJOR(devno);

	if (result < 0) {
		printk(KERN_WARNING "scullq: can't get major %d\n", my_major);
		return result;
	}

	q_dev = kmalloc(sizeof(struct my_dev), GFP_KERNEL);
	if (!q_dev) {
		result = -ENOMEM;
		goto fail;  /* Make this more graceful */
	}
	memset(q_dev, 0, sizeof(struct my_dev));

	/* setup device structure and cdev */
	mutex_init(&(q_dev->mutex));
	q_dev->buffer = __get_free_page(GFP_KERNEL);
	q_dev->head = q_dev->tail = q_dev->buffer;
	
	cdev_init(&q_dev->cdev, &my_fops);
	q_dev->cdev.owner = THIS_MODULE;
	q_dev->cdev.ops = &my_fops; 
	err = cdev_add (&(q_dev->cdev), devno, 1);

	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scullq", err);

	/* request fake iomem region and ioremap it */
	q_dev->mem = request_mem_region(REG_BASE,REG_MAX-REG_BASE,"scullq");
	if (!q_dev->mem)
    		printk(KERN_INFO "squllq: cant get iomem region\n");

	q_dev->regs = ioremap(q_dev->mem->start,REG_MAX-REG_BASE);
	if (!q_dev->regs)
    		printk(KERN_INFO "squllq: cant ioremap mem region\n");
	
	/* request irq */
 	result = request_irq (19, irq_handler, IRQF_SHARED, "scullq", q_dev);
  	if (result)
    		printk(KERN_INFO "squllq: cant get shared interrupt %d\n",19);

	printk(KERN_INFO "scullq: module loaded successfully\n");
	return 0; /* succeed */

  fail:
	my_cleanup_module();
	return result;
}

module_init(my_init_module);
module_exit(my_cleanup_module);

MODULE_AUTHOR("Chris Berry");
MODULE_LICENSE("Dual BSD/GPL");
