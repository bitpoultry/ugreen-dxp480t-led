// SPDX-License-Identifier: GPL-2.0-or-later
/* *  MEN 14F021P00 Board Management Controller (BMC) LEDs Driver.  
 * *  This is the core LED driver of the MEN 14F021P00 BMC.  
 * *  There are four LEDs available which can be switched on and off.
 * *  STATUS LED, HOT SWAP LED, USER LED 1, USER LED 2
 * *  Copyright (C) 2014 MEN Mikro Elektronik Nuernberg GmbH */ 

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <asm-generic/delay.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/workqueue.h>
#include <linux/dmi.h>
//#include <misc/ugreen_product.h>

#define  DATA_SET_REG			0xA0
#define  DATA_CLS_REG			0xB1
#define  LED_MODE_BASE_REG		0x50
#define  DELAY_ON_BASE_REG		0xC0
#define  DELAY_OFF_BASE_REG		0xD0
#define  LED_BRIGHTNESS_BASE_REG	0xE0
#define  DELAY_ON_BREATH_BASE_REG	0x60
#define  DELAY_OFF_BREATH_BASE_REG	0x72

#define  MODE_NONE	0X00
#define  MODE_TIMER	0X01
#define  MODE_BREATH	0x02
#define  MODE_ONE	0X03

#define  COLOR_WHITE	0x01
#define  COLOR_ORANGE	0x02
#define  COLOR_ALL	0x03
#define  LED_OFF	0x00

#define I2C_ADDR		0x31
#define I2C_ADDR2		0x26
#define MCU_CHIP_ID		0x5A
#define MCU_FW_VER		0x5D

#define  USE_WORKQUE

static const unsigned short normal_i2c[] = { I2C_ADDR, I2C_ADDR2, I2C_CLIENT_END };

struct mcu_data {
	struct i2c_client *i2c_client;
	struct mcu_led *leds;
	struct mutex update_lock;
};

struct mcu_led {
	u8 led_bit;
	u8 color;
	unsigned long delay_on;
	unsigned long delay_off;
	const char *name;
	struct led_classdev cdev;
	struct i2c_client *i2c_client;
	struct work_struct	work;
	enum led_brightness brightness;
#ifdef USE_WORKQUE
	enum led_brightness value;
#endif
};

static struct mcu_led leds[] = {
	{
		.name = "power",
		.led_bit = 0,
		.color=COLOR_WHITE,
	},
#if 0
	{
		.name = "disk1",
		.led_bit = 4,
		.color=COLOR_WHITE,
	},
	{
		.name = "disk2",
		.led_bit = 2,
		.color=COLOR_WHITE,
	}
#endif 
};

/* in most cases, should be called while holding */
static inline u16 mcu_read16(struct i2c_client *client, u8 reg)
{
	return (i2c_smbus_read_byte_data(client, reg) << 8)
		| i2c_smbus_read_byte_data(client, reg + 1);
}

static inline u8 mcu_read8(struct i2c_client *client, u8 reg)
{
	udelay(1000);
	return i2c_smbus_read_byte_data(client, reg);
}

static inline int smbus_write8(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if(ret < 0){
		dev_err(&client->dev, "Error:: smbus_write8 %02x value:%02x ret:%d\n", reg, value, ret);
	}
	if((reg == DATA_SET_REG) || (reg == DATA_CLS_REG)){
		udelay(2000);
	}else{
		udelay(10000);
	}
//	dev_err(&client->dev, "write reg:%02X value:%02X\n",reg, value);

	return ret;
}

static int smbus_need_write8(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;
	u8 value1;

	value1 = i2c_smbus_read_byte_data(client, reg);
	if(value1 == value){
		return 0;
	}

	ret = i2c_smbus_write_byte_data(client, reg, value);
	if(ret < 0){
		dev_err(&client->dev, "Error:: smbus_write8 %02x value:%02x ret:%d\n", reg, value, ret);
	}
	udelay(5000);
//	dev_err(&client->dev, "write reg:%02X value:%02X old_value:%02X\n",reg, value, value1);

	return ret;
}
static ssize_t color_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);

	if(led->color == 0x01){
		return sprintf(buf, "%s\n", "white");
	}
	if(led->color == 0x02){
		return sprintf(buf, "%s\n", "orange");
	}
	if(led->color == 0x03){
		return sprintf(buf, "%s\n", "white + orange");
	}

	return sprintf(buf, "%s\n", "None");
}

