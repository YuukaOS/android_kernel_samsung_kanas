#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/percpu-defs.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/tick.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/earlysuspend.h>
#include <linux/suspend.h>
#include <asm/cacheflush.h>
#include <linux/input.h>
#include <linux/delay.h>

#include <linux/kthread.h>

#include "../cpufreq/cpufreq_governor.h"
#include <linux/sprd.h>

// #define CPU_HOTPLUG_DISABLE_WQ
#ifdef CPU_HOTPLUG_DISABLE_WQ
#define HOTPLUG_DISABLE_ACTION_NONE     0
#define HOTPLUG_DISABLE_ACTION_ACTIVE   1
static atomic_t hotplug_disable_state = ATOMIC_INIT(HOTPLUG_DISABLE_ACTION_NONE);
#endif

#define CPU_HOTPLUG_BOOT_DONE_TIME	(50 * HZ)
#define SPRD_HOTPLUG_SCHED_PERIOD_TIME	(100)

static struct kobject hotplug_kobj;
static struct task_struct *ksprd_hotplug;
static unsigned long boot_done;

static struct delayed_work plugin_work;
static struct delayed_work unplug_work;

u64 g_prev_cpu_wall[4] = {0};
u64 g_prev_cpu_idle[4] = {0};

static DEFINE_PER_CPU(struct od_cpu_dbs_info_s, sd_cpu_dbs_info);

#define mod(n, div) ((n) % (div))

static struct workqueue_struct *input_wq;

static DEFINE_PER_CPU(struct work_struct, dbs_refresh_work);

static void __cpuinit sprd_plugin_one_cpu_ss(struct work_struct *work)
{
	int cpuid, ret = 0, i;

#ifdef CONFIG_HOTPLUG_CPU
#ifdef CPU_HOTPLUG_DISABLE_WQ
	if (HOTPLUG_DISABLE_ACTION_ACTIVE == atomic_read(&hotplug_disable_state)) {
		unsigned int cpu;

		for_each_cpu(cpu, cpu_possible_mask) {
			if (!cpu_online(cpu)) {
				cpu_up(cpu);
			}
		}
		atomic_set(&hotplug_disable_state,HOTPLUG_DISABLE_ACTION_NONE);
		printk("%s: all cpus were pluged in.\n", __func__);
		return;
	}
#endif

	if (num_online_cpus() < g_sd_tuners->cpu_num_limit) {
		cpuid = cpumask_next_zero(0, cpu_online_mask);
		if (!g_sd_tuners->cpu_hotplug_disable) {
			pr_info("!!  we gonna plugin cpu%d  !!\n", cpuid);
			for (i = 0; i < 5; i++) {
				ret = cpu_up(cpuid);
				if (ret != -ENOSYS)
					break;
			}
		}
	}
#endif
	return;
}

static void sprd_unplug_one_cpu_ss(struct work_struct *work)
{
	unsigned int cpuid = 0;

#ifdef CONFIG_HOTPLUG_CPU
	if (num_online_cpus() > 1) {
		if (!g_sd_tuners->cpu_hotplug_disable) {
			cpuid = cpumask_next(0, cpu_online_mask);
			pr_info("!!  we gonna unplug cpu%d  !!\n",cpuid);
			cpu_down(cpuid);
		}
	}
#endif
	return;
}

