/* Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2015 Francisco Franco
 * Copyright (c) 2017 Ayush Rathore
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
 /* ARB THERMAL CONFIGURARTION */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/msm_tsens.h>
#include <linux/msm_thermal.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/thermal.h>
#include <linux/regulator/rpm-smd-regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <soc/qcom/rpm-smd.h>
#include <soc/qcom/scm.h>
#include <linux/sched/rt.h>
#include <linux/ratelimit.h>
#include <trace/trace_thermal.h>

#define MAX_CURRENT_UA 100000
#define MAX_RAILS 5
#define MAX_THRESHOLD 2
#define MONITOR_ALL_TSENS -1
#define TSENS_NAME_MAX 20
#define TSENS_NAME_FORMAT "tsens_tz_sensor%d"
#define THERM_SECURE_BITE_CMD 8
#define SENSOR_SCALING_FACTOR 1
#define CPU_DEVICE "cpu%d"
#define CREATE_TRACE_POINTS
#define TRACE_MSM_THERMAL
#define _temp_threshold		50
#define _temp_step	3

static struct thermal_info {
	uint32_t cpuinfo_max_freq;
	uint32_t limited_max_freq;
	unsigned int safe_diff;
	bool throttling;
	bool pending_change;
} info = {
	.cpuinfo_max_freq = UINT_MAX,
	.limited_max_freq = UINT_MAX,
	.safe_diff = 5,
	.throttling = false,
	.pending_change = false,
};

int TEMP_SAFETY = 1;
int TEMP_THRESHOLD = _temp_threshold;
int TEMP_STEP = _temp_step;
int LEVEL_VERY_HOT = _temp_threshold + _temp_step;
int LEVEL_HOT = _temp_threshold + (_temp_step * 2);
int LEVEL_HELL = _temp_threshold + (_temp_step * 3);
int FREQ_HELL = 800000;
int FREQ_VERY_HOT = 1113600;
int FREQ_HOT = 1344000;
int FREQ_WARM = 1459200;
extern int AiO_HotPlug;
#ifdef CONFIG_THUNDERCHARGE_CONTROL
extern int mswitch;
extern int custom_current;
int custom_current;
#endif

static struct msm_thermal_data msm_thermal_info;
static struct delayed_work check_temp_work;
static struct workqueue_struct *thermal_wq;

static void cpu_offline_wrapper(int cpu)
{
        if (cpu_online(cpu))
		cpu_down(cpu);
}

static void __ref cpu_online_wrapper(int cpu)
{
        if (!cpu_online(cpu))
		cpu_up(cpu);
}

static int msm_thermal_cpufreq_callback(struct notifier_block *nfb,
		unsigned long event, void *data)
{
	struct cpufreq_policy *policy = data;

	if (event != CPUFREQ_ADJUST && !info.pending_change)
		return 0;

	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
		info.limited_max_freq);

	return 0;
}

static struct notifier_block msm_thermal_cpufreq_notifier = {
	.notifier_call = msm_thermal_cpufreq_callback,
};

static void limit_cpu_freqs(uint32_t max_freq)
{
	unsigned int cpu;

	if (info.limited_max_freq == max_freq)
		return;

	info.limited_max_freq = max_freq;
	info.pending_change = true;
	pr_info_ratelimited("%s: Setting cpu max frequency to %u\n",
	KBUILD_MODNAME, max_freq);

	get_online_cpus();
	for_each_online_cpu(cpu) {
		cpufreq_update_policy(cpu);
		pr_info("%s: Setting cpu%d max frequency to %d\n",
				KBUILD_MODNAME, cpu, info.limited_max_freq);
	}
	put_online_cpus();

	info.pending_change = false;
}

