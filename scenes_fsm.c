// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include "core.h"

#define fsm_to_core(fsm) \
	(container_of(fsm, struct wlan_ptracker_core, fsm))

static const struct wlan_state_condition conditions[FSM_STATE_MAX] = {
	{
		.scene = WLAN_SCENE_IDLE,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 0,
		.max_tp_threshold = 1000,
	},
	{
		.scene = WLAN_SCENE_WEB,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 1000,
		.max_tp_threshold = 10000,
	},
	{
		.scene = WLAN_SCENE_YOUTUBE,
		.ac_mask = WMM_AC_ALL_MASK,
		/* Total >= 10 Mbps && < 50 Mbps */
		.min_tp_threshold = 10000,
		.max_tp_threshold = 50000,
	},
	{
		.scene = WLAN_SCENE_LOW_LATENCY,
		.ac_mask = BIT(WMM_AC_VO),
		/*  VO >= 1 Mbps */
		.min_tp_threshold = 1000,
		.max_tp_threshold = __INT_MAX__,
	},
	{
		.scene = WLAN_SCENE_TPUT,
		.ac_mask = WMM_AC_ALL_MASK,
		/* Total >= 50 Mbps */
		.min_tp_threshold = 50000,
		.max_tp_threshold = __INT_MAX__,
	},
};

static int fsm_thread(void *param)
{
	struct wlan_ptracker_fsm *fsm = param;
	struct wlan_scene_event *msg = &fsm->msg;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			ptracker_info(core, "kthread is stopped\n");
			break;
		}
		wait_for_completion(&fsm->event);
		ptracker_dbg(core, "state: %d, trans state %d -> %d, rate %llu\n",
					  msg->state, msg->src, msg->dst, msg->rate);

		/*
		 * Request twice of transmit events are happing then trans state,
		 * to make sure the state is stable enough.
		 * first time: confirm is false, send prepare first.
		 * (ex: twt can tear down original setup first)
		 * second time: confirm is true and change the state to dst.
		 */
		if (fsm->confirm) {
			wlan_ptracker_call_chain(&core->notifier,
			WLAN_PTRACKER_NOTIFY_SCENE_CHANGE, core);
			msg->state = msg->dst;
			fsm->confirm = false;
		} else {
			/* call notifier chain */
			wlan_ptracker_call_chain(&core->notifier,
			WLAN_PTRACKER_NOTIFY_SCENE_CHANGE_PREPARE, core);
			fsm->confirm = true;
		}
	}
	return 0;
}

static bool scenes_check(u64 rate, const struct wlan_state_condition *cond,
	struct wlan_scene_event *msg)
{
	/* change bits rate to Kbits rate */
	u64 krate = rate / 1000;

	if (krate >= cond->min_tp_threshold && krate < cond->max_tp_threshold) {
		msg->rate = rate;
		return true;
	}
	return false;
}

static u32 scenes_condition_get(struct wlan_ptracker_fsm *fsm)
{
	const struct wlan_state_condition *cond;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);
	struct tp_monitor_stats *stats = &core->tp;
	struct wlan_scene_event *msg = &fsm->msg;
	int i, j;

	/* check from higher restriction to lower */
	for (i = FSM_STATE_MAX - 1 ; i >= 0 ; i--) {
		cond = &fsm->conditions[i];
		if (cond->ac_mask == WMM_AC_ALL_MASK) {
			if (scenes_check(
				stats->tx[WMM_AC_MAX].rate + stats->rx[WMM_AC_MAX].rate,
				cond, msg))
				return cond->scene;
		} else {
			u64 total_tx = 0;
			u64 total_rx = 0;

			for (j = 0 ; j < WMM_AC_MAX; j++) {
				if (cond->ac_mask & BIT(j)) {
					total_tx += stats->tx[j].rate;
					total_rx += stats->rx[j].rate;
				}
				if (scenes_check(total_tx + total_rx, cond, msg))
					return cond->scene;
			}
		}
	}
	return fsm->msg.state;
}

