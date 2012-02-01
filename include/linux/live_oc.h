/* include/linux/live_oc.h */

#ifndef _LINUX_LIVE_OC_H
#define _LINUX_LIVE_OC_H

extern void liveoc_register_freqtable(struct cpufreq_frequency_table * freq_table);
extern void liveoc_register_freqmutex(struct mutex * freq_mutex);
extern void liveoc_register_freqpolicy(struct cpufreq_policy * policy);
extern void liveoc_register_maxthermal(unsigned int * max_thermal);
extern void liveoc_register_oppdevice(struct device * dev, char * dev_name);
extern void liveoc_init(void);

#endif