static void check_temp(struct work_struct *work)
{
	struct tsens_device tsens_dev;
	uint32_t freq = 0;
	long temp = 0;

	tsens_dev.sensor_num = msm_thermal_info.sensor_id;
	tsens_get_temp(&tsens_dev, &temp);

	if (info.throttling) {
		if (temp < (TEMP_THRESHOLD - info.safe_diff)) {
			limit_cpu_freqs(info.cpuinfo_max_freq);
			info.throttling = false;
			goto reschedule;
		}
	}
    if (temp >= LEVEL_HELL)
		freq = FREQ_HELL;
	else if (temp >= LEVEL_VERY_HOT)
		freq = FREQ_VERY_HOT;
	else if (temp >= LEVEL_HOT)
		freq = FREQ_HOT;
	else if (temp >= TEMP_THRESHOLD)
		freq = FREQ_WARM;
	if (freq) {
		limit_cpu_freqs(freq);

		if (!info.throttling)
			info.throttling = true;
	}
	
   if(TEMP_SAFETY==1){
	if (temp >= 80){
 		cpu_offline_wrapper(1);
 		cpu_offline_wrapper(2);
 		cpu_offline_wrapper(3);
 		cpu_offline_wrapper(4);
 		cpu_offline_wrapper(5);
 	    cpu_offline_wrapper(6);
 		cpu_offline_wrapper(7);
	}
 	else if (temp >= 69){
		cpu_offline_wrapper(1);
 		cpu_offline_wrapper(2);
 		cpu_offline_wrapper(3);
 		cpu_online_wrapper(4);
 		cpu_online_wrapper(5);
 	    cpu_offline_wrapper(6);
 		cpu_offline_wrapper(7);
 	}
 	else if (temp >= 63){
 	    cpu_offline_wrapper(1);
 		cpu_offline_wrapper(2);
 		cpu_offline_wrapper(3);
 		cpu_online_wrapper(4);
 		cpu_online_wrapper(5);
		cpu_online_wrapper(6);
 		cpu_online_wrapper(7);
 	}
 	else if (temp < 63){
        cpu_online_wrapper(1);
 		cpu_online_wrapper(2);
 		cpu_online_wrapper(3);
 		cpu_online_wrapper(4);
 		cpu_online_wrapper(5);
		cpu_online_wrapper(6);
 		cpu_online_wrapper(7);
	}
 }
 
#ifdef CONFIG_THUNDERCHARGE_CONTROL
if (mswitch == 1){
    if (temp >= 70)
		custom_current = 1000;
	else if (temp >= 60)
		custom_current = 1250;
	else if (temp >= 50)
		custom_current = 1350;
	else if (temp >= 40)
		custom_current = 1500;}
#endif
 
reschedule:
	queue_delayed_work(thermal_wq, &check_temp_work, msecs_to_jiffies(250));
}
static int set_temp_threshold(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	if (i < 20 || i > 100)
		return -EINVAL;
	
	LEVEL_VERY_HOT = i + TEMP_STEP;
	LEVEL_HOT = i + (TEMP_STEP * 2);
	LEVEL_HELL = i + (TEMP_STEP * 3);
	
	ret = param_set_int(val, kp);

	return ret;
}

static struct kernel_param_ops temp_threshold_ops = {
	.set = set_temp_threshold,
	.get = param_get_int,
};

module_param_cb(temp_threshold, &temp_threshold_ops, &TEMP_THRESHOLD, 0644);

static int set_temp_step(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	/*	Restrict the values to 1-6 as this will result in threshold + value - value *3
		without a restriction this could result in significanty higher than expected values*/
	if (i < 1 || i > 6)
		return -EINVAL;
	
	LEVEL_VERY_HOT = TEMP_THRESHOLD + i;
	LEVEL_HOT = TEMP_THRESHOLD + (i * 2);
	LEVEL_HELL = TEMP_THRESHOLD + (i * 3);
	
	ret = param_set_int(val, kp);

	return ret;
}

static struct kernel_param_ops temp_step_ops = {
	.set = set_temp_step,
	.get = param_get_int,
};

module_param_cb(temp_step, &temp_step_ops, &TEMP_STEP, 0644);

