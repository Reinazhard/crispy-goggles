// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/debugfs.h>
#include "core.h"

static struct dytwt_manager dytwt_mgmt;

#define  dytwt_get_manager()  (&dytwt_mgmt)

static const char *const reason2str[WLAN_PTRACKER_NOTIFY_MAX] = {
	"tp", "scene_change", "scene_prep", "suspend", "sta_change",
};

#define DYMAIC_TWT_CONFIG_ID    3
#define TWT_WAKE_DURATION       8192
#define TWT_IDLE_INTERVAL       512000
#define TWT_WEB_INTERVAL        106496
#define TWT_YOUTUBE_INTERVAL    10240

static struct dytwt_scene_action dytwt_actions[WLAN_SCENE_MAX] = {
	{
		.action = TWT_ACTION_SETUP,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_WAKE_DURATION,
			.wake_interval = TWT_IDLE_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_SETUP,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_WAKE_DURATION,
			.wake_interval = TWT_WEB_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_SETUP,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_WAKE_DURATION,
			.wake_interval = TWT_YOUTUBE_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
		},
	},
};

static int dytwt_client_twt_setup(struct wlan_ptracker_client *client, u32 state)
{
	if (!client->dytwt_ops)
		return -EINVAL;

	if (!client->dytwt_ops->setup)
		return -EINVAL;

	if (state >= WLAN_SCENE_MAX)
		return -EINVAL;

	return client->dytwt_ops->setup(client->priv, &dytwt_actions[state].param);
}

static int dytwt_client_twt_teardown(struct wlan_ptracker_client *client, u32 state)
{
	if (!client->dytwt_ops)
		return -EINVAL;

	if (!client->dytwt_ops->teardown)
		return -EINVAL;

	if (state >= WLAN_SCENE_MAX)
		return -EINVAL;

	return client->dytwt_ops->teardown(client->priv, &dytwt_actions[state].param);
}

static bool dytwt_client_twt_cap(struct wlan_ptracker_client *client)
{
	struct dytwt_cap param;
	struct wlan_ptracker_core *core = client->core;
	int ret;

	if (!client->dytwt_ops)
		return false;

	if (!client->dytwt_ops->get_cap)
		return false;

	ret = client->dytwt_ops->get_cap(client->priv, &param);

	if (ret)
		return false;

	ptracker_dbg(core, "device: %d, peer: %d\n", param.device_cap, param.peer_cap);
	return param.peer_cap && param.device_cap;
}

static int dytwt_client_twt_pwrstates(struct wlan_ptracker_client *client,
	struct dytwt_pwr_state *state)
{
	if (!client->dytwt_ops)
		return -EINVAL;

	if (!client->dytwt_ops->get_pwrstates)
		return -EINVAL;

	return client->dytwt_ops->get_pwrstates(client->priv, state);
}

static inline void dytwt_record_get_pwr(u64 asleep, u64 awake, u64 *total, int *percent)
{
	/* for percent */
	*total = (asleep + awake) / 100;
	*percent = (*total == 0) ? 0 : (asleep / *total);
	/* trans 100 us to ms */
	*total /= 10;
}

static int dytwt_record_priv_read(void *cur, void *next, char *buf, int len)
{
	struct dytwt_entry *c = cur;
	struct dytwt_entry *n = next;
	int period_percent = 0, total_percent;
	u64 period_time = 0, total_time;

	/* get total */
	dytwt_record_get_pwr(c->pwr.asleep, c->pwr.awake, &total_time, &total_percent);

	/* get period */
	if (n) {
		u64 awake = n->pwr.awake > c->pwr.awake ?
			(n->pwr.awake - c->pwr.awake) : c->pwr.awake;
		u64 asleep = n->pwr.asleep > c->pwr.asleep ?
			(n->pwr.asleep - c->pwr.asleep) : c->pwr.asleep;
		dytwt_record_get_pwr(asleep, awake, &period_time, &period_percent);
	}

	return scnprintf(buf, len,
		"Applied: %s, Time: %llu (%llu) ms, Percent: %d%% (%d%%) Reason: %s, Rate: %d\n",
		c->apply ? "TRUE" : "FALSE", period_time, total_time, period_percent, total_percent,
		reason2str[c->reason], c->rate);
}

