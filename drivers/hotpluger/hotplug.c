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

static LIST_HEAD(hotplugger_driver_list);
static DEFINE_MUTEX(hotplugger_driver_mutex);
atomic_t enabled = ATOMIC_INIT(1);

static struct hotplugger_driver *__find_driver(const char *name)
{
	struct hotplugger_driver *d = NULL;
	int ret;
#ifdef DEBUG
	int i = 0;
#endif

	pr_debug("%s was called\n", __func__);

	if (name == NULL) {
		pr_debug("%s: driver name search query is NULL\n", __func__);
		return NULL;
	}

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		if (!(ret = strnicmp(name, d->name, DRIVER_NAME_LEN)))
			return d;
#ifdef DEBUG
		pr_debug("%s: i is %d\n", __func__, ++i);
		pr_debug("%s: name is %s, driver is %s\n", __func__, name, d->name);
		pr_debug("%s: result is %d\n",  __func__, ret);
#endif
	}

	return NULL;
}

static struct hotplugger_driver *__find_driver_ptr(struct hotplugger_driver *driver)
{
	struct hotplugger_driver *d = NULL;
#ifdef DEBUG
	int i = 0;
#endif

	pr_debug("%s was called\n", __func__);

	if (driver == NULL) {
		pr_debug("%s: driver var is NULL\n", __func__);
		return NULL;
	}

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		if (driver == d)
			return d;
#ifdef DEBUG
		pr_debug("%s: i is %d\n", __func__, ++i);
		pr_debug("%s: driver at %p, d at %p\n", __func__, driver, d);
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
	if (name == NULL) {
		pr_debug("%s: driver name is NULL\n", __func__);
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
		pr_debug("%s: name is %s, list[%d] is %s\n", __func__, name, i, l);
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
	int ret = -EFAULT;
	
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
	}
#ifdef DEBUG
	  else if (d->is_enabled() == state) {
		pr_debug("%s: \"%s\" driver is already %s\n", __func__,
		         d->name, state ? "enabled" : "disabled");
	} else if (!d->change_state) {
		pr_debug("%s: change_state for \"%s\" driver is NULL\n", __func__,
		         d->name);
	}
#endif

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

	mutex_lock(&hotplugger_driver_mutex);

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		if (i >= buf_size) {
			pr_debug("%s: buffer is full...\n", __func__);
			break;
		}

		pr_debug("%s: j is %d\n", __func__, ++j);
		if (d && d->name && d->is_enabled && d->is_enabled() == state) {
			pr_debug("%s: i was %d\n", __func__, i);
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "%s ", d->name);
			pr_debug("%s: i is now %d\n", __func__, i);
		} 
#ifdef DEBUG
		  else if (!d->name) {
			pr_debug("%s: d->name is NULL\n", __func__);
		} else if (!d->is_enabled) {
			pr_debug("%s: d->is_enabled is NULL\n", __func__);
		} else if (!d) {
			pr_debug("%s: d is NULL\n", __func__);
		}