static int set_freq_limit(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	int i, cnt;
	int valid = 0;
	struct cpufreq_policy *policy;
	static struct cpufreq_frequency_table *tbl = NULL;
	
	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;

	policy = cpufreq_cpu_get(0);
	tbl = cpufreq_frequency_get_table(0);
	for (cnt = 0; (tbl[cnt].frequency != CPUFREQ_TABLE_END); cnt++) {
		if (cnt > 0)
			if (tbl[cnt].frequency == i)
				valid = 1;
	}
			
	if (strcmp( kp->name, "msm_thermal.freq_warm") == 0 && i <= FREQ_HOT) 
		return -EINVAL;
	if ( strcmp( kp->name, "msm_thermal.freq_hot") == 0 &&  ( i >= FREQ_WARM || i <= FREQ_VERY_HOT ))
		return -EINVAL;	
	if ( strcmp( kp->name, "msm_thermal.freq_very_hot") == 0 && ( i >= FREQ_HOT || i <= FREQ_HELL ))
		return -EINVAL;		
	if ( strcmp( kp->name, "msm_thermal.freq_hell") == 0 && i >= FREQ_VERY_HOT ) 
		return -EINVAL;		

	if (!valid)
		return -EINVAL;
	
	ret = param_set_int(val, kp);

	return ret;
}

static struct kernel_param_ops freq_limit_ops = {
	.set = set_freq_limit,
	.get = param_get_int,
};

module_param_cb(freq_hell, &freq_limit_ops, &FREQ_HELL, 0644);
module_param_cb(freq_very_hot, &freq_limit_ops, &FREQ_VERY_HOT, 0644);
module_param_cb(freq_hot, &freq_limit_ops, &FREQ_HOT, 0644);
module_param_cb(freq_warm, &freq_limit_ops, &FREQ_WARM, 0644);

static int set_temp_safety(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	int i;

	ret = kstrtouint(val, 10, &i);
	if (ret)
		return -EINVAL;
	if (i < 0 || i > 1)
		return -EINVAL;
	if (AiO_HotPlug)
		return -EINVAL;	
	ret = param_set_int(val, kp);
    if (!TEMP_SAFETY)
	{
	   int cpu;
	   for_each_possible_cpu(cpu)
	       if (!cpu_online(cpu))
		  cpu_online_wrapper(cpu);
	} 

	return ret;
}

static struct kernel_param_ops temp_safety_ops = {
	.set = set_temp_safety,
	.get = param_get_int,
};

module_param_cb(temp_safety, &temp_safety_ops, &TEMP_SAFETY, 0644);


static int msm_thermal_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *node = pdev->dev.of_node;
	struct msm_thermal_data data;

	memset(&data, 0, sizeof(struct msm_thermal_data));

	ret = of_property_read_u32(node, "qcom,sensor-id", &data.sensor_id);
	if (ret)
		return ret;

	WARN_ON(data.sensor_id >= TSENS_MAX_SENSORS);

        memcpy(&msm_thermal_info, &data, sizeof(struct msm_thermal_data));

	ret = cpufreq_register_notifier(&msm_thermal_cpufreq_notifier,
		CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		pr_err("thermals: well, if this fails here, we're fucked\n");

	thermal_wq = alloc_workqueue("thermal_wq", WQ_HIGHPRI, 0);
	if (!thermal_wq) {
		pr_err("thermals: don't worry, if this fails we're also bananas\n");
		goto err;
	}

	INIT_DELAYED_WORK(&check_temp_work, check_temp);
	queue_delayed_work(thermal_wq, &check_temp_work, 5);

err:
	return ret;
}

static int msm_thermal_dev_remove(struct platform_device *pdev)
{
	cancel_delayed_work_sync(&check_temp_work);
	destroy_workqueue(thermal_wq);
	cpufreq_unregister_notifier(&msm_thermal_cpufreq_notifier,
                        CPUFREQ_POLICY_NOTIFIER);
	return 0;
}

static struct of_device_id msm_thermal_match_table[] = {
	{.compatible = "qcom,msm-thermal"},
	{},
};

static struct platform_driver msm_thermal_device_driver = {
	.probe = msm_thermal_dev_probe,
	.remove = msm_thermal_dev_remove,
	.driver = {
		.name = "msm-thermal",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_match_table,
	},
};

static int __init msm_thermal_device_init(void)
{
	return platform_driver_register(&msm_thermal_device_driver);
}

static void __exit msm_thermal_device_exit(void)
{
	platform_driver_unregister(&msm_thermal_device_driver);
}

arch_initcall(msm_thermal_device_init);
module_exit(msm_thermal_device_exit);
