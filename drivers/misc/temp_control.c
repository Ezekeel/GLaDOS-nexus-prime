/* drivers/misc/temp_control.c
 *
 * Copyright 2012  Imoseyon, Ezekeel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#define TEMPCONTROL_VERSION 1

#define MIN_TEMPLIMIT 50000
#define MAX_TEMPLIMIT 90000

static int temp_limit, original_templimit;

static bool safety_enabled = true;

extern void tempcontrol_update(int templimit);

void tempcontrol_registerlimit(int templimit)
{
    original_templimit = temp_limit = templimit;

    return;
}
EXPORT_SYMBOL(tempcontrol_registerlimit);

static ssize_t tempcontrol_limit_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%d\n", temp_limit);
}

static ssize_t tempcontrol_limit_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    int data;

    if (sscanf(buf, "%d\n", &data) == 1)
	{
	    if (data != temp_limit)
		{
		    if (safety_enabled)
			temp_limit = min(max(data, MIN_TEMPLIMIT), original_templimit);
		    else
			temp_limit = min(max(data, MIN_TEMPLIMIT), MAX_TEMPLIMIT);

		    pr_info("[imoseyon] TEMPCONTROL threshold changed to %d\n", temp_limit);

		    tempcontrol_update(temp_limit);
		}
	}
    else
	{
	    pr_info("[imoseyon] TEMPCONTROL invalid input\n"); 
	}
	    
    return size;
}

static ssize_t tempcontrol_original_limit_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%d\n", original_templimit);
}

static ssize_t tempcontrol_safety_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", (safety_enabled ? 1 : 0));
}

static ssize_t tempcontrol_safety_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    unsigned int data;

    if(sscanf(buf, "%u\n", &data) == 1) 
	{
	    pr_devel("%s: %u \n", __FUNCTION__, data);

	    if (data == 1) 
		{
		    pr_info("%s: TEMPCONTROL safety enabled\n", __FUNCTION__);

		    safety_enabled = true;

		    if (temp_limit > original_templimit)
			{
			    temp_limit = original_templimit;

			    tempcontrol_update(temp_limit);
			}
		} 
	    else if (data == 0) 
		{
		    pr_info("%s: TEMPCONTROL safety disabled\n", __FUNCTION__);

		    safety_enabled = false;
		} 
	    else 
		{
		    pr_info("%s: invalid input range %u\n", __FUNCTION__, data);
		}
	} 
    else 
	{
	    pr_info("%s: invalid input\n", __FUNCTION__);
	}

    return size;
}

static ssize_t tempcontrol_version(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", TEMPCONTROL_VERSION);
}

static DEVICE_ATTR(templimit, S_IRUGO | S_IWUGO, tempcontrol_limit_read, tempcontrol_limit_write);
static DEVICE_ATTR(safety_enabled, S_IRUGO | S_IWUGO, tempcontrol_safety_read, tempcontrol_safety_write);
static DEVICE_ATTR(original_templimit, S_IRUGO, tempcontrol_original_limit_read, NULL);
static DEVICE_ATTR(version, S_IRUGO , tempcontrol_version, NULL);

static struct attribute *tempcontrol_attributes[] = 
    {
	&dev_attr_templimit.attr,
	&dev_attr_safety_enabled.attr,
	&dev_attr_original_templimit.attr,
	&dev_attr_version.attr,
	NULL
    };

static struct attribute_group tempcontrol_group = 
    {
	.attrs  = tempcontrol_attributes,
    };

static struct miscdevice tempcontrol_device = 
    {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tempcontrol",
    };

static int __init tempcontrol_init(void)
{
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, tempcontrol_device.name);

    ret = misc_register(&tempcontrol_device);

    if (ret) 
	{
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, tempcontrol_device.name);
	    return 1;
	}

    if (sysfs_create_group(&tempcontrol_device.this_device->kobj, &tempcontrol_group) < 0) 
	{
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", tempcontrol_device.name);
	}

    return 0;
}

device_initcall(tempcontrol_init);