static ssize_t color_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);

	unsigned long state;
	ssize_t ret;

	mutex_lock(&led_cdev->led_access);

	if (led_sysfs_is_disabled(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;
	if(state > 3){
	     ret = -EINVAL;
	     goto unlock;
	}
	state &= 0x03;

	//pr_err("color_store bri:%d,value:%d, color:%d , state:%lu\n", led->brightness, led->value, led->color, state);
	if(led->color != (unsigned char)state){
		led->color = (unsigned char)state;
		if(led->brightness != 0){
			if(led->color == 0x01){
				smbus_write8(led->i2c_client,  LED_MODE_BASE_REG+1, MODE_NONE);
			}else{
				smbus_write8(led->i2c_client,  LED_MODE_BASE_REG, MODE_NONE);
			}
			//smbus_write8(led->i2c_client,  DATA_CLS_REG, ((~state) & 0x03) << led->led_bit);
			smbus_write8(led->i2c_client,  DATA_CLS_REG,  0x03 << led->led_bit);
			smbus_write8(led->i2c_client,  DATA_SET_REG, state << led->led_bit);
		}
	}
	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}
static DEVICE_ATTR_RW(color);

static ssize_t rbrightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned char ver;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);

	return sprintf(buf, "%d\n", led->brightness);
}

static DEVICE_ATTR_RO(rbrightness);

static struct attribute *color_attrs[] = {
	&dev_attr_rbrightness.attr,	
	&dev_attr_color.attr,
	NULL
};
ATTRIBUTE_GROUPS(color);

static void mcu_led_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	struct mcu_led *led = container_of(led_cdev, struct mcu_led, cdev);

#ifndef USE_WORKQUE
	unsigned char val = (led->color & 0x03) << led->led_bit;
	struct mcu_data *data = i2c_get_clientdata(led->i2c_client);

	mutex_lock(&data->update_lock);

	if (value == LED_OFF){
		if(led->brightness != 0){
		     smbus_write8(led->i2c_client,  DATA_CLS_REG, val);
		     led->brightness = 0;
		}
	}else{
		if(led->brightness != 1){
		     smbus_write8(led->i2c_client,  DATA_SET_REG, val);
		     led->brightness = 1;
		}
	}

	mutex_unlock(&data->update_lock);
#else
	led->value = value;
	schedule_work(&led->work);
#endif
}

#ifdef USE_WORKQUE
static void mcu_work(struct work_struct *work)
{
	struct mcu_led *led = container_of(work, struct mcu_led, work);
	unsigned char val = (led->color << led->led_bit);
	enum led_brightness value = led->value;
	int need;

	if (value == LED_OFF){
	     	smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x3 << led->led_bit);
	        //led->brightness = 0;
	}else{
	     if(led->color == 0x01){
	     	   smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x02 << led->led_bit);
	     }else{
	     	   smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x01 << led->led_bit);
	     }
	     smbus_write8(led->i2c_client,  DATA_SET_REG, val);
	     if (1 != value ){
		 	smbus_write8(led->i2c_client,  LED_BRIGHTNESS_BASE_REG, value);
		 	smbus_write8(led->i2c_client,  (LED_BRIGHTNESS_BASE_REG+1), value);
			dev_err(&led->i2c_client->dev, "set 2 brightness %d\n",value);
#if 0
		     if(led->color == 0x01){
			     smbus_write8(led->i2c_client,  LED_BRIGHTNESS_BASE_REG, value);
			     dev_err(&led->i2c_client->dev, "set white brightness %d\n",value);
		     }else{
			     smbus_write8(led->i2c_client,  (LED_BRIGHTNESS_BASE_REG+1), value);
			     dev_err(&led->i2c_client->dev, "set orange brightness %d\n",value);
		     }
#endif 
			led->brightness = value;
	    }
	    //led->brightness = value;
	}
}
#endif

