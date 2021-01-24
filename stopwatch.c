#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/interrupt.h>

/*
  Kopirano iz Xilinxovog primera
*/

#define XIL_AXI_TIMER_TCSR_OFFSET	0x0
#define XIL_AXI_TIMER_TLR_OFFSET		0x4
#define XIL_AXI_TIMER_TCR_OFFSET		0x8

#define XIL_AXI_TIMER_TCSR1_OFFSET	0x10
#define XIL_AXI_TIMER_TLR1_OFFSET		0x14
#define XIL_AXI_TIMER_TCR1_OFFSET		0x18

#define XIL_AXI_TIMER_CSR_CASC_MASK	0x00000800
#define XIL_AXI_TIMER_CSR_ENABLE_ALL_MASK	0x00000400
#define XIL_AXI_TIMER_CSR_ENABLE_PWM_MASK	0x00000200
#define XIL_AXI_TIMER_CSR_INT_OCCURED_MASK 0x00000100
#define XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK 0x00000080
#define XIL_AXI_TIMER_CSR_ENABLE_INT_MASK 0x00000040
#define XIL_AXI_TIMER_CSR_LOAD_MASK 0x00000020
#define XIL_AXI_TIMER_CSR_AUTO_RELOAD_MASK 0x00000010
#define XIL_AXI_TIMER_CSR_EXT_CAPTURE_MASK 0x00000008
#define XIL_AXI_TIMER_CSR_EXT_GENERATE_MASK 0x00000004
#define XIL_AXI_TIMER_CSR_DOWN_COUNT_MASK 0x00000002
#define XIL_AXI_TIMER_CSR_CAPTURE_MODE_MASK 0x00000001

#define BUFF_SIZE 20
#define DRIVER_NAME "stopwatch"
#define DEVICE_NAME "timerstopwatch"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR ("Pilipovic");
MODULE_DESCRIPTION("Stopwatch Xiling AXI Timer driver example.");

struct timer_info {
	unsigned long mem_start;
	unsigned long mem_end;
	void __iomem *base_addr;
	int irq_num;
};

dev_t my_dev_id;
static struct class *my_class;
static struct device *my_device;
static struct cdev *my_cdev;
static struct timer_info *tp = NULL;

unsigned int startAt0 = 0;
unsigned int startAt1 = 0;

static void setup_and_start_timer(unsigned int startAt0, unsigned int startAt1);
static int timer_probe(struct platform_device *pdev);
static int timer_remove(struct platform_device *pdev);
int stopwatch_open(struct inode *pinode, struct file *pfile);
int stopwatch_close(struct inode *pinode, struct file *pfile);
ssize_t stopwatch_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t stopwatch_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int __init timer_init(void);
static void __exit timer_exit(void);

static irqreturn_t xilaxitimer_isr(int irq,void*dev_id); // test function

unsigned int raw0 = 0;
unsigned int raw1 = 0;
int secondPass = 1;

struct file_operations my_fops =
{
	.owner = THIS_MODULE,
	.open = stopwatch_open,
	.read = stopwatch_read,
	.write = stopwatch_write,
	.release = stopwatch_close,
};

static struct of_device_id timer_of_match[] = {
	{ .compatible = "xlnx,xps-timer-1.00.a", },
	{ /* end of list */ },
};

static struct platform_driver timer_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table	= timer_of_match,
	},
	.probe		= timer_probe,
	.remove		= timer_remove,
};

MODULE_DEVICE_TABLE(of, timer_of_match);

static irqreturn_t xilaxitimer_isr(int irq,void*dev_id)	{
	unsigned int data = 0;


	printk(KERN_INFO "xilaxitimer_isr: Timer0 Overflow.\n");
	// Clear Interrupt
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data | XIL_AXI_TIMER_CSR_INT_OCCURED_MASK,
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR1_OFFSET);

	if(data == 0xffffffff){
		printk(KERN_INFO "xilaxitimer_isr: Stopwatch overflow. Reset and stop.\n");
		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK), tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	}

	return IRQ_HANDLED;
}

static void setup_and_start_timer(unsigned int startAt0, unsigned int startAt1) {

	unsigned int data = 0;

	raw0 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
	raw1 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR1_OFFSET);
	printk(KERN_WARNING "TCRs: %u | %u\n", raw0, raw1);

	// Disable timer/counter while configuration is in progress
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK),
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK),
			tp->base_addr + XIL_AXI_TIMER_TCSR1_OFFSET);

	// Set initial value in load registers
	iowrite32(startAt0, tp->base_addr + XIL_AXI_TIMER_TLR_OFFSET);
	iowrite32(startAt1, tp->base_addr + XIL_AXI_TIMER_TLR1_OFFSET);

	// Load initial value into counters from load registers
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data | XIL_AXI_TIMER_CSR_LOAD_MASK,
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data & ~(XIL_AXI_TIMER_CSR_LOAD_MASK),
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

	// Do the same for Timer1
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data | XIL_AXI_TIMER_CSR_LOAD_MASK,
			tp->base_addr + XIL_AXI_TIMER_TCSR1_OFFSET);

	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data & ~(XIL_AXI_TIMER_CSR_LOAD_MASK),
			tp->base_addr + XIL_AXI_TIMER_TCSR1_OFFSET);

	// Enable interrupts and autoreload, rest should be zero
	iowrite32(XIL_AXI_TIMER_CSR_ENABLE_INT_MASK | XIL_AXI_TIMER_CSR_CASC_MASK,
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

			raw0 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
			raw1 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR1_OFFSET);
			printk(KERN_WARNING "TCRs: %u | %u\n", raw0, raw1);

	// Start Timer bz setting enable signal

	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data | XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK,
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);


}

