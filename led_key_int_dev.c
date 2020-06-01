#include <linux/module.h>

#include <linux/fs.h>
#include <linux/errno.h>

#include <linux/kernel.h>
#include <linux/major.h>

#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>

#include <linux/ide.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>



#define LED_ON 1
#define LED_OFF 0

//define irq struct
struct key_irq_desc{
	int gpio;
	int irqnum;
	unsigned char key_val;
	irqreturn_t (*handler)(int, void *);	
};


//set dev struct
struct char_dev{
	dev_t devid;
	struct cdev cdev;		//struct for file operation
	struct class *class;	//class created by kernel in init operation
	struct device *device;  //struct created by kernel in init operation
	int major;
	int minor;
	struct device_node *nd_led; //device tree node led
	int gpio_led;
	struct device_node *nd_key; //device tree node key
 	int gpio_key;
	struct key_irq_desc keyirqdesc;
	struct tasklet_struct key_tasklet;	
} led_dev;
static char dev_buf[100];

//taskled
void delay_short(volatile unsigned int n)
{
	while(n--){}
}

void delay(volatile unsigned int n)
{
	while(n--)
	{
		delay_short(0x7ff);
	}
}

void key_tasklet_opr(unsigned long data){
	struct key_irq_desc *keydesc;
	struct char_dev *devc = (struct char_dev *)data;
	keydesc = &devc->keyirqdesc;
	delay(10);
	printk("key1 press confirmed.\r\n");
	if(gpio_get_value(devc->keyirqdesc.gpio) == 0){
		if(gpio_get_value(devc->gpio_led) == 0){
			gpio_set_value(devc->gpio_led, 1);
		}
		else if(gpio_get_value(devc->gpio_led) == 1){
			gpio_set_value(devc->gpio_led, 0);	
		}
		printk("key operated in tasklet, gpio_led=%d \r\n",gpio_get_value(devc->gpio_led));	
	}

}

//irq handler
static irqreturn_t key1_irq_handler(int irq, void *dev_id){
	struct char_dev *devc = (struct char_dev *)dev_id;
	devc->key_tasklet.data = (unsigned long)dev_id;
	tasklet_schedule(&devc->key_tasklet);	
	return IRQ_RETVAL(IRQ_HANDLED);
}

//read write open release func
static int led_open(struct inode *inode, struct file *file)
{
	file->private_data = &led_dev;
	printk("chardev led opened.\r\n");
	return 0;
}


static ssize_t led_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
		int err = 0;
		struct char_dev *devc = file->private_data;
		int key_status;
	
	//	printk("key operating, gpio_key=%d gpio_led=%d \r\n",gpio_get_value(devc->gpio_key),gpio_get_value(devc->gpio_led));
	
		if(gpio_get_value(devc->gpio_key) == 0){
			if(gpio_get_value(devc->gpio_led) == 0){
				gpio_set_value(devc->gpio_led, 1);
			}
			else if(gpio_get_value(devc->gpio_led) == 1){
				gpio_set_value(devc->gpio_led, 0);	
			}
			printk("key2 operated, gpio_key=%d gpio_led=%d \r\n",gpio_get_value(devc->gpio_key),gpio_get_value(devc->gpio_led)); 
		}
	
		key_status = gpio_get_value(devc->gpio_key);
		err = copy_to_user(buf, &key_status, sizeof(key_status));
		
		return 0;

}

static ssize_t led_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
	int err = 0;
	struct char_dev *devc = file->private_data;

	err = copy_from_user(dev_buf, buf, size);
 	printk("led operating, gpio_led=%d \r\n",gpio_get_value(devc->gpio_led));
	
	if(dev_buf[0] == LED_ON){
		gpio_set_value(devc->gpio_led, 0);
	}
	else if(dev_buf[0] == LED_OFF){
		gpio_set_value(devc->gpio_led, 1);
	}
	
	printk("led operated, gpio_led=%d \r\n",gpio_get_value(devc->gpio_led));

	return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
	printk("chardev led released.\r\n");
	return 0;
}