#endif
	}
	if (i == 0)
		i += sprintf(buf, "NaN");

	mutex_unlock(&hotplugger_driver_mutex);

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

	if (atomic_read(&enabled) <= 0) {
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	ret = sscanf(buf, "%31s", name);
	if (ret != 1) {
		pr_debug("%s: sscanf returns %d\n", __func__, ret);
		return -EINVAL;
	}

	new = __find_driver(name);

	if (new == NULL) {
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

/*************
 * sysfs start
 *************/

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
	pr_debug("%s: setting %s state\n",
	          __func__,
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
#ifdef DEBUG
	int j = 0;
#endif
	ssize_t i = 0;
	ssize_t buf_size = ((PAGE_SIZE / sizeof(char)) - (DRIVER_NAME_LEN + 2));

	pr_debug("%s was called\n", __func__);

	mutex_lock(&hotplugger_driver_mutex);

	list_for_each_entry(d, &hotplugger_driver_list, list) {
		if (i >= buf_size) {
			pr_debug("%s: buffer is full...\n", __func__);
			break;
		}
		pr_debug("%s: j is %d\n"
		         "%s: i was %d\n",
		          __func__, ++j, __func__, i);
		if (d->is_enabled())
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "[%s] ", d->name);
		else
			i += scnprintf(&buf[i], DRIVER_NAME_LEN + 1, "%s ", d->name);
		pr_debug("%s: i is now %d\n", __func__, i);
	}
	if (i == 0)
		sprintf(buf, "NaN");

	mutex_unlock(&hotplugger_driver_mutex);

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

/*************
 * sysfs end
 *************/

int hotplugger_register_driver(struct hotplugger_driver *driver)
{
	int err = -EINVAL;
	struct hotplugger_driver *d;

	pr_debug("%s was called\n", __func__);

	if (!driver) {
		pr_debug("%s: driver is NULL\n", __func__);
		return err;
	}

	if (!driver->name) {
		pr_debug("%s: driver name is NULL\n", __func__);
		return err;
	}

	mutex_lock(&hotplugger_driver_mutex);
	/* Checks */
	d = __find_driver(driver->name);
	if (d && (d != driver)) {
		pr_debug("%s: A driver with name \"%s\" exists!\n",
		          __func__, driver->name);
	} else if (d && d == driver) {
		pr_debug("%s: driver \"%s\" already registered\n",
		          __func__, driver->name);
	} else {
		list_add(&driver->list, &hotplugger_driver_list);
		err = 0;
	}
	mutex_unlock(&hotplugger_driver_mutex);

	return err;
}
EXPORT_SYMBOL_GPL(hotplugger_register_driver);

void hotplugger_unregister_driver(struct hotplugger_driver *driver)
{
	struct hotplugger_driver *d;

	pr_debug("%s was called\n", __func__);

	if (!driver) {
		pr_debug("%s: driver is NULL\n", __func__);
		return;
	}

	mutex_lock(&hotplugger_driver_mutex);
	d = __find_driver(driver->name);
	if ((d) && (d == driver)) {
			__state_change(NULL, driver, false);
			pr_debug("%s: Removing \"%s\" driver from list\n",
			          __func__, driver->name);
			list_del(&driver->list);
	} else if ((d) && (d != driver)) {
		pr_debug("%s: A driver with name \"%s\" exists but their pointers"
		         "differ [%p(list) =/= %p(yours)]\n",
		         __func__, driver->name, d, driver);
	} else {
		pr_debug("%s: No matching \"%s\" driver found!\n",
		         __func__, driver->name);
	}

	mutex_unlock(&hotplugger_driver_mutex);
	return;
}
EXPORT_SYMBOL_GPL(hotplugger_unregister_driver);

static int __init hotplugger_init(void)
{
	int ret;

	pr_debug("hotplugger sysfs init START!!!\n");
	ret = sysfs_create_group(kernel_kobj, &hotplugger_attr_group);
	if (ret) {
		pr_err("%s: sysfs_create_group failed\n", __func__);
		return ret;
	}
	pr_debug("hotplugger sysfs init END!!!\n");
	return 0;
}

int hotplugger_get_running(void) {
	int num = 0;
	struct hotplugger_driver *d = NULL;

	mutex_lock(&hotplugger_driver_mutex);
	list_for_each_entry(d, &hotplugger_driver_list, list)
		num++;
	mutex_unlock(&hotplugger_driver_mutex);

	return num;
}
EXPORT_SYMBOL_GPL(hotplugger_get_running);

int hotplugger_disable_conflicts(struct hotplugger_driver *driver)
{
	struct hotplugger_driver *d;
	int ret = 0;

	pr_debug("%s was called\n", __func__);

	if (driver == NULL) {
		pr_debug("%s: undefined driver.\n", __func__);
		pr_debug("%s: aborting\n", __func__);
		return -ENXIO;
	}

	if (mutex_is_locked(&hotplugger_driver_mutex)) {
		pr_debug("%s: another \"disabling\" is in progress\n", __func__);
		return -EBUSY;
	}

	mutex_lock(&hotplugger_driver_mutex);

	if (atomic_read(&enabled) <= 0) {
        pr_debug("%s: enabled = %d \n", __func__, atomic_read(&enabled));
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	/* TODO: refactor this whole function as it uses more than 3 linear searches
	 *       and a lot of duplications due to design error.
	 *       No one would try and register about 100 hotplug drivers/modules
	 *       right? */
	/* First, check if driver exists */
	d =__find_driver_ptr(driver);
	if (d == NULL) {
		pr_debug("%s: driver \"%s\" is unregistered\n", __func__, driver->name);
		pr_debug("%s: aborting...\n", __func__);
		ret = -ENODEV;
	} else {
		pr_debug("%s: driver \"%s\" requests conflict resolution\n", __func__,
		          driver->name);

		/* Check if the driver specified a white list*/
		if (driver->whitelist && *driver->whitelist != NULL) {
			pr_debug("%s: whitelist found!\n", __func__);
			pr_debug("%s: list address: %p \n", __func__, driver->whitelist);
			pr_debug("%s: first element: %p \n", __func__, *driver->whitelist);
			list_for_each_entry(d, &hotplugger_driver_list, list) {
				/* check if the other driver's name is on the whitelist */
				if (!d->is_enabled())
					continue;

				if (d != driver &&
					__find_name_in_list(driver->whitelist, d->name) == true) {
					pr_debug("%s: driver \"%s\" is whitelisted in  driver \"%s\".\n",
					          __func__, d->name, driver->name);

					/* We need both of the drivers to whitelist themselves as a
					   safety measure
					 */
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
		           (driver->whitelist && *driver->whitelist == NULL)) {
			pr_debug("%s: driver \"%s\" has empty lists, disabling all\n",
			          __func__, driver->name);
			list_for_each_entry(d, &hotplugger_driver_list, list)
				hotplugger_disable_one(driver, d);
		}
	}

	mutex_unlock(&hotplugger_driver_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(hotplugger_disable_conflicts);

int hotplugger_enable_one(const char *name)
{
	struct hotplugger_driver *d = __find_driver(name);

	pr_debug("%s was called with \"%s\" as its first parameter\n",
	         __func__, name);

	if (atomic_read(&enabled) <= 0) {
        pr_debug("%s: enabled = %d \n", __func__, atomic_read(&enabled));
		pr_debug("%s: hotplugger is disabled\n", __func__);
		return -EPERM;
	}

	if (d == NULL) {
		pr_debug("%s: \"%s\" driver cannot be found\n",
		          __func__, name);
		return -EINVAL;
	}

	pr_debug("%s: activating \"%s\" driver\n", __func__, name);

	return d->change_state(true);
}
EXPORT_SYMBOL_GPL(hotplugger_enable_one);

MODULE_AUTHOR("ME");
MODULE_DESCRIPTION("Manage hotplug modules so they won't run simultaneously"
                   "naivete style");
MODULE_LICENSE("GPL");

module_init(hotplugger_init);