void sd_check_cpu_sprd(unsigned int load)
{
	unsigned int itself_avg_load = 0;
// 	struct unplug_work_info *puwi;
	int cpu_num_limit = 0;
	
	struct od_cpu_dbs_info_s *dbs_info = &per_cpu(sd_cpu_dbs_info, 0);

	if(time_before(jiffies, boot_done))
		return;

	if(g_sd_tuners->cpu_hotplug_disable)
		return;

	/* cpu plugin check */
	cpu_num_limit = min(g_sd_tuners->cpu_num_min_limit,g_sd_tuners->cpu_num_limit);
	if (num_online_cpus() < cpu_num_limit) {
		pr_debug("cpu_num_limit=%d, begin plugin cpu!\n",cpu_num_limit);
		schedule_delayed_work_on(0, &plugin_work, 0);
	}
	else {
		cpu_score += cpu_evaluate_score(0,g_sd_tuners, load);

		pr_debug("cpu_score %d %x\n",cpu_score,cpu_score);

		if (cpu_score < 0)
			cpu_score = 0;
		if (cpu_score >= g_sd_tuners->cpu_score_up_threshold
			 &&(num_online_cpus() < g_sd_tuners->cpu_num_limit)) {
			pr_debug("cpu_score=%d, begin plugin cpu!\n", cpu_score);
			cpu_score = 0;
			schedule_delayed_work_on(0, &plugin_work, 0);
		}
	}

	/* don't to check unplug if online cpus is less than or equal to cpu min limit */
	if(num_online_cpus() <= cpu_num_limit)
		return;

	/* cpu unplug check */
	cpu_num_limit = max(g_sd_tuners->cpu_num_min_limit,g_sd_tuners->cpu_num_limit);
	if(num_online_cpus() > 1 && (dvfs_unplug_select == 2))
	{
		/* calculate itself's average load */
		itself_avg_load = sd_unplug_avg_load1(0, g_sd_tuners, load);
		pr_debug("check unplug: for cpu%u avg_load=%d\n", 0, itself_avg_load);
		if((num_online_cpus() > cpu_num_limit)
			|| ((itself_avg_load < g_sd_tuners->cpu_down_threshold)
				&&(num_online_cpus() > g_sd_tuners->cpu_num_min_limit)))
		{
			pr_debug("cpu%u's avg_load=%d,begin unplug cpu\n",
					0, itself_avg_load);
			percpu_load[0] = 0;
			cur_window_size[0] = 0;
			cur_window_index[0] = 0;
			cur_window_cnt[0] = 0;
			prev_window_size[0] = 0;
			first_window_flag[0] = 0;
			sum_load[0] = 0;
			memset(&ga_percpu_total_load[0][0],0,sizeof(int) * MAX_PERCPU_TOTAL_LOAD_WINDOW_SIZE);
			schedule_delayed_work_on(0, &unplug_work, 0);
		}
	}
	else if(num_online_cpus() > 1 && (dvfs_unplug_select > 2))
	{
		/* calculate itself's average load */
		itself_avg_load = sd_unplug_avg_load11(0, g_sd_tuners, load);
		pr_debug("check unplug: for cpu%u avg_load=%d\n", 0, itself_avg_load);
		if((num_online_cpus() > cpu_num_limit) 
			|| ((itself_avg_load < g_sd_tuners->cpu_down_threshold)
				&&(num_online_cpus() > g_sd_tuners->cpu_num_min_limit)))
		{
			pr_debug("cpu%u's avg_load=%d,begin unplug cpu\n",
					0, itself_avg_load);
			percpu_load[0] = 0;
			cur_window_size[0] = 0;
			cur_window_index[0] = 0;
			cur_window_cnt[0] = 0;
			prev_window_size[0] = 0;
			first_window_flag[0] = 0;
			sum_load[0] = 0;
			memset(&ga_percpu_total_load[0][0],0,sizeof(int) * MAX_PERCPU_TOTAL_LOAD_WINDOW_SIZE);
			schedule_delayed_work_on(0, &unplug_work, 0);
		}
	}
}

void dbs_check_cpu_sprd(void)
{
	unsigned int max_load = 0;
	unsigned int j;

	/* Get Absolute Load */
	for_each_cpu(j, cpu_online_mask) {
		u64 cur_wall_time, cur_idle_time;
		unsigned int idle_time, wall_time;
		unsigned int load;
		int io_busy = 0;
		u64 prev_cpu_wall;
		u64 prev_cpu_idle;

		prev_cpu_wall = g_prev_cpu_wall[j];
		prev_cpu_idle = g_prev_cpu_idle[j];

		/*
		 * For the purpose of ondemand, waiting for disk IO is
		 * an indication that you're performance critical, and
		 * not that the system is actually idle. So do not add
		 * the iowait time to the cpu idle time.
		 */
		cur_idle_time = get_cpu_idle_time(j, &cur_wall_time, io_busy);

		wall_time = (unsigned int)
			(cur_wall_time - prev_cpu_wall);

		idle_time = (unsigned int)
			(cur_idle_time - prev_cpu_idle);

		g_prev_cpu_wall[j] = cur_wall_time;
		g_prev_cpu_idle[j] = cur_idle_time;

		if (unlikely(!wall_time || wall_time < idle_time))
			continue;

		load = 100 * (wall_time - idle_time) / wall_time;

// 		pr_debug("***[cpu %d]cur_idle_time %lld prev_cpu_idle %lld cur_wall_time %lld prev_cpu_wall %lld wall_time %ld idle_time %ld load %ld\n",
// 			j,cur_idle_time,prev_cpu_idle,cur_wall_time,prev_cpu_wall,wall_time,idle_time,load);

		if (load > max_load)
			max_load = load;
	}

	sd_check_cpu_sprd(max_load);
}
#if 0
int _store_cpu_num_limit(unsigned int input)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;

   	printk("%s: input = %d\n", __func__, input);

	if(sd_tuners)
	{
		sd_tuners->cpu_num_limit = input;
		sd_check_cpu_sprd(50);
	}
	else
	{
		pr_info("[store_cpu_num_min_limit] current governor is not sprdemand\n");
		return -EINVAL;
	}

	return 0;
}