static int timer_probe(struct platform_device *pdev){
	struct resource *r_mem;
	int rc = 0;

	// Get phisical register adress space from device tree
	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r_mem) {
		printk(KERN_ALERT "xilaxitimer_probe: Failed to get reg resource\n");
		return -ENODEV;
	}

	// Get memory for structure timer_info
	tp = (struct timer_info *) kmalloc(sizeof(struct timer_info), GFP_KERNEL);
	if (!tp) {
		printk(KERN_ALERT "xilaxitimer_probe: Could not allocate timer device\n");
		return -ENOMEM;
	}

	// Put phisical adresses in timer_info structure
	tp->mem_start = r_mem->start;
	tp->mem_end = r_mem->end;

	// Reserve that memory space for this driver
	if (!request_mem_region(tp->mem_start,tp->mem_end - tp->mem_start + 1,	DEVICE_NAME))
	{
		printk(KERN_ALERT "xilaxitimer_probe: Could not lock memory region at %p\n",(void *)tp->mem_start);
		rc = -EBUSY;
		goto error1;
	}

	// Remap phisical to virtual adresses
	tp->base_addr = ioremap(tp->mem_start, tp->mem_end - tp->mem_start + 1);
	if (!tp->base_addr) {
		printk(KERN_ALERT "xilaxitimer_probe: Could not allocate memory\n");
		rc = -EIO;
		goto error2;
	}

	// Get interrupt number from device tree
	tp->irq_num = platform_get_irq(pdev, 0);
	if (!tp->irq_num) {
		printk(KERN_ALERT "xilaxitimer_probe: Failed to get irq resource\n");
		rc = -ENODEV;
		goto error2;
	}

	// Reserve interrupt number for this driver
	if (request_irq(tp->irq_num, xilaxitimer_isr, 0, DEVICE_NAME, NULL)) {
		printk(KERN_ERR "xilaxitimer_probe: Cannot register IRQ %d\n", tp->irq_num);
		rc = -EIO;
		goto error3;

	}
	else {
		printk(KERN_INFO "xilaxitimer_probe: Registered IRQ %d\n", tp->irq_num);
	}

	printk(KERN_NOTICE "xilaxitimer_probe: Timer platform driver registered\n");
	return 0;//ALL OK

error3:
	iounmap(tp->base_addr);
error2:
	release_mem_region(tp->mem_start, tp->mem_end - tp->mem_start + 1);
	kfree(tp);
error1:
	return rc;
}

static int timer_remove(struct platform_device *pdev){
	// Disable timer
	unsigned int data=0;
	data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK),
			tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
	// Free resources taken in probe
	free_irq(tp->irq_num, NULL);
	iowrite32(0, tp->base_addr);
	iounmap(tp->base_addr);
	release_mem_region(tp->mem_start, tp->mem_end - tp->mem_start + 1);
	kfree(tp);
	printk(KERN_WARNING "xilaxitimer_remove: Timer driver removed\n");
	return 0;
}

int stopwatch_open(struct inode *pinode, struct file *pfile) {
	//printk(KERN_INFO "Succesfully opened timer\n");
	return 0;
}

int stopwatch_close(struct inode *pinode, struct file *pfile) {
	//printk(KERN_INFO "Succesfully closed timer\n");
	return 0;
}

ssize_t stopwatch_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset) {

	long int len = 0;
	int ret = 0;
	unsigned int rawCountLSB;
	unsigned int rawCountMSB;
	unsigned int rawCount;
	unsigned int microseconds;
	unsigned int milliseconds;
	unsigned int seconds;
	unsigned int minutes;
	unsigned int hours;
	char output[20];

	secondPass = !secondPass;
	if (secondPass == 1) return 0;

	rawCountLSB = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
	rawCountMSB = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR1_OFFSET);
	//rawCount = rawCountLSB + (0x100000000*rawCountMSB);  // long long impossible, system is 32bit

	microseconds = (rawCountLSB/100 + rawCountMSB*0x28F5C29)%1000;
	milliseconds = (rawCountLSB/100000 + rawCountMSB*0xA7C6)%1000;
	seconds = (rawCountLSB/100000000 + rawCountMSB*0x2B)%60;
	minutes = ((rawCountLSB/100000000 + rawCountMSB*0x2B)/60)%60;
	hours = ((rawCountLSB/100000000 + rawCountMSB*0x2B)/60)/60;
	// example output: 20:59:59.999,999
	len = snprintf(output, 20, "%u:%u:%u.%u,%u\n", hours, minutes, seconds, milliseconds, microseconds);
	raw0 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
	raw1 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR1_OFFSET);
	printk(KERN_WARNING "TCRs: %u | %u\n", raw0, raw1);

	ret = copy_to_user(buffer, output, len);

  if (ret)
    return -EFAULT;

		return len;
}

