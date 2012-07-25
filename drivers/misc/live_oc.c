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
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/opp.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/input.h>
#include <linux/wakelock.h>

#include <plat/clock.h>

#include "../../arch/arm/mach-omap2/voltage.h"
#include "../../arch/arm/mach-omap2/smartreflex.h"
#include "../../kernel/power/power.h"

#define LIVEOC_VERSION 2

#define MAX_MPU_OCVALUE 150
#define MAX_CORE_OCVALUE 150

#define FREQ_INCREASE_STEP 100000

#define MAX_GPU_PERFORMANCE 2

#define PRESSPOWER_DELAY 100
#define SUSPEND_DELAY 200
#define COREUPDATE_DELAY 5000

#define MAX_MPU_FREQ 2147446153

static bool device_suspended, screen_on;

static struct wake_lock liveoc_wake_lock;

static const long unsigned gpu_freqs[] = {307200000, 384000000, 512000000};

static unsigned int mpu_ocvalue = 100, core_ocvalue = 100, gpu_performance = 0,
    num_mpufreqs, num_l3freqs, new_coreocvalue = 100, new_gpuperformance = 0;

static struct cpufreq_frequency_table * frequency_table = NULL;

static struct mutex * frequency_mutex = NULL;
static struct mutex * dvfs_mutex = NULL;

static unsigned long * original_mpu_freqs;

static unsigned long ** mpu_freqs;

static struct device * mpu_device = NULL;

static struct voltagedomain * mpu_voltdm = NULL;

static struct clk * dpll_mpu_clock = NULL;

static unsigned long * original_l3_freqs;

static unsigned long ** l3_freqs;

static struct device * l3_device = NULL;

static struct voltagedomain * core_voltdm = NULL;

static struct clk * l3_clock = NULL;
static struct clk * dpll_core_clock = NULL;
static struct clk * dpll_corex2_clock = NULL;

static unsigned long original_core_freq;

static struct device * gpu_device = NULL;

static unsigned long * gpu_freq;

static unsigned int * maximum_thermal = NULL;
static unsigned int * maximum_freq = NULL;

static struct input_dev * powerkey_device;

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

static void liveoc_update_core(struct work_struct * coreupdate_work);
static DECLARE_DELAYED_WORK(coreupdate_work, liveoc_update_core);

extern struct device_opp * find_device_opp(struct device * dev);
extern void cpufreq_stats_reset(void);

void liveoc_register_powerkey(struct input_dev * input_device)
{
    powerkey_device = input_device;

    return;
}
EXPORT_SYMBOL(liveoc_register_powerkey);

void liveoc_register_maxthermal(unsigned int * max_thermal)
{
    maximum_thermal = max_thermal;

    return;
}
EXPORT_SYMBOL(liveoc_register_maxthermal);

void liveoc_register_maxfreq(unsigned int * max_freq)
{
    maximum_freq = max_freq;

    return;
}
EXPORT_SYMBOL(liveoc_register_maxfreq);

void liveoc_register_freqtable(struct cpufreq_frequency_table * freq_table)
{
    frequency_table = freq_table;

    return;
}
EXPORT_SYMBOL(liveoc_register_freqtable);

void liveoc_register_freqmutex(struct mutex * freqmutex)
{
    frequency_mutex = freqmutex;

    return;
}
EXPORT_SYMBOL(liveoc_register_freqmutex);

void liveoc_register_dvfsmutex(struct mutex * dvfsmutex)
{
    dvfs_mutex = dvfsmutex;

    return;
}
EXPORT_SYMBOL(liveoc_register_dvfsmutex);

void liveoc_register_oppdevice(struct device * dev, char * dev_name)
{
    if (!strcmp(dev_name, "mpu"))
	{
	    if (!mpu_device)
		mpu_device = dev;
	}
    else if (!strcmp(dev_name, "l3_main_1"))
	{
	    if (!l3_device)
		l3_device = dev;
	}
    else if (!strcmp(dev_name, "gpu"))
	{
	    if (!gpu_device)
		gpu_device = dev;
	}

    return;
}
EXPORT_SYMBOL(liveoc_register_oppdevice);