/* TODO: fine-tune period threshold */
#define RESET_THRESHOLD 1
static void scenes_fsm_decision(struct wlan_ptracker_core *core, u32 type)
{
	struct wlan_ptracker_fsm *fsm = &core->fsm;
	struct wlan_scene_event *msg = &fsm->msg;
	u32 new_state;
	bool except = false;

	if (!fsm->fsm_thread)
		return;

	/* condition check */
	new_state = scenes_condition_get(fsm);

	/* reset check */
	if (type == WLAN_PTRACKER_NOTIFY_SUSPEND) {
		fsm->reset_cnt++;
		except = !(fsm->reset_cnt % RESET_THRESHOLD);
	}

	/* check state isn't change and not first time do nothing */
	if (new_state == msg->state && type != WLAN_PTRACKER_NOTIFY_STA_CHANGE)
		return;
	/* new state must higher then current state */
	if (new_state < msg->state && !except)
		return;

	ptracker_dbg(core, "type %d, reset_cnt %d, %d -> %d\n",
				  type, fsm->reset_cnt, msg->state, new_state);

	/* clear reset cnt*/
	fsm->reset_cnt = 0;
	/* decide to trans state */
	mutex_lock(&msg->lock);
	msg->src = msg->state;
	msg->dst = new_state;
	msg->reason = type;
	mutex_unlock(&msg->lock);

	/* send complete to wake up thread to handle fsm */
	complete(&fsm->event);
}

static int scene_notifier_handler(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct wlan_ptracker_core *core = ptr;
	struct wlan_ptracker_notifier *notifier = &core->notifier;

	/*
	 * Events of suspen and sta change will block wlan driver
	 * should not spend too much time. Move complex part to thread handle.
	 */
	switch (event) {
	case WLAN_PTRACKER_NOTIFY_SUSPEND:
		ptracker_dbg(core, "update time (%d)\n",
			jiffies_to_msecs(jiffies - notifier->prev_event));
		notifier->prev_event = jiffies;
	case WLAN_PTRACKER_NOTIFY_STA_CHANGE:
	case WLAN_PTRACKER_NOTIFY_TP:
		scenes_fsm_decision(core, event);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block scene_nb = {
	.priority = 0,
	.notifier_call = scene_notifier_handler,
};

int scenes_fsm_init(struct wlan_ptracker_fsm *fsm)
{
	struct wlan_scene_event *msg = &fsm->msg;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);
	int ret = 0;

	/* assign scenes and conditions */
	fsm->conditions = &conditions[0];
	fsm->reset_cnt = 0;
	/* for first link up setting */
	fsm->confirm = true;
	/* init msg for receiving event */
	msg->dst = WLAN_SCENE_IDLE;
	msg->src = WLAN_SCENE_IDLE;
	msg->state = WLAN_SCENE_IDLE;
	mutex_init(&msg->lock);

	/*scene event notifier handler from client */
	ret = wlan_ptracker_register_notifier(&core->notifier, &scene_nb);
	if (ret)
		return ret;

	/* initial thread for listening event */
	init_completion(&fsm->event);
	fsm->fsm_thread = kthread_create(fsm_thread, fsm, "wlan_ptracker_thread");
	if (IS_ERR(fsm->fsm_thread)) {
		ret = PTR_ERR(fsm->fsm_thread);
		fsm->fsm_thread = NULL;
		ptracker_err(core, "unable to start kernel thread %d\n", ret);
		return ret;
	}
	wake_up_process(fsm->fsm_thread);
	return 0;
}

void scenes_fsm_exit(struct wlan_ptracker_fsm *fsm)
{
	struct wlan_ptracker_core *core = fsm_to_core(fsm);

	wlan_ptracker_unregister_notifier(&core->notifier, &scene_nb);
	if (fsm->fsm_thread) {
		int ret = kthread_stop(fsm->fsm_thread);
		fsm->fsm_thread = NULL;
		if (ret)
			ptracker_err(core, "stop thread fail: %d\n", ret);
	}
	complete(&fsm->event);
	fsm->conditions = NULL;
	fsm->reset_cnt = 0;
}

