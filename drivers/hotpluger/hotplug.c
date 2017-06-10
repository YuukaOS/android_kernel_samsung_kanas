/*
 *  linux/drivers/hotplugger/hotplug.c
 *
 * Copyright (c) 2017, Mark Enriquez <enriquezmark36@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include <linux/hotplugger.h>

static int num_drivers_loaded = 0;
static LIST_HEAD(hotplugger_driver_list);
static DEFINE_MUTEX(hotplugger_driver_mutex);
atomic_t enabled = ATOMIC_INIT(1);

static struct hotplugger_driver *__find_driver(const char *name)
{
	struct hotplugger_driver *d;
	int ret;
#ifdef DEBUG
	int i = 0;
#endif

	pr_debug("%s was called\n", __func__);

	list_for_each_entry(d , &hotplugger_driver_list, list) {
		if (!(ret = strnicmp(name, d->name, DRIVER_NAME_LEN)))
			return d;
#ifdef DEBUG
		pr_debug("%s: i is %d\n", __func__, ++i);
		pr_debug("%s: name is %s, driver is %s\n", __func__, name , d->name);
		pr_debug("%s: result is %d\n",  __func__, ret);
#endif
	}

	return NULL;
}

static bool __find_name_in_list(char **list, char *name)
{
	int i = 0;
	char *l;

	pr_debug("%s was called\n", __func__);

	if (list == NULL) {
		pr_debug("%s: list is NULL\n", __func__);
		return false;
	}
	if (*list == NULL) {
		pr_debug("%s: list is EMPTY\n", __func__);
		return false;
	}

#ifdef DEBUG
	pr_debug("%s: ----------list start----------\n", __func__);

	for ( i = 0 ; (l = *list) ; i++, list++)
		pr_debug("%s: list[%d] = \"%s\"\n", __func__, i, l);

	pr_debug("%s: ----------list end-----------\n", __func__);
#endif

	for ( i = 0 ; (l = *list) ; i++, list++) {
		pr_debug("%s: i is %d\n", __func__, i);
		pr_debug("%s: name is %s, list[%d] is %s\n", __func__, name , i, l);
		if (!strnicmp(l, name, DRIVER_NAME_LEN)) {
			pr_debug("%s: result is TRUE\n", __func__);
			return true;
		} else {
			pr_debug("%s: result is FALSE\n", __func__);
		}
	}

	return NULL;
}

static inline int __state_change(struct hotplugger_driver *caller,
                                 struct hotplugger_driver *d,
                                 bool state)
{
	int ret = 0;
	
	pr_debug("%s was called\n", __func__);

	if ((d) && (d != caller) && d->change_state && 
	    (d->is_enabled() != state)) {
		pr_debug("%s: %s \"%s\" driver \n", __func__,
		          state ? "enabling" : "disabling", d->name);

		ret = d->change_state(state);

		if (ret) {
			pr_debug("%s: %s: %pf failed with err %d\n", __func__,
		         d->name, d->change_state, ret);
		}
	} else if (d->is_enabled() == state) {
		pr_debug("%s: \"%s\" driver is already %s\n", __func__,
		         d->name, state ? "enabled" : "disabled");
		ret = -EFAULT;
	} else if (!d->change_state) {
		pr_debug("%s: change_state for \"%s\" driver is NULL\n", __func__,
		         d->name);
		ret = -EFAULT;
	}

	return ret;
}

static inline int hotplugger_disable_one(struct hotplugger_driver *caller,
                                          struct hotplugger_driver *d)
{
	pr_debug("%s was called\n", __func__);

	return __state_change(caller, d, false);
}

static inline ssize_t __show_drivers_by_state(char *buf, bool state)
{
	struct hotplugger_driver *d;
	ssize_t buf_size = ((PAGE_SIZE / sizeof(char)) - (DRIVER_NAME_LEN + 2));
	ssize_t i = 0;
	int j = 0;

	pr_debug("%s was called\n", __func__);

	if (!num_drivers_loaded) {
		i += sprintf(buf, "NaN");
		goto out;
	}

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		pr_debug("%s: j is %d\n", __func__, ++j);
		if (i >= buf_size)
			goto out;
		if (d->is_enabled() == state) {
			pr_debug("%s: i was %d\n", __func__, i);
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "%s ", d->name);
			pr_debug("%s: i is now %d\n", __func__, i);
		}
	}
out:
	i += sprintf(&buf[i], "\n");
	pr_debug("%s: printed %d bytes\n", __func__, i);
	return i;
}

static inline ssize_t __store_state_by_name(const char *buf, 
                                          size_t count, bool state)
{
	unsigned int ret;
	char name[DRIVER_NAME_LEN];
	struct hotplugger_driver *new;

	pr_debug("%s was called\n", __func__);

	if (atomic_read(&enabled) <= 0){
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	ret = sscanf(buf, "%31s", name);
	if (ret != 1) {
		pr_debug("%s: sscanf returns %d\n", __func__, ret);
		return -EINVAL;
	}

	new = __find_driver(name);

	if (new == NULL){
		pr_debug("%s: \"%s\" driver is not found\n", __func__, name);
		return -EINVAL;
	} else {
		pr_debug("%s: \"%s\" driver found!\n", __func__, name);
	}

	pr_debug("%s: calling \"%s\" driver's change_state()\n", __func__, name);
	ret = __state_change(NULL, new, state);

	if (ret)
		return ret;
	else
		return count;
}

/*********************************sysfs start*********************************/