ssize_t stopwatch_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset) {
	char buff[BUFF_SIZE];
	int ret = 0;
	unsigned int data = 0;
	ret = copy_from_user(buff, buffer, length);
	if(ret)
		return -EFAULT;
	buff[length-1] = '\0';
	printk(KERN_WARNING "TimerStopwatch: BUFF: %s\n", buff);
	if(!strcmp(buff,"start")){

		setup_and_start_timer(startAt0, startAt1);
		printk(KERN_WARNING "TimerStopwatch: Stopwatch (re)started!\n");
	}else if(!strcmp(buff,"stop")){

		startAt0 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
		startAt1 = ioread32(tp->base_addr + XIL_AXI_TIMER_TCR_OFFSET);
		// stop timer after reading TCRs, values will be slightly off by approx 2xinstruction cycle (reading two registers), better to read faster changing one first
		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK), tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		printk(KERN_WARNING "TimerStopwatch: Stopwatch paused!\n");

	}else if(!strcmp(buff,"reset")){

		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data & ~(XIL_AXI_TIMER_CSR_ENABLE_TMR_MASK), tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		startAt0 = 0;
		startAt1 = 0;

		// Set initial value in load register
		iowrite32(startAt0, tp->base_addr + XIL_AXI_TIMER_TLR_OFFSET);
		iowrite32(startAt1, tp->base_addr + XIL_AXI_TIMER_TLR1_OFFSET);

		// Load initial value into counter from load register
		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data | XIL_AXI_TIMER_CSR_LOAD_MASK,
				tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data & ~(XIL_AXI_TIMER_CSR_LOAD_MASK),
				tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);

		// Do the same for Timer1
		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data | XIL_AXI_TIMER_CSR_LOAD_MASK,
				tp->base_addr + XIL_AXI_TIMER_TCSR1_OFFSET);

		data = ioread32(tp->base_addr + XIL_AXI_TIMER_TCSR_OFFSET);
		iowrite32(data & ~(XIL_AXI_TIMER_CSR_LOAD_MASK),
				tp->base_addr + XIL_AXI_TIMER_TCSR1_OFFSET);

		printk(KERN_WARNING "TimerStopwatch: Stopwatch reset!\n");

	}else{
		printk(KERN_WARNING "xilaxitimer_write: Wrong format, expected start, stop, reset!\n");
	}
	return length;
}

static int __init timer_init(void){
	int ret = 0;


	ret = alloc_chrdev_region(&my_dev_id, 0, 1, DRIVER_NAME);
	if (ret){
		printk(KERN_ERR "xilaxitimer_init: Failed to register char device\n");
		return ret;
	}
	printk(KERN_INFO "xilaxitimer_init: Char device region allocated\n");

	my_class = class_create(THIS_MODULE, "timer_class");
	if (my_class == NULL){
		printk(KERN_ERR "xilaxitimer_init: Failed to create class\n");
		goto fail_0;
	}
	printk(KERN_INFO "xilaxitimer_init: Class created\n");

	my_device = device_create(my_class, NULL, my_dev_id, NULL, DRIVER_NAME);
	if (my_device == NULL){
		printk(KERN_ERR "xilaxitimer_init: Failed to create device\n");
		goto fail_1;
	}
	printk(KERN_INFO "xilaxitimer_init: Device created\n");

	my_cdev = cdev_alloc();
	my_cdev->ops = &my_fops;
	my_cdev->owner = THIS_MODULE;
	ret = cdev_add(my_cdev, my_dev_id, 1);
	if (ret)
	{
		printk(KERN_ERR "xilaxitimer_init: Failed to add cdev\n");
		goto fail_2;
	}
	printk(KERN_INFO "xilaxitimer_init: Cdev added\n");
	printk(KERN_NOTICE "xilaxitimer_init: Hello world\n");

	return platform_driver_register(&timer_driver);

fail_2:
	device_destroy(my_class, my_dev_id);
fail_1:
	class_destroy(my_class);
fail_0:
	unregister_chrdev_region(my_dev_id, 1);
	return -1;
}

static void __exit timer_exit(void){
	platform_driver_unregister(&timer_driver);
	cdev_del(my_cdev);
	device_destroy(my_class, my_dev_id);
	class_destroy(my_class);
	unregister_chrdev_region(my_dev_id,1);
	printk(KERN_INFO "xilaxitimer_exit: Goodbye, cruel world\n");
}


module_init(timer_init);
module_exit(timer_exit);
