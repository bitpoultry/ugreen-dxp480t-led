#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/timer.h>
#include <linux/dmi.h>
#include <misc/ugreen_product.h>

#define SATA_SWITCH	"sata_sw"
#define BEEPER		"beeper"
#define DELAY_VALUE	200 //  ms



static struct delayed_work beeper_delay_work;
struct timer_list beeper_timer;
unsigned int timerStatus;
unsigned long delay_off;
unsigned long delay_on;
unsigned long g_count;

struct devinfo{
	const char *name;
	int shift; /* bit <<  shift*/
	u32 phyaddr;
	void __iomem *base;
};
struct devinfo sataInfo[]={
	{.name="SATA1", .phyaddr=0XFD6E0B00, .shift=1, .base=NULL},
	{.name="SATA2", .phyaddr=0XFD6E0AC0, .shift=1, .base=NULL},
};
	/* 
0xFD6E07E0
MMIO地址0xFD6E07E0写 0x84000201 蜂鸣器就响，写 0x84000200 蜂鸣器就停止响*/
struct devinfo beeperInfo = {
	.name="BEEPER",
	.phyaddr=0XFD6E07E0,//0XFD6E08B0, modify for dxp480T plus
	.shift=0,
	.base=NULL,
};

static u32 get_value(const volatile void __iomem *addr)
{
	return __raw_readl(addr);
}
static void set_value(u32 val,  volatile void __iomem *addr)
{
	__raw_writel(val,addr);
}

static void hwinit(void)
{
	int i;

//	for(i=0; i < sizeof(sataInfo)/sizeof(sataInfo[0]); i++){
//		sataInfo[i].base = ioremap(sataInfo[i].phyaddr, 4);
//	}

	beeperInfo.base = ioremap(beeperInfo.phyaddr, 4);
}

static void hw_unioremap(void)
{
	int i;
	for(i=0; i < sizeof(sataInfo)/sizeof(sataInfo[0]); i++){
		if(sataInfo[i].base != NULL){
			iounmap(sataInfo[i].base);
			sataInfo[i].base = NULL;
		}
	}
}

static ssize_t sata_satate_read(struct file *file,
					char __user *usr_buf,
					size_t size, loff_t *ppos)
{
	unsigned int val;
	unsigned int i;
	unsigned int cnt = 0;
	char tmpbuf[128];

	if (*ppos != 0)
		return 0;

	memset(tmpbuf, 0, sizeof(tmpbuf));

	for(i=0; i < sizeof(sataInfo)/sizeof(sataInfo[0]); i++){
		if(sataInfo[i].base != NULL){
			val = (get_value(sataInfo[i].base)) & (1 << sataInfo[i].shift);
			cnt += snprintf(tmpbuf + cnt, sizeof(tmpbuf) - cnt, "%s:\t%s\n", sataInfo[i].name, val ? "OFF" : "ON");
		}
	}

	return simple_read_from_buffer(usr_buf, size, ppos, tmpbuf, cnt);

}
static void beeper_delay_work_func(struct work_struct *work)
{
	int val;
	void __iomem *addr = beeperInfo.base;

	val = get_value(addr);
	val &= ~(7<<8);
	val |= (1<<9);
	set_value((val & (~0x1)), addr);
}


static void set_beeper(int dat, unsigned long delay)
{
	int val;
	void __iomem *addr = beeperInfo.base;

	val = get_value(addr);
	val &= ~(7<<8);
	val |= (1<<9);
	if(dat == 1){
		set_value(val | 0x01, addr);
	}else if(dat == 2){
		set_value(val | 0x01, addr);
		schedule_delayed_work(&beeper_delay_work, msecs_to_jiffies(delay));
	}
	else{
		set_value((val & (~0x1)), addr);
	}
}

static void beeperTimerHandle(struct timer_list *t)
{
	static int count;
	if((delay_on < 1) || (delay_off < 1)){
		delay_on = 500;
		delay_off = 2000;
	}
	set_beeper(2,delay_on);
	mod_timer(&beeper_timer, jiffies + msecs_to_jiffies(delay_off + delay_on));
	if(g_count){
		count++;
		if(g_count == count){
			del_timer(&beeper_timer);
			timerStatus = 0;
			count = 0;
		}
	}
}

static ssize_t  beeper_read(struct file *file, char __user *usr_buf, size_t size, loff_t *ppos)
{
	unsigned char val;
	char tmpbuf[128];
	unsigned int cnt = 0;

	if (*ppos != 0)
		return 0;
		
	outb(0x66,0x70);
	val = inb(0x71);
	if( val & 0x01){
		cnt = snprintf(tmpbuf, sizeof(tmpbuf), "%s\n", "bootoff");
	}
	else {
		cnt = snprintf(tmpbuf, sizeof(tmpbuf), "%s\n", "booton");
	}

	return simple_read_from_buffer(usr_buf, size, ppos, tmpbuf, cnt);
}