static int mcu_led_set_blink(struct led_classdev *cdev,
                                unsigned long *delay_on,
                                unsigned long *delay_off)
{

	unsigned char delayOnReg;
	unsigned char delayOffReg;
	unsigned char modeReg;
	unsigned char needSet;
	unsigned char mode = -1;
	struct mcu_led *led = container_of(cdev, struct mcu_led, cdev);
	struct mcu_data *data = i2c_get_clientdata(led->i2c_client);

	modeReg = LED_MODE_BASE_REG + led->led_bit;
	if((cdev->trigger) && (cdev->trigger->name)){
		if(!strncmp(cdev->trigger->name, "breath",6)){
			if(strncmp(led->name, "power",5)){ // 电源白色LED才 支持
				return 0;	
			}
			mode = MODE_BREATH;
			led->color = COLOR_WHITE;
		//	dev_err(&led->i2c_client->dev, "set mode breath %s\n",led->name);
		}
		if(!strncmp(cdev->trigger->name, "timer",5)){
			mode = MODE_TIMER;
			led->value = 1;
			//led->brightness = 255;  // 解决闪烁时能修改颜色
			delayOnReg = DELAY_ON_BASE_REG + (led->led_bit << 1);
			delayOffReg = DELAY_OFF_BASE_REG + (led->led_bit << 1);
		//	dev_err(&led->i2c_client->dev, "set mode timer %s\n",led->name);
		}
		if(!strncmp(cdev->trigger->name, "normal",6)){
			mode = MODE_NONE;
			led->value = 1;
			//led->brightness = 255;  // 解决闪烁时能修改颜色
		//	dev_err(&led->i2c_client->dev, "set mode normal %s\n",led->name);
		}
		if(mode != -1){
			if(led->color == (COLOR_ALL)){
				dev_err(&led->i2c_client->dev, "not support color:%d \n",led->color);
				//smbus_write8(led->i2c_client,  modeReg, mode);
				//smbus_write8(led->i2c_client,  modeReg + 1, mode);
			}else if(led->color == COLOR_ORANGE){
				smbus_write8(led->i2c_client,  modeReg, MODE_NONE);
				smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x01 << led->led_bit);
				smbus_write8(led->i2c_client,  modeReg + 1, mode);
			}else if(led->color == COLOR_WHITE){
				smbus_write8(led->i2c_client,  modeReg + 1, MODE_NONE);
				smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x02 << led->led_bit);
				smbus_write8(led->i2c_client,  modeReg, mode);
			}else{
				smbus_write8(led->i2c_client,  DATA_CLS_REG, 0x03 << led->led_bit);
				return 0;
			}
		}
		if(mode != MODE_TIMER){
	     		return 0;
		}

	}else{
		delayOnReg = DELAY_ON_BASE_REG + (led->led_bit << 1);
		delayOffReg = DELAY_OFF_BASE_REG + (led->led_bit << 1);
	}

	needSet = 0;
	if(*delay_on != led->delay_on){
		if(*delay_on < 20){
			*delay_on = 500;
		}
		led->delay_on = *delay_on;
		needSet = 1;
	}
	if(*delay_off != led->delay_off){
		if(*delay_off < 20){
			*delay_off = 500;
		}
		led->delay_off = *delay_off;
		needSet = 1;
	}
	
	if(!needSet){
		goto end;
	}

	mutex_lock(&data->update_lock);
	//------------- white
	if(led->color & 0x01){
		// delay_on
		smbus_write8(led->i2c_client,  delayOnReg, (*delay_on & 0xff));
		smbus_write8(led->i2c_client,  delayOnReg + 1, (*delay_on >> 8) & 0xff);
		// delay_off
		smbus_write8(led->i2c_client,  delayOffReg, (*delay_off & 0xff));
		smbus_write8(led->i2c_client,  delayOffReg + 1, (*delay_off >> 8) & 0xff);
//		dev_info(&led->i2c_client->dev, "delay_on:%lu off:%lu led_bit:%d, on_reg:0x%x off_reg:0x%x  %s  color:%X\n",
//			*delay_on, *delay_off, led->led_bit,delayOnReg, delayOffReg, cdev->trigger->name, led->color);
	}

	//---------- orange
	if(led->color & 0x02){
		// delay_on
		smbus_write8(led->i2c_client,  delayOnReg + 2, (*delay_on & 0xff));
		smbus_write8(led->i2c_client,  delayOnReg + 3, (*delay_on >> 8) & 0xff);
		// delay_off
		smbus_write8(led->i2c_client,  delayOffReg + 2, (*delay_off & 0xff));
		smbus_write8(led->i2c_client,  delayOffReg + 3, (*delay_off >> 8) & 0xff);
//		dev_info(&led->i2c_client->dev, "delay_on:%lu off:%lu led_bit:%d, on_reg:0x%x off_reg:0x%x  %s  color:%X\n",
//			*delay_on, *delay_off, led->led_bit,delayOnReg + 2, delayOffReg + 2, cdev->trigger->name, led->color);
	}
	mutex_unlock(&data->update_lock);