static ssize_t show_enabled(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
	ssize_t i;

	pr_debug("%s was called\n", __func__);

	i = snprintf(buf,10, "%d\n", atomic_read(&enabled));

	return i;
}

static ssize_t store_enabled(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf, size_t count)
{
	bool state;
	int ret;
	unsigned int input;

	pr_debug("%s was called\n", __func__);

	ret = sscanf(buf, "%u", &input);

	if (ret != 1) {
		pr_debug("%s: sscanf returns %d\n", __func__, ret);
		return -EINVAL;
	}

	state = input > 0 ? true : false;
	pr_debug("%s: %s functions\n", __func__, 
	          state ? "enabling" : "disabling");
	pr_debug("%s: setting %s state\n", __func__,
	          state ? "enabled" : "disabled");
	atomic_set(&enabled, state);

	return count;
}

static ssize_t show_enable_driver(struct device *dev,
                                   struct device_attribute *attr,char *buf)
{
	pr_debug("%s was called\n", __func__);

	return __show_drivers_by_state(buf, true);
}

static ssize_t store_enable_driver(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
	pr_debug("%s was called\n", __func__);

	return __store_state_by_name(buf, count, true);
}

static ssize_t show_disable_driver(struct device *dev,
                                   struct device_attribute *attr,char *buf)
{
	pr_debug("%s was called\n", __func__);

	return __show_drivers_by_state(buf, false);
}

static ssize_t store_disable_driver(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
	pr_debug("%s was called\n", __func__);

	return __store_state_by_name(buf, count, false);
}

static ssize_t show_available_drivers(struct device *dev,
                                      struct device_attribute *attr,
                                      char *buf)
{
	struct hotplugger_driver *d;
	int j = 0;
	ssize_t i = 0;
	ssize_t buf_size = ((PAGE_SIZE / sizeof(char)) - (DRIVER_NAME_LEN + 2));

	pr_debug("%s was called\n", __func__);

	if (!num_drivers_loaded) {
		i += sprintf(buf, "NaN");
		goto out;
	}

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		pr_debug("%s: j is %d\n", __func__, ++j);
		if (i >= buf_size)
			goto out;
		pr_debug("%s: i was %d\n", __func__, i);
		if (d->is_enabled())
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "[%s] ", d->name);
		else
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "%s ", d->name);
		pr_debug("%s: i is now %d\n", __func__, i);
	}
out:
	i += sprintf(&buf[i], "\n");
	pr_debug("%s: finishing with %d bytes\n", __func__, i);
	return i;
}

static DEVICE_ATTR(available_drivers, 0444, show_available_drivers, NULL);
static DEVICE_ATTR(disable_driver, 0644, show_disable_driver, store_disable_driver);
static DEVICE_ATTR(enable_driver, 0644, show_enable_driver, store_enable_driver);
static DEVICE_ATTR(enabled, 0644, show_enabled, store_enabled);

static struct attribute *hotplugger_attrs[] = {
	&dev_attr_available_drivers.attr,
	&dev_attr_disable_driver.attr,
	&dev_attr_enable_driver.attr,
	&dev_attr_enabled.attr,
	NULL
};

static struct attribute_group hotplugger_attr_group = {
	.attrs = hotplugger_attrs,
	.name = "hotplugger",
};

/*********************************sysfs end*********************************/