static ssize_t beeper_write(struct file *file,const char __user * buffer,size_t count, loff_t * ppos)
{
        int result = 0;
        char buf[32] = { '\0' };
	

        if (/*!beeper ||*/ (count > sizeof(buf) - 1)){
                return -EINVAL;
	}
        if (copy_from_user(buf, buffer, count)) {
                result = -EFAULT;
                goto end;
        }
	if(buf[count - 1] == 0x0a){
		buf[count - 1] = '\0';
	}
	if((!strncmp(buf, "booton",6)) || (!strncmp(buf, "BOOTON",6))){
	    outb(0x66,0x70);
    	outb(inb(0x71)&(~0x01),0x71);   /* open boot beep */
		printk("---open boot beep\n");
	}
	else if((!strncmp(buf, "bootoff",7)) || (!strncmp(buf, "BOOTOFF",7))){
    	outb(0x66,0x70);
    	outb(inb(0x71)|0x01,0x71);   /* close boot beep */
		printk("---close boot beep\n");
	}	
	else if((!strncmp(buf, "on",3)) || (!strncmp(buf, "ON",3))){
		del_timer(&beeper_timer);
		timerStatus = 0;
		set_beeper(1, 0);
	}else if((!strncmp(buf, "off",4)) || (!strncmp(buf, "OFF",4))){
		del_timer(&beeper_timer);
		timerStatus = 0;
		set_beeper(0, 0);
	}else if((!strncmp(buf, "one",4)) || (!strncmp(buf, "ONE",4))){
		del_timer(&beeper_timer);
		timerStatus = 0;
		set_beeper(2, DELAY_VALUE);
	}else if(!strncmp(buf, "timer ",6)){
		// timer 2000 500 
        	int fields = 0;
		unsigned long a,b;

		fields = sscanf(buf, "timer %ld %ld", &a, &b);
        	if (fields != 2){
                     result = -EFAULT;
                     goto end;
		}

		if((a < 20) || (a > 99999) ||
   		   (b < 20)  || (b > 99999)){
                     result = -EFAULT;
                     goto end;
		}

		delay_off = a;
		delay_on = b;

		if(0 == timerStatus){
		     beeper_timer.expires = jiffies + msecs_to_jiffies(100);
		     add_timer(&beeper_timer);
		     timerStatus = 1;
		}
	}else if(!strncmp(buf, "rep ",3)){
		// timer 2000 500 
        	int fields2 = 0;
		unsigned long count,c,d;
		

		fields2 = sscanf(buf, "rep %ld %ld %ld", &count,&c, &d);
        	if (fields2 != 3){
                     result = -EFAULT;
                     goto end;
		}

		if((c < 20) || (c > 99999) ||
   		   (d < 20)  || (d > 99999)){
                     result = -EFAULT;
                     goto end;
		}

		delay_off = c;
		delay_on = d;
		g_count = count;

		if(0 == timerStatus){
		     beeper_timer.expires = jiffies + msecs_to_jiffies(100);
		     add_timer(&beeper_timer);
		     timerStatus = 1;
		}
	}else{
		result = -EINVAL;
	}
end:
	if(result){
		return result;
	}
        return count;
}

static int null_proc_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, pde_data(inode));
}

static const struct proc_ops sata_proc_fops = {

        .proc_open           = null_proc_open,
        .proc_read           = sata_satate_read,
        .proc_release        = seq_release,
};

static const struct proc_ops beeper_proc_fops = {
        .proc_open           = null_proc_open,
		.proc_write		= beeper_write,
        .proc_release        = seq_release,
		.proc_read           = beeper_read,
};

struct proc_dir_entry *nas_dir = NULL;
static int procfs_create(void)
{
	struct proc_dir_entry *entry = NULL;
	int ret = 0;

	/* create /proc/nas/ */
	nas_dir = proc_mkdir("nas", NULL);
	if (!nas_dir){
		return -ENODEV;
	}
#if 0
	/* create /proc/nas/sata_sw */
	entry = proc_create_data(SATA_SWITCH, S_IRUGO, nas_dir, &sata_proc_fops, NULL);
	if (!entry) {
		ret = -ENODEV;
		goto remove_dev_dir;
	}
#endif
	/* create /proc/nas/beeper */
	entry = proc_create_data(BEEPER, S_IWUGO, nas_dir, &beeper_proc_fops, NULL);
	if (!entry) {
		ret = -ENODEV;
		goto remove_dev_dir;
	}
done:
	return ret;

remove_dev_dir:
	remove_proc_entry("nas", NULL);
	nas_dir = NULL;
	goto done;
}
static int procfs_remove(void)
{
	//remove_proc_entry(SATA_SWITCH, nas_dir);
	remove_proc_entry(BEEPER, nas_dir);
	remove_proc_entry("nas", NULL);
	nas_dir = NULL;
	return 0;

}
static int __init reg_init(void)
{

	INIT_DELAYED_WORK(&beeper_delay_work, beeper_delay_work_func);
	timerStatus = 0;
	timer_setup(&beeper_timer, beeperTimerHandle, 0);
	procfs_create();
	hwinit();
	return 0;
	
}

static void __exit reg_exit(void)
{
	//hw_unioremap();
	procfs_remove();
	cancel_delayed_work_sync(&beeper_delay_work);
	
	if(timerStatus == 1){
	     del_timer(&beeper_timer);
	}
	printk("Exit\n");
	return;
}

module_init(reg_init);
module_exit(reg_exit);

MODULE_LICENSE("GPL");



