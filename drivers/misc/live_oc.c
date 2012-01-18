/* drivers/misc/live_oc.c
 *
 * Copyright 2012  Ezekeel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/cpufreq.h>
#include <linux/opp.h>

#define LIVEOC_VERSION 1

#define MAX_MPU_OCVALUE 150

#define NUM_MPU_FREQS	5

static int mpu_ocvalue = 100;

static unsigned long original_mpu_freqs[NUM_MPU_FREQS];

static struct device * mpu_device = NULL;

static struct cpufreq_frequency_table * frequency_table = NULL;

static struct mutex * frequency_mutex = NULL;

static struct cpufreq_policy * freq_policy = NULL;

unsigned int * maximum_thermal = NULL;

struct opp {
    struct list_head node;

    bool available;
    unsigned long rate;
    unsigned long u_volt;

    struct device_opp * dev_opp;
};

struct device_opp {
    struct list_head node;

    struct device * dev;
    struct list_head opp_list;
};

extern struct device_opp * find_device_opp(struct device * dev);
extern void cpufreq_stats_reset(void);

void register_maxthermal(unsigned int * max_thermal)
{
    maximum_thermal = max_thermal;

    return;
}
EXPORT_SYMBOL(register_maxthermal);

void register_freqpolicy(struct cpufreq_policy * policy)
{
    freq_policy = policy;

    return;
}
EXPORT_SYMBOL(register_freqpolicy);

void register_freqtable(struct cpufreq_frequency_table * freq_table)
{
    frequency_table = freq_table;

    return;
}
EXPORT_SYMBOL(register_freqtable);

void register_freqmutex(struct mutex * freq_mutex)
{
    frequency_mutex = freq_mutex;

    return;
}
EXPORT_SYMBOL(register_freqmutex);

void register_oppdevice(struct device * dev, char * dev_name)
{
    if (!strcmp(dev_name, "mpu"))
	{
	    if (!mpu_device)
		mpu_device = dev;
	}

    return;
}
EXPORT_SYMBOL(register_oppdevice);

void liveoc_init(void)
{
    struct device_opp * dev_opp;

    struct opp * temp_opp;

    int i;

    dev_opp = find_device_opp(mpu_device);

    i = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    original_mpu_freqs[i] = temp_opp->rate;

	    i++;
	}
 
    return;
}
EXPORT_SYMBOL(liveoc_init);

static void mpu_update(void)
{
    struct device_opp * dev_opp;

    struct opp * temp_opp;

    int i, index_min, index_max, index_maxthermal;

    mutex_lock(frequency_mutex);

    dev_opp = find_device_opp(mpu_device);

    i = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    if (frequency_table[i].frequency == freq_policy->user_policy.min)
		index_min = i;

	    if (frequency_table[i].frequency == freq_policy->user_policy.max)
		index_max = i;

	    if (frequency_table[i].frequency == *(maximum_thermal))
		index_maxthermal = i;

	    if (i != 0)
		{
		    temp_opp->rate = (original_mpu_freqs[i] / 100) * mpu_ocvalue;
		    if (temp_opp->available)
			frequency_table[i].frequency = temp_opp->rate / 1000;
		}

	    i++;
	}

    cpufreq_frequency_table_cpuinfo(freq_policy, frequency_table);

    freq_policy->user_policy.min = frequency_table[index_min].frequency;
    freq_policy->user_policy.max = frequency_table[index_max].frequency;

    *(maximum_thermal) = frequency_table[index_maxthermal].frequency;

    mutex_unlock(frequency_mutex);

#ifdef CONFIG_CPU_FREQ_STAT
    cpufreq_stats_reset();
#endif

    return;
}

static ssize_t mpu_ocvalue_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", mpu_ocvalue);
}

static ssize_t mpu_ocvalue_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    unsigned int data;

    if(sscanf(buf, "%u\n", &data) == 1) 
	{
	    if (data >= 100 && data <= MAX_MPU_OCVALUE)
		{
		    if (data != mpu_ocvalue)
			{
			    mpu_ocvalue = data;
		    
			    mpu_update();
			}

		    pr_info("LIVEOC MPU oc-value set to %u\n", mpu_ocvalue);
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

static ssize_t liveoc_version(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", LIVEOC_VERSION);
}

static DEVICE_ATTR(mpu_ocvalue, S_IRUGO | S_IWUGO, mpu_ocvalue_read, mpu_ocvalue_write);
static DEVICE_ATTR(version, S_IRUGO , liveoc_version, NULL);

static struct attribute *liveoc_attributes[] = 
    {
	&dev_attr_mpu_ocvalue.attr,
	&dev_attr_version.attr,
	NULL
    };

static struct attribute_group liveoc_group = 
    {
	.attrs  = liveoc_attributes,
    };

static struct miscdevice liveoc_device = 
    {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "liveoc",
    };

static int __init liveoc_initialization(void)
{
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, liveoc_device.name);

    ret = misc_register(&liveoc_device);

    if (ret) 
	{
	    pr_err("%s misc_register(%s) fail\n", __FUNCTION__, liveoc_device.name);

	    return 1;
	}

    if (sysfs_create_group(&liveoc_device.this_device->kobj, &liveoc_group) < 0) 
	{
	    pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	    pr_err("Failed to create sysfs group for device (%s)!\n", liveoc_device.name);
	}

    return 0;
}

device_initcall(liveoc_initialization);