static void dytwt_mgmt_history_store(struct wlan_ptracker_client *client,
	struct dytwt_manager *dytwt, struct wlan_scene_event *msg, bool apply)
{
	struct dytwt_entry *entry;

	/* record assign base*/
	entry = wlan_ptracker_history_store(dytwt->hm, msg->dst);
	if (!entry)
		return;
	/* record private values */
	entry->apply = apply;
	entry->reason = msg->reason;
	entry->rate = msg->rate;
	dytwt_client_twt_pwrstates(client, &entry->pwr);
	/* prev will be used for decided teardown or not. */
	dytwt->prev = msg->dst;
}

#define TWT_HISTORY_BUF_SIZE 10240
static ssize_t twt_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	struct dytwt_manager *dytwt = dytwt_get_manager();
	char *buf;
	int len;
	ssize_t ret;

	buf = vmalloc(TWT_HISTORY_BUF_SIZE);

	if (!buf)
		return 0;

	len = wlan_ptracker_history_read(dytwt->hm, buf, TWT_HISTORY_BUF_SIZE);
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static void update_twt_flag(struct wlan_ptracker_core *core)
{
	struct dytwt_manager *dytwt = dytwt_get_manager();

	if (dytwt->feature_flag & BIT(FEATURE_FLAG_TWT))
		dytwt->feature_flag &= ~BIT(FEATURE_FLAG_TWT);
	else
		dytwt->feature_flag |= BIT(FEATURE_FLAG_TWT);
}

static int dytwt_debugfs_action(struct wlan_ptracker_core *core, u32 action)
{
	struct dytwt_pwr_state pwr_state;
	struct dytwt_manager *dytwt = dytwt_get_manager();
	struct wlan_ptracker_client *client = core->client;

	switch (action) {
	case TWT_TEST_SETUP:
		dytwt_client_twt_setup(client, dytwt->state);
		break;
	case TWT_TEST_TEARDOWN:
		dytwt_client_twt_teardown(client, dytwt->state);
		break;
	case TWT_TEST_CAP:
		dytwt_client_twt_cap(client);
		break;
	case TWT_TEST_PWRSTATS:
		dytwt_client_twt_pwrstates(client, &pwr_state);
		break;
	case TWT_TEST_ONOFF:
		update_twt_flag(core);
		break;
	default:
		ptracker_err(core, "action %d is not supported!\n", action);
		return -ENOTSUPP;
	}
	return 0;
}

static ssize_t twt_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	u32 action;

	if (kstrtouint_from_user(buf, len, 10, &action))
		return -EFAULT;

	return dytwt_debugfs_action(core, action);
}

static const struct file_operations twt_ops = {
	.open = simple_open,
	.read = twt_read,
	.write = twt_write,
	.llseek = generic_file_llseek,
};

/* This function is running in thread context */
#define TWT_WAIT_STA_READY_TIME 1000
static int dytwt_scene_change_handler(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = client->core;
	struct wlan_scene_event *msg = &core->fsm.msg;
	struct dytwt_scene_action *act;
	struct dytwt_manager *dytwt = dytwt_get_manager();
	bool apply = false;
	u32 state = msg->dst;
	int ret = 0;

	if (!(dytwt->feature_flag & BIT(FEATURE_FLAG_TWT)))
		goto out;

	if (!dytwt_client_twt_cap(client)) {
		ptracker_dbg(core, "twt is not supported on device or peer\n");
		goto out;
	}

	act = &dytwt_actions[state];
	ptracker_dbg(core, "twt setup for state: %d, reason: %s!\n",
				  state, reason2str[msg->reason]);

	/* wait for sta ready after connected. */
	if (msg->reason == WLAN_PTRACKER_NOTIFY_STA_CHANGE)
		msleep(TWT_WAIT_STA_READY_TIME);

	/* follow action to setup */
	if (act->action == TWT_ACTION_SETUP) {
		ret = dytwt_client_twt_setup(client, state);
	} else {
		/* tear down was apply during state of perpare change. */
		apply = true;
	}
	apply = ret ? false : true;
out:
	/* store record of hostory even twt is not applid! */
	dytwt_mgmt_history_store(client, dytwt, msg, apply);
	return ret;
}

