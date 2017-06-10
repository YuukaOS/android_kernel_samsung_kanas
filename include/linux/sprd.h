/*
 * include/linux/sprd.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _LINUX_SPRD_H
#define _LINUX_SPRD_H

/* On-demand governor macros */
#define DEF_FREQUENCY_DOWN_DIFFERENTIAL		(10)
#define DEF_FREQUENCY_UP_THRESHOLD		(80)
#define DEF_SAMPLING_DOWN_FACTOR		(1)
#define MAX_SAMPLING_DOWN_FACTOR		(100000)
#define MICRO_FREQUENCY_DOWN_DIFFERENTIAL	(3)
#define MICRO_FREQUENCY_UP_THRESHOLD		(95)
#define MICRO_FREQUENCY_MIN_SAMPLE_RATE		(10000)
#define MIN_FREQUENCY_UP_THRESHOLD		(11)
#define MAX_FREQUENCY_UP_THRESHOLD		(100)

/* whether plugin cpu according to this score up threshold */
#define DEF_CPU_SCORE_UP_THRESHOLD		(100)
/* whether unplug cpu according to this down threshold*/
#define DEF_CPU_LOAD_DOWN_THRESHOLD		(20)
#define DEF_CPU_DOWN_COUNT		(3)

#define LOAD_CRITICAL 100
#define LOAD_HI 90
#define LOAD_MID 80
#define LOAD_LIGHT 50
#define LOAD_LO 0

#define LOAD_CRITICAL_SCORE 10
#define LOAD_HI_SCORE 5
#define LOAD_MID_SCORE 0
#define LOAD_LIGHT_SCORE -10
#define LOAD_LO_SCORE -20

#define GOVERNOR_BOOT_TIME	(50*HZ)

#define MAX_CPU_NUM  (4)
#define MAX_PERCPU_TOTAL_LOAD_WINDOW_SIZE  (8)
#define MAX_PLUG_AVG_LOAD_SIZE (2)

extern unsigned int cur_window_size[MAX_CPU_NUM];
extern unsigned int prev_window_size[MAX_CPU_NUM];

extern int a_score_sub[4][4][11];
extern unsigned int a_sub_windowsize[8][6];
extern int ga_samp_rate[11];

extern int cpu_score;

extern int cur_window_index[MAX_CPU_NUM];
extern unsigned int cur_window_cnt[MAX_CPU_NUM];
extern int first_window_flag[4];

extern unsigned int ga_percpu_total_load[MAX_CPU_NUM][MAX_PERCPU_TOTAL_LOAD_WINDOW_SIZE];

extern unsigned int sum_load[4];
extern unsigned int percpu_load[4];

extern unsigned int dvfs_unplug_select;
extern unsigned int dvfs_plug_select;
extern unsigned int dvfs_score_select;
extern unsigned int dvfs_score_hi[4];
extern unsigned int dvfs_score_mid[4];
extern unsigned int dvfs_score_critical[4];

extern struct cpufreq_conf *sprd_cpufreq_conf;
extern struct sd_dbs_tuners *g_sd_tuners;

int sd_adjust_window(struct sd_dbs_tuners *sd_tunners , unsigned int load);
int sd_adjust_window(struct sd_dbs_tuners *sd_tunners , unsigned int load);
unsigned int sd_unplug_avg_load1(int cpu, struct sd_dbs_tuners *sd_tunners , unsigned int load);
unsigned int sd_unplug_avg_load11(int cpu, struct sd_dbs_tuners *sd_tunners , unsigned int load);
int cpu_evaluate_score(int cpu, struct sd_dbs_tuners *sd_tunners , unsigned int load);

int sd_tuners_init(struct sd_dbs_tuners *tuners);
#endif /* _LINUX_SPRD_H */
