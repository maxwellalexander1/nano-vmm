#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/ioctl.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Max");
MODULE_DESCRIPTION("Nano-VMM Virtual Accelerator FIFO Character Device Driver");
MODULE_VERSION("1.0");


#define VMM_IOC_MAGIC        'V'
#define VMM_IOC_RESET_FIFO   _IO(VMM_IOC_MAGIC, 0)
#define VMM_IOC_GET_COUNT    _IOR(VMM_IOC_MAGIC, 1, uint32_t)

#define FIFO_CAPACITY 64

static uint32_t hw_fifo[FIFO_CAPACITY];
static uint32_t fifo_head  = 0;
static uint32_t fifo_tail  = 0;
static uint32_t fifo_count = 0;
static uint32_t total_ops  = 0;

static DEFINE_SPINLOCK(fifo_lock);
static DECLARE_WAIT_QUEUE_HEAD(read_wq);


static ssize_t vmm_dev_write(struct file *file, const char __user *ubuf,
                             size_t len, loff_t *ppos)
{
    uint32_t input_val, processed_val;
    unsigned long flags;

    if (len != sizeof(uint32_t))
        return -EINVAL;

    if (copy_from_user(&input_val, ubuf, sizeof(uint32_t)))
        return -EFAULT;

    processed_val = (input_val ^ 0x5A5A5A5AU);

    spin_lock_irqsave(&fifo_lock, flags);

    if (fifo_count >= FIFO_CAPACITY) {
        spin_unlock_irqrestore(&fifo_lock, flags);
        return -ENOSPC; /* Hardware FIFO Full */
    }

    hw_fifo[fifo_head] = processed_val;
    fifo_head = (fifo_head + 1) % FIFO_CAPACITY;
    fifo_count++;
    total_ops++;

    spin_unlock_irqrestore(&fifo_lock, flags);

    wake_up_interruptible(&read_wq);

    return sizeof(uint32_t);
}

static ssize_t vmm_dev_read(struct file *file, char __user *ubuf,
                            size_t len, loff_t *ppos)
{
    uint32_t out_val;
    unsigned long flags;
    int ret;

    if (len != sizeof(uint32_t))
        return -EINVAL;

 
    ret = wait_event_interruptible(read_wq, READ_ONCE(fifo_count) > 0);
    if (ret)
        return -ERESTARTSYS; 

    spin_lock_irqsave(&fifo_lock, flags);

    if (fifo_count == 0) {
        spin_unlock_irqrestore(&fifo_lock, flags);
        return -EAGAIN;
    }

    out_val = hw_fifo[fifo_tail];
    fifo_tail = (fifo_tail + 1) % FIFO_CAPACITY;
    fifo_count--;

    spin_unlock_irqrestore(&fifo_lock, flags);


    if (copy_to_user(ubuf, &out_val, sizeof(uint32_t)))
        return -EFAULT;

    return sizeof(uint32_t);
}


static long vmm_dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    unsigned long flags;
    uint32_t count_snapshot;

    switch (cmd) {
    case VMM_IOC_RESET_FIFO:
        spin_lock_irqsave(&fifo_lock, flags);
        fifo_head = 0;
        fifo_tail = 0;
        fifo_count = 0;
        spin_unlock_irqrestore(&fifo_lock, flags);
        pr_info("[vmm_kmod] Hardware FIFO reset via ioctl\n");
        return 0;

    case VMM_IOC_GET_COUNT:
        spin_lock_irqsave(&fifo_lock, flags);
        count_snapshot = total_ops;
        spin_unlock_irqrestore(&fifo_lock, flags);

        if (copy_to_user((uint32_t __user *)arg, &count_snapshot, sizeof(uint32_t)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations vmm_fops = {
    .owner          = THIS_MODULE,
    .read           = vmm_dev_read,
    .write          = vmm_dev_write,
    .unlocked_ioctl = vmm_dev_ioctl,
};

static struct miscdevice vmm_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "vmm_accel",
    .fops  = &vmm_fops,
    .mode  = 0666, /* Allow non-root test suite to open /dev/vmm_accel */
};

static int __init vmm_kmod_init(void)
{
    int ret = misc_register(&vmm_misc_device);
    if (ret) {
        pr_err("[vmm_kmod] Failed to register /dev/vmm_accel\n");
        return ret;
    }
    pr_info("[vmm_kmod] Registered /dev/vmm_accel virtual accelerator driver\n");
    return 0;
}

static void __exit vmm_kmod_exit(void)
{
    misc_deregister(&vmm_misc_device);
    pr_info("[vmm_kmod] Unloaded /dev/vmm_accel driver (Total ops: %u)\n", total_ops);
}

module_init(vmm_kmod_init);
module_exit(vmm_kmod_exit);