static void dytwt_scene_change_prepare_handler(struct wlan_ptracker_client *client)
{
	struct dytwt_manager *dytwt = dytwt_get_manager();

	/* prepare to change state teardown original setup first */
	if (dytwt->prev < WLAN_SCENE_LOW_LATENCY)
		dytwt_client_twt_teardown(client, dytwt->prev);
}

static int dytwt_notifier_handler(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct wlan_ptracker_core *core = ptr;
	struct wlan_ptracker_client *client = core->client;

	if (!client)
		return NOTIFY_OK;

	switch (event) {
	case WLAN_PTRACKER_NOTIFY_SCENE_CHANGE:
		dytwt_scene_change_handler(client);
		break;
	case WLAN_PTRACKER_NOTIFY_SCENE_CHANGE_PREPARE:
		dytwt_scene_change_prepare_handler(client);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int dytwt_debugfs_init(struct wlan_ptracker_core *core)
{
	struct wlan_ptracker_debugfs *debugfs = &core->debugfs;
	struct dytwt_manager *dytwt = dytwt_get_manager();

	dytwt->feature_flag |= BIT(FEATURE_FLAG_TWT);
	dytwt->dir = debugfs_create_dir("twt", debugfs->root);
	if (!dytwt->dir)
		return -ENODEV;
	debugfs_create_file("history", 0600, dytwt->dir, core, &twt_ops);
	debugfs_create_u32("state", 0600, dytwt->dir, &dytwt->state);
	return 0;
}

#define DYTWT_RECORD_MAX 50
static int dytwt_mgmt_init(void)
{
	struct dytwt_manager *dytwt = dytwt_get_manager();
	struct history_manager *hm;

	if (dytwt->dir)
		debugfs_remove_recursive(dytwt->dir);
	memset(dytwt, 0, sizeof(*dytwt));
	dytwt->state = WLAN_SCENE_IDLE;
	dytwt->prev = WLAN_SCENE_MAX;
	hm =  wlan_ptracker_history_create(DYTWT_RECORD_MAX, sizeof(struct dytwt_entry));
	if (!hm)
		return -ENOMEM;
	strncpy(hm->name, "Dynamic TWT Setup", sizeof(hm->name));
	hm->priv_read = dytwt_record_priv_read;
	dytwt->hm = hm;
	return 0;
}

static void dytwt_mgmt_exit(void)
{
	struct dytwt_manager *dytwt = dytwt_get_manager();

	if (dytwt->dir)
		debugfs_remove_recursive(dytwt->dir);

	wlan_ptracker_history_destroy(dytwt->hm);
	memset(dytwt, 0, sizeof(*dytwt));
}

static struct notifier_block twt_nb = {
	.priority = 0,
	.notifier_call = dytwt_notifier_handler,
};

int dytwt_init(struct wlan_ptracker_core *core)
{
	dytwt_mgmt_init();
	dytwt_debugfs_init(core);
	return wlan_ptracker_register_notifier(&core->notifier, &twt_nb);
}

void dytwt_exit(struct wlan_ptracker_core *core)
{
	dytwt_mgmt_exit();
	return wlan_ptracker_unregister_notifier(&core->notifier, &twt_nb);
}

