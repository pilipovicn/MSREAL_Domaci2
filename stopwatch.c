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

static void setup_and_start_timer();
static int timer_probe(struct platform_device *pdev);
static int timer_remove(struct platform_device *pdev);
int timer_open(struct inode *pinode, struct file *pfile);
int timer_close(struct inode *pinode, struct file *pfile);
ssize_t timer_read(struct file *pfile, char __user *buffer, size_t length, loff_t *offset);
ssize_t timer_write(struct file *pfile, const char __user *buffer, size_t length, loff_t *offset);
static int __init timer_init(void);
static void __exit timer_exit(void);