int _store_cpu_num_min_limit(unsigned int input)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;

    printk("%s: input = %d\n", __func__, input);

	if(sd_tuners)
	{
		sd_tuners->cpu_num_min_limit = input;
		sd_check_cpu_sprd(50);
	}
	else
	{
		pr_info("[store_cpu_num_min_limit] current governor is not sprdemand\n");
		return -EINVAL;
	}

	return 0;
}
#endif
static int should_io_be_busy(void)
{
	return 0;
}

static int sprd_hotplug(void *data)
{
	unsigned int timeout_ms = SPRD_HOTPLUG_SCHED_PERIOD_TIME; //100

	pr_debug("-start!\n");

	do {
		if (time_before(jiffies, boot_done))
			goto wait_for_boot_done;

		dbs_check_cpu_sprd();

wait_for_boot_done :
		schedule_timeout_interruptible(msecs_to_jiffies(timeout_ms));

	} while (!kthread_should_stop());

	pr_debug("-exit! \n");
	return 0;
}

static ssize_t store_cpu_num_limit(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		return -EINVAL;
	}
	sd_tuners->cpu_num_limit = input;
	return count;
}

static ssize_t show_cpu_num_limit(struct device *dev, struct device_attribute *attr,char *buf)
{
	snprintf(buf,10,"%d\n",g_sd_tuners->cpu_num_limit);
	return strlen(buf) + 1;
}

static ssize_t store_cpu_num_min_limit(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		return -EINVAL;
	}
	sd_tuners->cpu_num_min_limit = input;
	return count;
}

static ssize_t show_cpu_num_min_limit(struct device *dev, struct device_attribute *attr,char *buf)
{
	snprintf(buf,10,"%d\n",g_sd_tuners->cpu_num_min_limit);
	return strlen(buf) + 1;
}

static ssize_t store_cpu_score_up_threshold(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		return -EINVAL;
	}
	sd_tuners->cpu_score_up_threshold = input;
	return count;
}

static ssize_t show_cpu_score_up_threshold(struct device *dev, struct device_attribute *attr,char *buf)
{
	snprintf(buf,10,"%d\n",g_sd_tuners->cpu_score_up_threshold);
	return strlen(buf) + 1;
}

static ssize_t store_cpu_down_threshold(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)
{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		return -EINVAL;
	}
	sd_tuners->cpu_down_threshold = input;
	return count;
}

static ssize_t show_cpu_down_threshold(struct device *dev, struct device_attribute *attr,char *buf)
{
	snprintf(buf,10,"%d\n",g_sd_tuners->cpu_down_threshold);
	return strlen(buf) + 1;
}

static ssize_t __ref store_cpu_hotplug_disable(struct device *dev, struct device_attribute *attr,const char *buf, size_t count)

{
	struct sd_dbs_tuners *sd_tuners = g_sd_tuners;
	unsigned int input;
#ifndef CPU_HOTPLUG_DISABLE_WQ
	unsigned int cpu;
	int i;
#endif
	int ret;

	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		return -EINVAL;
	}

	if (sd_tuners->cpu_hotplug_disable == input) {
		return count;
	}
	if (sd_tuners->cpu_num_limit > 1)
		sd_tuners->cpu_hotplug_disable = input;

	smp_wmb();
	/* plug-in all offline cpu mandatory if we didn't
	 * enable CPU_DYNAMIC_HOTPLUG
         */
#ifdef CONFIG_HOTPLUG_CPU
	if (sd_tuners->cpu_hotplug_disable) {
#ifdef CPU_HOTPLUG_DISABLE_WQ
		atomic_set(&hotplug_disable_state, HOTPLUG_DISABLE_ACTION_ACTIVE);
		schedule_delayed_work_on(0, &plugin_work, 0);
#else
		for_each_cpu(cpu, cpu_possible_mask) {
			if (!cpu_online(cpu))
				{
					for (i = 0; i < 5; i++) {
						ret = cpu_up(cpu);
						if (ret != -ENOSYS)
							break;
					}
				}
		}
#endif
	}