void liveoc_init(void)
{
    struct device_opp * dev_opp;

    struct opp * temp_opp;

    int i;

    mpu_voltdm = voltdm_lookup("mpu");

    dpll_mpu_clock = clk_get(NULL, "dpll_mpu_ck");

    dev_opp = find_device_opp(mpu_device);

    num_mpufreqs = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    if (temp_opp->available)
		num_mpufreqs++;
	}

    original_mpu_freqs = kzalloc(num_mpufreqs * sizeof(unsigned long), GFP_KERNEL);
    mpu_freqs = kzalloc(num_mpufreqs * sizeof(unsigned long *), GFP_KERNEL);

    i = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    if (temp_opp->available)
		{
		    original_mpu_freqs[i] = temp_opp->rate;
		    mpu_freqs[i] = &(temp_opp->rate);

		    i++;
		}
	}
 

    core_voltdm = voltdm_lookup("core");

    l3_clock = clk_get(NULL, "virt_l3_ck");
    dpll_core_clock = clk_get(NULL, "dpll_core_ck");
    dpll_corex2_clock = clk_get(NULL, "dpll_core_x2_ck");

    original_core_freq = dpll_core_clock->rate;

    dev_opp = find_device_opp(l3_device);

    num_l3freqs = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    if (temp_opp->available)
		num_l3freqs++;
	}

    original_l3_freqs = kzalloc(num_l3freqs * sizeof(unsigned long), GFP_KERNEL);
    l3_freqs = kzalloc(num_l3freqs * sizeof(unsigned long *), GFP_KERNEL);

    i = 0;

    list_for_each_entry(temp_opp, &dev_opp->opp_list, node)
	{
	    if (temp_opp->available)
		{
		    original_l3_freqs[i] = temp_opp->rate;
		    l3_freqs[i] = &(temp_opp->rate);

		    i++;
		}
	}

    temp_opp = opp_find_freq_exact(gpu_device, gpu_freqs[0], true);

    gpu_freq = &(temp_opp->rate);

    return;
}
EXPORT_SYMBOL(liveoc_init);

int liveoc_core_ocvalue(void)
{
    return core_ocvalue;
}
EXPORT_SYMBOL(liveoc_core_ocvalue);

unsigned long liveoc_gpu_freq(void)
{
    return gpu_freqs[gpu_performance];
}
EXPORT_SYMBOL(liveoc_gpu_freq);

static void liveoc_mpu_update(void)
{
    int i, index_min = 0, index_max = 0, index_maxthermal = 0, index_maxfreq = 0;

    unsigned long new_freq;

    long rounded_freq;

    struct cpufreq_policy * policy;

    mutex_lock(frequency_mutex);
    mutex_lock(dvfs_mutex);

    omap_sr_disable_reset_volt(mpu_voltdm);
    omap_voltage_calib_reset(mpu_voltdm);

    policy = cpufreq_cpu_get(0);

    for (i = 0; i < num_mpufreqs; i++)
	{
	    if (frequency_table[i].frequency == policy->user_policy.min)
		index_min = i;

	    if (frequency_table[i].frequency == policy->user_policy.max)
		index_max = i;

	    if (frequency_table[i].frequency == *(maximum_thermal))
		index_maxthermal = i;

	    if (frequency_table[i].frequency == *(maximum_freq))
		index_maxfreq = i;

	    new_freq = (original_mpu_freqs[i] / 100) * mpu_ocvalue;

	    if (new_freq > MAX_MPU_FREQ)
		new_freq = MAX_MPU_FREQ;

	    rounded_freq = dpll_mpu_clock->round_rate(dpll_mpu_clock, new_freq);

	    while (rounded_freq <= 0)
		{
		    new_freq += FREQ_INCREASE_STEP;

		    if (new_freq > MAX_MPU_FREQ)
			new_freq = MAX_MPU_FREQ;
    
		    rounded_freq = dpll_mpu_clock->round_rate(dpll_mpu_clock, new_freq);
		}

	    *mpu_freqs[i] = new_freq;

	    frequency_table[i].frequency = *mpu_freqs[i] / 1000;
	}

    cpufreq_frequency_table_cpuinfo(policy, frequency_table);

    policy->min = policy->user_policy.min = frequency_table[index_min].frequency;
    policy->max = policy->user_policy.max = frequency_table[index_max].frequency;

    *(maximum_thermal) = frequency_table[index_maxthermal].frequency;
    *(maximum_freq) = frequency_table[index_maxfreq].frequency;

    omap_sr_enable(mpu_voltdm, omap_voltage_get_curr_vdata(mpu_voltdm));

    mutex_unlock(dvfs_mutex);
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
		    
			    liveoc_mpu_update();

			    pr_info("LIVEOC MPU oc-value set to %u\n", mpu_ocvalue);
			}
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

static void press_powerkey(void)
{
    input_event(powerkey_device, EV_KEY, KEY_POWER, 1);
    input_event(powerkey_device, EV_SYN, 0, 0);
    msleep(PRESSPOWER_DELAY);

    input_event(powerkey_device, EV_KEY, KEY_POWER, 0);
    input_event(powerkey_device, EV_SYN, 0, 0);
    msleep(PRESSPOWER_DELAY);

    return;
}