//struct file opr
static struct file_operations led_dev_fops = {
	.owner = THIS_MODULE,
	.open=  led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

//init and exit
static int __init led_init(void)
{
	int err;

//led init	
	printk("ready to init led dev.\r\n");

	led_dev.nd_led = of_find_node_by_path("/tstled");
	if(led_dev.nd_led == NULL){
		printk("node led not found.\r\n");
		return -1;
	}
	else{
		printk("node led has been found.\r\n");
	}

	led_dev.gpio_led = of_get_named_gpio(led_dev.nd_led, "gpios", 0);
	if(led_dev.gpio_led < 0){
		printk("get gpio_led failed.");
		return -1;
	}
	err = gpio_direction_output(led_dev.gpio_led, 1);
	if(err < 0) printk("set led gpio direction failed");

//key1 init
	printk("ready to init key dev.\r\n");

	led_dev.nd_key = of_find_node_by_path("/key1");
	if(led_dev.nd_key == NULL){
		printk("node key not found.\r\n");
		return -1;
	}
	else{
		printk("node key has been found.\r\n");
	}

	led_dev.keyirqdesc.gpio = of_get_named_gpio(led_dev.nd_key, "key-gpio", 0);
	if(led_dev.keyirqdesc.gpio < 0){
		printk("get key gpio failed.");
		return -1;
	}

	err = gpio_request(led_dev.keyirqdesc.gpio, "key1");
	err = gpio_direction_input(led_dev.keyirqdesc.gpio);
 	if(err < 0) printk("set key gpio direction failed");
	
	led_dev.keyirqdesc.handler = key1_irq_handler;
	
//	led_dev.keyirqdesc.irqnum = irq_of_parse_and_map(led_dev.nd_key, 0);
	led_dev.keyirqdesc.irqnum = gpio_to_irq(led_dev.keyirqdesc.gpio);
	
	printk("key irqnum is:%d \r\n",led_dev.keyirqdesc.irqnum);
	if(led_dev.keyirqdesc.irqnum == 0) printk("failed to get an irq num.");
	

	tasklet_init(&led_dev.key_tasklet, key_tasklet_opr, led_dev.key_tasklet.data);

	err = request_irq(led_dev.keyirqdesc.irqnum, led_dev.keyirqdesc.handler, IRQF_TRIGGER_FALLING, "key1", &led_dev);
	if(err < 0) printk("key1 irq request failed");

//key2 init
	printk("ready to init key2 dev.\r\n");

	led_dev.nd_key = of_find_node_by_path("/key2");
	if(led_dev.nd_key == NULL){
		printk("node key2 not found.\r\n");
		return -1;
	}
	else{
		printk("node key2 has been found.\r\n");
	}

	led_dev.gpio_key = of_get_named_gpio(led_dev.nd_key, "key-gpio", 0);
	if(led_dev.gpio_key < 0){
		printk("get key2 gpio failed.");
		return -1;
	}

	err = gpio_request(led_dev.gpio_key, "key2");
	err = gpio_direction_input(led_dev.gpio_key);


//register chardev
	//applicate a dev num
	if(led_dev.major){
		led_dev.devid = MKDEV(led_dev.major, 0);
		register_chrdev_region(led_dev.devid, 1, "led_dev");
	}
	else{
		alloc_chrdev_region(&led_dev.devid, 0, 1, "led_dev");
		led_dev.major = MAJOR(led_dev.devid);
		led_dev.minor = MINOR(led_dev.devid);
	}
	printk("led_dev major=%d minor=%d",led_dev.major,led_dev.minor);

	//initialize cdev
	led_dev.cdev.owner = THIS_MODULE;
	cdev_init(&led_dev.cdev, &led_dev_fops);
	
	//add cdev
	cdev_add(&led_dev.cdev, led_dev.devid, 1);

	//create class
	led_dev.class = class_create(THIS_MODULE, "led_dev");
	if(IS_ERR(led_dev.class)) return PTR_ERR(led_dev.class);

	//create device
	led_dev.device = device_create(led_dev.class, NULL, led_dev.devid, NULL, "led_dev");
	if(IS_ERR(led_dev.device)) return PTR_ERR(led_dev.device);
	
	return 0;
}

static void __exit led_exit(void)
{
	//release interrupt
	free_irq(led_dev.keyirqdesc.irqnum, &led_dev);
	
	//unregister dev
	cdev_del(&led_dev.cdev);
	unregister_chrdev_region(led_dev.devid, 1);

	device_destroy(led_dev.class, led_dev.devid);
	class_destroy(led_dev.class);

	printk("led dev unregisted.\r\n");
}

module_init(led_init);
module_exit(led_exit);

//appendix
MODULE_LICENSE("GPL");
MODULE_AUTHOR("XJJ");