end:
        return 0;
}

static void mcu_init(struct i2c_client *i2c_client, struct mcu_data *data)
{
	int i;

	mutex_lock(&data->update_lock);

// set mode normal
	for(i=0; i< 2; i++){
		smbus_write8(i2c_client, 0x50 + i, 0x00); // mode:none
	}
// led ORANGE off
	smbus_write8(i2c_client, DATA_CLS_REG, 0x02);
// power_led on
	smbus_write8(i2c_client, DATA_SET_REG, 0x01);

	mutex_unlock(&data->update_lock);
}

static int mcu_led_probe(struct i2c_client *client)
{
	int i;
	int ret;
	struct mcu_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct mcu_data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto exit;
	}
	data->leds = leds;
	data->i2c_client = client;
	mutex_init(&data->update_lock);
	i2c_set_clientdata(client, data);

	mcu_init(client,data);

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		leds[i].cdev.name = leds[i].name;
		leds[i].cdev.brightness_set = mcu_led_set;
		leds[i].cdev.brightness = LED_FULL;
		leds[i].cdev.blink_set = mcu_led_set_blink;
		leds[i].cdev.groups = color_groups;
		leds[i].cdev.max_brightness = 254;
		leds[i].color = COLOR_WHITE;
		leds[i].delay_on = 1;
		leds[i].delay_off = 1;
		leds[i].brightness = LED_FULL; 
		leds[i].i2c_client = client;
#ifdef USE_WORKQUE
		INIT_WORK(&leds[i].work, mcu_work);
#endif
		ret = led_classdev_register(&client->dev, &leds[i].cdev);
		if (ret < 0) {
			dev_err(&client->dev, "failed to register LED device\n");
			return ret;
		}
	}
	dev_info(&client->dev, "MCU LED device enabled\n");

	return 0;
exit:
	return ret;

}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int mcu_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *name;
	u16 chipid;
	u8 vendid;
#if 0
	if(!ug_check_product_match(DH2600, __func__)){
		return -ENODEV;
	}
#endif

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
              return -ENODEV;

	chipid = mcu_read16(client, MCU_CHIP_ID);
	vendid = mcu_read8(client, MCU_FW_VER);

	if (chipid == 0xA5B5)
		name = "mcu_led";
	else
		return -ENODEV;

	dev_info(&adapter->dev, "Found %s version:v1.0.%d\n", name, vendid);
	strscpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static void mcu_remove(struct i2c_client *client)
{
	int i;
	unsigned char modeReg;
	struct mcu_data *data = i2c_get_clientdata(client);
#if 1
	mutex_lock(&data->update_lock);

	for (i = 0; i < ARRAY_SIZE(leds); i++) {
		cancel_work_sync(&data->leds[i].work);
		led_classdev_unregister(&data->leds[i].cdev);
//		modeReg = LED_MODE_BASE_REG + leds[i].led_bit;
//		smbus_write8(client,  modeReg,     MODE_NONE);
//		smbus_write8(client,  modeReg + 1, MODE_NONE);
	}
//	smbus_write8(client, DATA_CLS_REG, 0xff);
	mutex_unlock(&data->update_lock);
#endif

	return;
}

static void mcu_shutdown(struct i2c_client *client)
{
	mcu_remove(client);
}

static const struct i2c_device_id mcu_id[] = {
	{ "mcu_led", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcu_id);

static struct i2c_driver mcu_led = {
	.class		= I2C_CLASS_HWMON,
	.probe		= mcu_led_probe,
	.remove 	= mcu_remove,
	.shutdown	= mcu_shutdown,
	.driver		= {
		.name	= "mcu_led",
	},
	.id_table = mcu_id,
	.detect = mcu_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(mcu_led);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mcu_led");