static void liveoc_update_core(struct work_struct * coreupdate_work)
{
    int i, index_l3 = 0;

    unsigned long new_freq;

    mutex_lock(frequency_mutex);
    mutex_lock(dvfs_mutex);

    omap_sr_disable_reset_volt(core_voltdm);
    omap_voltage_calib_reset(core_voltdm);

    core_ocvalue = new_coreocvalue;

    new_freq = (original_core_freq / 100) * core_ocvalue;

    dpll_core_clock->set_rate(dpll_core_clock, new_freq);
    dpll_corex2_clock->rate = dpll_core_clock->rate * 2;

    for (i = 0; i < num_l3freqs; i++)
	{
	    if (*l3_freqs[i] == l3_clock->rate)
		index_l3 = i;

	    *l3_freqs[i] = (original_l3_freqs[i] / 100) * core_ocvalue;
	}

    l3_clock->set_rate(l3_clock, *l3_freqs[index_l3]);

    omap_sr_enable(core_voltdm, omap_voltage_get_curr_vdata(core_voltdm));

    mutex_unlock(dvfs_mutex);
    mutex_unlock(frequency_mutex);

    pr_info("LIVEOC CORE oc-value set to %u\n", core_ocvalue);

    if (screen_on)
	{
	    wake_unlock(&liveoc_wake_lock);
	    press_powerkey();
	}

    return;
}

static void liveoc_core_update(void)
{
    screen_on = !device_suspended;

    if (screen_on)
	{
	    wake_lock(&liveoc_wake_lock);
	    msleep(SUSPEND_DELAY);
	    press_powerkey();

	    schedule_delayed_work(&coreupdate_work, msecs_to_jiffies(COREUPDATE_DELAY));
	}
    else
	{
	    schedule_delayed_work(&coreupdate_work, 0);
	}

    return;
}

static ssize_t core_ocvalue_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u\n", new_coreocvalue);
}

static ssize_t core_ocvalue_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    unsigned int data;

    if(sscanf(buf, "%u\n", &data) == 1) 
	{
	    if (data >= 100 && data <= MAX_CORE_OCVALUE)
		{
		    if (data != core_ocvalue)
			{
			    new_coreocvalue = data;

			    pr_info("LIVEOC preparing to change CORE oc-value to %u\n", new_coreocvalue);

			    liveoc_core_update();
			}
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

static void liveoc_gpu_update(void)
{
    mutex_lock(frequency_mutex);
    mutex_lock(dvfs_mutex);

    omap_sr_disable_reset_volt(core_voltdm);
    omap_voltage_calib_reset(core_voltdm);

    gpu_performance = new_gpuperformance; 

    *gpu_freq = gpu_freqs[gpu_performance];

    omap_sr_enable(core_voltdm, omap_voltage_get_curr_vdata(core_voltdm));

    mutex_unlock(dvfs_mutex);
    mutex_unlock(frequency_mutex);

    return;
}

static ssize_t gpu_performance_read(struct device * dev, struct device_attribute * attr, char * buf)
{
    return sprintf(buf, "%u (%lumhz)\n", new_gpuperformance, gpu_freqs[new_gpuperformance] / 1000000);
}

static ssize_t gpu_performance_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size)
{
    unsigned int data;

    if(sscanf(buf, "%u\n", &data) == 1) 
	{
	    if (data <= MAX_GPU_PERFORMANCE)
		{
		    if (data != gpu_performance)
			{
			    new_gpuperformance = data;
		    
			    liveoc_gpu_update();

			    pr_info("LIVEOC GPU performance set to %u\n", gpu_performance);
			}
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
static DEVICE_ATTR(core_ocvalue, S_IRUGO | S_IWUGO, core_ocvalue_read, core_ocvalue_write);
static DEVICE_ATTR(gpu_performance, S_IRUGO | S_IWUGO, gpu_performance_read, gpu_performance_write);
static DEVICE_ATTR(version, S_IRUGO , liveoc_version, NULL);

static struct attribute *liveoc_attributes[] = 
    {
	&dev_attr_mpu_ocvalue.attr,
	&dev_attr_core_ocvalue.attr,
	&dev_attr_gpu_performance.attr,
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

static void liveoc_early_suspend(struct early_suspend * h)
{
    device_suspended = true;

    return;
}

static void liveoc_late_resume(struct early_suspend * h)
{
    device_suspended = false;

    return;
}

static struct early_suspend liveoc_suspend_data = 
    {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1,
	.suspend = liveoc_early_suspend,
	.resume = liveoc_late_resume,
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

    register_early_suspend(&liveoc_suspend_data);

    wake_lock_init(&liveoc_wake_lock, WAKE_LOCK_SUSPEND, "liveoc_wake");

    return 0;
}

device_initcall(liveoc_initialization);
