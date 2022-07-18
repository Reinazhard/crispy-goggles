// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __TP_TRACKER_DYNAMIC_TWT_SETUP_H
#define __TP_TRACKER_DYNAMIC_TWT_SETUP_H

#include "debugfs.h"

struct wlan_ptracker_client;
struct wlan_ptracker_core;

struct dytwt_setup_param {
	u8 config_id;
	u8 nego_type;
	u8 trigger_type;
	u32 wake_duration;
	u32 wake_interval;
};

struct dytwt_cap {
	u16 device_cap;
	u16 peer_cap;
};

struct dytwt_pwr_state {
	u64 awake;
	u64 asleep;
};

struct dytwt_client_ops {
	int (*setup)(void *priv, struct dytwt_setup_param *param);
	int (*teardown)(void *priv, struct dytwt_setup_param *param);
	int (*get_cap)(void *priv, struct dytwt_cap *cap);
	int (*get_pwrstates)(void *priv, struct dytwt_pwr_state *state);
};

enum {
	TWT_ACTION_SETUP,
	TWT_ACTION_TEARDOWN,
	TWT_ACTION_MAX,
};

enum {
	TWT_TEST_SETUP,
	TWT_TEST_TEARDOWN,
	TWT_TEST_CAP,
	TWT_TEST_PWRSTATS,
	TWT_TEST_ONOFF,
	TWT_TEST_MAX,
};

struct dytwt_scene_action {
	u32 action;
	struct dytwt_setup_param param;
};

struct dytwt_entry {
	/* base should put as first membor */
	struct history_entry base;
	bool apply;
	u32 rate;
	u32 reason;
	struct dytwt_pwr_state pwr;
} __align(void *);


struct dytwt_manager {
	u32 prev;
	u32 feature_flag;
	u32 state;
	struct history_manager *hm;
	struct dentry *dir;
};

extern int dytwt_init(struct wlan_ptracker_core *core);
extern void dytwt_exit(struct wlan_ptracker_core *core);

#endif /* __TP_TRACKER_DYNAMIC_TWT_SETUP_H */