int hotplugger_register_driver(struct hotplugger_driver *driver)
{
	int err = -EINVAL;

	if (!driver)
		return err;

	mutex_lock(&hotplugger_driver_mutex);
	err = -EBUSY;
	if (__find_driver(driver->name) == NULL) {
		err = 0;
		num_drivers_loaded++;
		list_add(&driver->list, &hotplugger_driver_list);
	}
 
	mutex_unlock(&hotplugger_driver_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(hotplugger_register_driver);

void hotplugger_unregister_driver(struct hotplugger_driver *driver)
{
	if (!driver)
		return;

	if (__find_driver(driver->name) == NULL) {
		if (driver->is_enabled())
			driver->change_state(false);
	}

	mutex_lock(&hotplugger_driver_mutex);
	list_del(&driver->list);
	num_drivers_loaded--;
	mutex_unlock(&hotplugger_driver_mutex);
	return;
}
EXPORT_SYMBOL_GPL(hotplugger_unregister_driver);

static int __init hotplugger_init(void)
{
	int ret;

	pr_debug("hotplugger init START!!!\n");
	ret = sysfs_create_group(kernel_kobj, &hotplugger_attr_group);
	if (ret) {
		pr_err("%s: sysfs_create_group failed\n", __func__);
		return ret;
	}
	pr_debug("hotplugger init END!!!\n");
	return 0;
}

int hotplugger_disable_conflicts(struct hotplugger_driver *driver)
{
	struct hotplugger_driver *d;

	pr_debug("%s was called\n", __func__);

	if (driver == NULL) {
		pr_debug("%s: undefined driver.\n", __func__);
		pr_debug("%s: aborting\n", __func__);
		return -ENXIO;
	}

	if (mutex_is_locked(&hotplugger_driver_mutex)){
		pr_debug("%s: another \"disabling\" is in progress\n", __func__);
		return -EBUSY;
	}

	if (atomic_read(&enabled) <= 0){
        pr_debug("%s: enabled = %d \n", __func__, atomic_read(&enabled));
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	mutex_lock(&hotplugger_driver_mutex);

	/* TODO: refactor this whole function as it uses more than 3 linear searches
	 *       and a lot of duplications due to design error.
	 *       No one would try and register about 10K hotplug drivers/modules
	 *       right? */
	/* First check-if driver exists */
	list_for_each_entry(d, &hotplugger_driver_list, list) {
		if (d == driver)
			break;
	}
	if (d == NULL) {
		pr_debug("%s: driver \"%s\" is unregistered\n", __func__, driver->name);
		pr_debug("%s: aborting...\n", __func__);
		return -ENODEV;
	}

	pr_debug("%s: driver \"%s\" requests conflict resolution\n", __func__,
		   driver->name);

	/* Check if the driver specified a white list*/
	if (driver->whitelist && *driver->whitelist != NULL){
		pr_debug("%s: whitelist found!\n", __func__);
		pr_debug("%s: whitelist address: %p \n", __func__, driver->whitelist);
		pr_debug("%s: whitelist first: %p \n", __func__, *driver->whitelist);
		pr_debug("%s: first string is \"%s\"!\n", __func__, *driver->whitelist);
		list_for_each_entry(d , &hotplugger_driver_list, list) {
			/* check if the other driver's name is on the whitelist */
			if (!d->is_enabled())
				continue;

			if (d != driver &&
				  __find_name_in_list(driver->whitelist, d->name) == true) {
				pr_debug("%s: driver \"%s\" is whitelisted in  driver \"%s\".\n",
				         __func__, d->name, driver->name);

				/* We need both of the drivers to whitelist themselves as a 
				   safety measure */
				if (d->whitelist && *d->whitelist == NULL) {
				pr_debug("%s: BUT driver \"%s\" is NOT whitelisted"
				         " in  driver \"%s\".\n",
				         __func__, driver->name, d->name);
					hotplugger_disable_one(driver, d);
				} else if ((d->whitelist && *d->whitelist != NULL) &&
					__find_name_in_list(d->whitelist, driver->name) == true) {
					pr_debug("%s: Both are whitelisted for each other.\n",
							__func__);
					continue;
				}
			} else {
				hotplugger_disable_one(driver, d);
			}
		}
    } else if ((driver->whitelist == NULL) ||
		       (driver->whitelist && *driver->whitelist == NULL))
	{
		pr_debug("%s: driver \"%s\" has empty lists, disabling all\n",
			   __func__, driver->name);
		list_for_each_entry(d , &hotplugger_driver_list, list) {
			hotplugger_disable_one(driver, d);
		}
	}

	mutex_unlock(&hotplugger_driver_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(hotplugger_disable_conflicts);

int hotplugger_enable_one(const char *name)
{
	struct hotplugger_driver *d = __find_driver(name);

	pr_debug("%s was called with \"%s\" as its first parameter\n",
	         __func__, name);

	if (atomic_read(&enabled) <= 0){
        pr_debug("%s: enabled = %d \n", __func__, atomic_read(&enabled));
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	if (d == NULL) {
		pr_debug("%s: \"%s\" driver cannot be found\n", __func__, name);
		return -EINVAL;
	}

	pr_debug("%s: activating \"%s\" driver\n", __func__, name);

	return d->change_state(true);
}
EXPORT_SYMBOL_GPL(hotplugger_enable_one);

MODULE_AUTHOR("ME");

MODULE_LICENSE("GPL");

module_init(hotplugger_init);
