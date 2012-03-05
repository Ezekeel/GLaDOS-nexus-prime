/* include/linux/live_oc.h */

#ifndef _LINUX_LIVE_OC_H
#define _LINUX_LIVE_OC_H

#include <linux/cpufreq.h>
#include <linux/input.h>

extern void liveoc_register_freqtable(struct cpufreq_frequency_table * freq_table);
extern void liveoc_register_freqmutex(struct mutex * freqmutex);
extern void liveoc_register_dvfsmutex(struct mutex * dvfsmutex);
extern void liveoc_register_maxthermal(unsigned int * max_thermal);
extern void liveoc_register_maxfreq(unsigned int * max_freq);
extern void liveoc_register_oppdevice(struct device * dev, char * dev_name);
extern void liveoc_init(void);
extern int liveoc_core_ocvalue(void);
extern unsigned long liveoc_gpu_freq(void);
extern void liveoc_register_powerkey(struct input_dev * input_device);

#endif
