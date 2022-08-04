// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/net_namespace.h>
#include "core.h"


static struct wlan_ptracker_core ptracker_core;

#define get_core() (&ptracker_core)

#define client_to_core(client) \
	((struct wlan_ptracker_core *)((client)->core))


/* Default mapping rule follow 802.11e */
static const int dscp_trans[WMM_AC_MAX][DSCP_MAP_MAX] = {
	{0, 24, 26, 28, 30, -1}, /* AC_BE */
	{8, 10, 12, 14, 16, 18, 20, 22, -1}, /* AC_BK */
	{32, 34, 36, 38, 40, 46, -1}, /* AC_VI */
	{48, 56, -1}, /* AC_VO */
};

static void dscp_to_ac_init(u8 *dscp_to_ac)
{
	int i, j;

	for (i = 0 ; i < WMM_AC_MAX; i++) {
		for (j = 0 ; j < DSCP_MAP_MAX; j++) {
			int dscp = dscp_trans[i][j];

			if (dscp == -1)
				break;
			dscp_to_ac[dscp] = i;
		}
	}
}

static int wlan_ptracker_core_init(struct wlan_ptracker_core *core)
{
	memset(core, 0, sizeof(*core));
	device_initialize(&core->device);
	dev_set_name(&core->device, PTRACKER_PREFIX);
	device_add(&core->device);
	dscp_to_ac_init(core->dscp_to_ac);
	wlan_ptracker_debugfs_init(&core->debugfs);
	wlan_ptracker_notifier_init(&core->notifier);
	scenes_fsm_init(&core->fsm);
	dytwt_init(core);
	return 0;
}

static void wlan_ptracker_core_exit(struct wlan_ptracker_core *core)
{
	dytwt_exit(core);
	scenes_fsm_exit(&core->fsm);
	wlan_ptracker_notifier_exit(&core->notifier);
	wlan_ptracker_debugfs_exit(&core->debugfs);
	device_del(&core->device);
	memset(core, 0, sizeof(struct wlan_ptracker_core));
}

static int client_event_handler(void *priv, u32 event)
{
	struct wlan_ptracker_client *client = priv;
	struct wlan_ptracker_core *core = client_to_core(client);

	return wlan_ptracker_call_chain(&core->notifier, event, core);
}

int wlan_ptracker_register_client(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = get_core();

	if (!core->client) {
		rcu_read_lock();
		rcu_assign_pointer(core->client, client);
		rcu_read_unlock();
		client->cb = client_event_handler;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(wlan_ptracker_register_client);

void wlan_ptracker_unregister_client(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = get_core();

	if (core->client == client) {
		client->cb = NULL;
		rcu_read_lock();
		rcu_assign_pointer(core->client, NULL);
		rcu_read_unlock();
	}
}
EXPORT_SYMBOL_GPL(wlan_ptracker_unregister_client);

static int __init wlan_ptracker_init(void)
{
	struct wlan_ptracker_core *core = get_core();
	int ret;

	ret = wlan_ptracker_core_init(core);
	if (ret)
		goto err;
	dev_dbg(&core->device, "module init\n");
	return 0;
err:
	wlan_ptracker_core_exit(core);
	return ret;
}

static void __exit wlan_ptracker_exit(void)
{
	struct wlan_ptracker_core *core = get_core();

	dev_dbg(&core->device, "module exit\n");
	wlan_ptracker_core_exit(core);
}

module_init(wlan_ptracker_init);
module_exit(wlan_ptracker_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("WiFi Performance Tracker");