#endif
	return count;
}

static ssize_t show_cpu_hotplug_disable(struct device *dev, struct device_attribute *attr,char *buf)
{
	snprintf(buf,10,"%d\n",g_sd_tuners->cpu_hotplug_disable);
	return strlen(buf) + 1;
}

static DEVICE_ATTR(cpu_num_limit, 0660, show_cpu_num_limit,store_cpu_num_limit);
static DEVICE_ATTR(cpu_num_min_limit, 0660, show_cpu_num_min_limit,store_cpu_num_min_limit);
static DEVICE_ATTR(cpu_score_up_threshold, 0660, show_cpu_score_up_threshold,store_cpu_score_up_threshold);
static DEVICE_ATTR(cpu_down_threshold, 0660, show_cpu_down_threshold,store_cpu_down_threshold);
static DEVICE_ATTR(cpu_hotplug_disable, 0660, show_cpu_hotplug_disable,store_cpu_hotplug_disable);

static struct attribute *g[] = {
	&dev_attr_cpu_num_limit.attr,
	&dev_attr_cpu_num_min_limit.attr,
	&dev_attr_cpu_score_up_threshold.attr,
	&dev_attr_cpu_down_threshold.attr,
	&dev_attr_cpu_hotplug_disable.attr,
	NULL,
};

static struct kobj_type hotplug_dir_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_attrs	= g,
};

static void dbs_refresh_callback(struct work_struct *work)
{
#if 0
	unsigned int cpu = smp_processor_id();

	struct cpufreq_policy *policy = NULL;

	if (!policy)
	{
		return;
	}

	if (policy->cur < policy->max)
	{
		policy->cur = policy->max;

		cpufreq_driver_target(policy, policy->max, CPUFREQ_RELATION_H);

		g_prev_cpu_idle[cpu] = get_cpu_idle_time(cpu,
				&g_prev_cpu_wall[cpu],should_io_be_busy());
	}
#endif
}

static void dbs_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	int i;
	bool ret;

	for_each_online_cpu(i)
	{
		ret = queue_work_on(i, input_wq, &per_cpu(dbs_refresh_work, i));
		pr_debug("[DVFS] dbs_input_event %d\n",ret);
	}
}

static int dbs_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	pr_info("[DVFS] dbs_input_connect register success\n");
	return 0;
err1:
	pr_info("[DVFS] dbs_input_connect register fail err1\n");
	input_unregister_handle(handle);
err2:
	pr_info("[DVFS] dbs_input_connect register fail err2\n");
	kfree(handle);
	return error;
}

static void dbs_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id dbs_ids[] = {
	{ .driver_info = 1 },
	{ },
};

struct input_handler dbs_input_handler = {
	.event		= dbs_input_event,
	.connect	= dbs_input_connect,
	.disconnect	= dbs_input_disconnect,
	.name		= "cpufreq_ond",
	.id_table	= dbs_ids,
};

static int __init sprd_hotplug_init(void)
{
	int i;
	int ret; 
	
	boot_done = jiffies + CPU_HOTPLUG_BOOT_DONE_TIME;

	if (!g_sd_tuners)
		g_sd_tuners = kzalloc(sizeof(struct sd_dbs_tuners), GFP_KERNEL);
	
	sd_tuners_init(g_sd_tuners);

	INIT_DELAYED_WORK(&plugin_work, sprd_plugin_one_cpu_ss);
	INIT_DELAYED_WORK(&unplug_work, sprd_unplug_one_cpu_ss);

	input_wq = alloc_workqueue("iewq", WQ_MEM_RECLAIM|WQ_SYSFS, 1);

	if (!input_wq)
	{
		printk(KERN_ERR "Failed to create iewq workqueue\n");
		return -EFAULT;
	}

	for_each_possible_cpu(i)
	{
		INIT_WORK(&per_cpu(dbs_refresh_work, i), dbs_refresh_callback);
	}

	ksprd_hotplug = kthread_create(sprd_hotplug,NULL,"sprd_hotplug");

	wake_up_process(ksprd_hotplug);

	ret = kobject_init_and_add(&hotplug_kobj, &hotplug_dir_ktype,
				   &(cpu_subsys.dev_root->kobj), "cpuhotplug");
	if (ret) {
		pr_err("%s: Failed to add kobject for hotplug\n", __func__);
	}
	return 0;
}

MODULE_AUTHOR("sprd");

MODULE_LICENSE("GPL");

module_init(sprd_hotplug_init);
