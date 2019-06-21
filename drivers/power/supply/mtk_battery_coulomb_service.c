// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author Wy Chuang<wy.chuang@mediatek.com>
 */

#include <linux/list.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include "mtk_battery.h"
struct mtk_coulomb_service *get_mtk_coulomb_service(void)
{
	struct mtk_battery *gm;

	gm = get_mtk_battery();
	if (gm == NULL) {
		pr_notice("mtk_battery is not ready\n");
		return NULL;
	}

	if (gm->cs.init == false) {
		pr_notice("mtk_coulomb_service is not ready\n");
		return NULL;
	}
	return &gm->cs;
}

void wake_up_gauge_coulomb(void)
{
	unsigned long flags = 0;
	struct mtk_battery *gm;
	struct mtk_coulomb_service *cs;

	gm = get_mtk_battery();
	cs = &gm->cs;

	if (cs == NULL)
		return;

	dev_info(gm->dev, "%s %d %d\n",
		__func__,
		cs->wlock.active,
		cs->coulomb_thread_timeout);

	mutex_lock(&cs->hw_coulomb_lock);
	gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 300);
	gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT, 300);
	mutex_unlock(&cs->hw_coulomb_lock);
	spin_lock_irqsave(&cs->slock, flags);
	if (cs->wlock.active == 0)
		__pm_stay_awake(&cs->wlock);
	spin_unlock_irqrestore(&cs->slock, flags);

	cs->coulomb_thread_timeout = true;
	wake_up(&cs->wait_que);
	dev_info(gm->dev, "%s end\n", __func__);
}

void gauge_coulomb_consumer_init(struct gauge_consumer *coulomb,
	struct device *dev, char *name)
{
	coulomb->name = name;
	INIT_LIST_HEAD(&coulomb->list);
	coulomb->dev = dev;
}

void gauge_coulomb_dump_list(struct mtk_battery *gm)
{
	struct list_head *pos;
	struct list_head *phead;
	struct gauge_consumer *ptr;
	int car;
	struct mtk_coulomb_service *cs;

	cs = &gm->cs;
	dev_info(gm->dev, "%s %d %d\n",
		__func__,
		cs->wlock.active,
		cs->coulomb_thread_timeout);

	phead = &cs->coulomb_head_plus;
	mutex_lock(&cs->coulomb_lock);
	gauge_get_property(GAUGE_PROP_COULOMB, &car);
	if (list_empty(phead) != true) {
		dev_info(gm->dev, "dump plus list start\n");
		list_for_each(pos, phead) {
			ptr = container_of(pos, struct gauge_consumer, list);
			dev_info(gm->dev,
				"+dump list name:%s start:%ld end:%ld car:%d int:%d\n",
				ptr->name,
			ptr->start, ptr->end, car, ptr->variable);
		}
	}

	phead = &cs->coulomb_head_minus;
	if (list_empty(phead) != true) {
		dev_info(gm->dev, "dump minus list start\n");
		list_for_each(pos, phead) {
			ptr = container_of(pos, struct gauge_consumer, list);
			dev_info(gm->dev,
				"-dump list name:%s start:%ld end:%ld car:%d int:%d\n",
				ptr->name,
			ptr->start, ptr->end, car, ptr->variable);
		}
	}
	mutex_unlock(&cs->coulomb_lock);
}

void gauge_coulomb_before_reset(struct mtk_battery *gm)
{
	struct mtk_coulomb_service *cs;
	int val;

	cs = &gm->cs;

	if (cs->init == false) {
		dev_notice(gm->dev, "[%s]gauge_coulomb service is not rdy\n",
			__func__);
		return;
	}
	mutex_lock(&cs->coulomb_lock);
	mutex_lock(&cs->hw_coulomb_lock);
	gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, 0);
	gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT, 0);
	mutex_unlock(&cs->hw_coulomb_lock);
	mutex_unlock(&cs->coulomb_lock);

	gauge_get_property(GAUGE_PROP_COULOMB, &val);
	cs->reset_coulomb = val;
	dev_notice(gm->dev, "%s car=%ld\n",
		__func__,
		cs->reset_coulomb);
	gauge_coulomb_dump_list(gm);
}

void gauge_coulomb_after_reset(struct mtk_battery *gm)
{
	struct list_head *pos;
	struct list_head *phead;
	struct gauge_consumer *ptr;
	unsigned long now;
	unsigned long duraction;
	struct mtk_coulomb_service *cs;

	cs = &gm->cs;
	dev_notice(gm->dev, "%s\n", __func__);
	now = cs->reset_coulomb;
	mutex_lock(&cs->coulomb_lock);

	/* check plus list */
	phead = &cs->coulomb_head_plus;
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct gauge_consumer, list);

		ptr->start = 0;
		duraction = ptr->end - now;
		ptr->end = duraction;
		ptr->variable = duraction;
		dev_info(gm->dev, "[%s]+ %s %ld %ld %d\n",
			__func__,
			ptr->name,
		ptr->start, ptr->end, ptr->variable);
	}

	/* check minus list */
	phead = &cs->coulomb_head_minus;
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct gauge_consumer, list);

		ptr->start = 0;
		duraction = ptr->end - now;
		ptr->end = duraction;
		ptr->variable = duraction;
		dev_info(gm->dev, "[%s]- %s %ld %ld %d\n",
			__func__,
			ptr->name,
		ptr->start, ptr->end, ptr->variable);
	}

	mutex_unlock(&cs->coulomb_lock);
	gauge_coulomb_dump_list(gm);
	wake_up_gauge_coulomb();
}

void gauge_coulomb_start(struct gauge_consumer *coulomb, int car)
{
	struct list_head *pos;
	struct list_head *phead;
	struct gauge_consumer *ptr = NULL;
	int hw_car, now_car;
	bool wake = false;
	int car_now;
	int val;
	struct mtk_coulomb_service *cs;
	struct mtk_battery *gm;

	gm = get_mtk_battery();
	cs = &gm->cs;

	if (car == 0)
		return;

	mutex_lock(&cs->coulomb_lock);
	gauge_get_property(GAUGE_PROP_COULOMB, &val);
	car_now = val;
	/* del from old list */
	if (list_empty(&coulomb->list) != true) {
		dev_info(gm->dev, "coulomb_start del name:%s s:%ld e:%ld v:%d car:%d\n",
		coulomb->name,
		coulomb->start, coulomb->end, coulomb->variable, car_now);
		list_del_init(&coulomb->list);
	}

	coulomb->start = car_now;
	coulomb->end = coulomb->start + car;
	coulomb->variable = car;
	now_car = coulomb->start;

	if (car > 0)
		phead = &cs->coulomb_head_plus;
	else
		phead = &cs->coulomb_head_minus;

	/* add node to list */
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct gauge_consumer, list);
		if (car > 0) {
			if (coulomb->end < ptr->end)
				break;
		} else
			if (coulomb->end > ptr->end)
				break;
	}
	list_add(&coulomb->list, pos->prev);

	if (car > 0) {
		list_for_each(pos, phead) {
			ptr = container_of(pos, struct gauge_consumer, list);
			if (ptr->end - now_car <= 0)
				wake = true;
			else
				break;
		}
		hw_car = ptr->end - now_car;
		mutex_lock(&cs->hw_coulomb_lock);
		gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT, hw_car);
		mutex_unlock(&cs->hw_coulomb_lock);
	} else {
		list_for_each(pos, phead) {
			ptr = container_of(pos, struct gauge_consumer, list);
			if (ptr->end - now_car >= 0)
				wake = true;
			else
				break;
		}
		hw_car = now_car - ptr->end;
		mutex_lock(&cs->hw_coulomb_lock);
		gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT, hw_car);
		mutex_unlock(&cs->hw_coulomb_lock);
	}
	mutex_unlock(&cs->coulomb_lock);

	if (wake == true)
		wake_up_gauge_coulomb();

	dev_info(gm->dev, "coulomb_start dev:%s name:%s s:%ld e:%ld v:%d car:%d w:%d\n",
	dev_name(coulomb->dev), coulomb->name, coulomb->start, coulomb->end,
	coulomb->variable, car, wake);
}

void gauge_coulomb_stop(struct gauge_consumer *coulomb)
{
	struct mtk_coulomb_service *cs;
	struct mtk_battery *gm;

	gm = get_mtk_battery();
	cs = &gm->cs;

	dev_info(gm->dev, "coulomb_stop name:%s %ld %ld %d\n",
	coulomb->name, coulomb->start, coulomb->end,
	coulomb->variable);

	mutex_lock(&cs->coulomb_lock);
	list_del_init(&coulomb->list);
	mutex_unlock(&cs->coulomb_lock);

}

void gauge_coulomb_int_handler(struct mtk_coulomb_service *cs)
{
	int car, hw_car;
	struct list_head *pos;
	struct list_head *phead;
	struct gauge_consumer *ptr = NULL;
	struct mtk_battery *gm;

	gm = get_mtk_battery();
	gauge_get_property(GAUGE_PROP_COULOMB, &car);
	dev_info(gm->dev, "[%s] car:%d preCar:%d\n",
		__func__,
		car, cs->pre_coulomb);

	if (list_empty(&cs->coulomb_head_plus) != true) {
		pos = cs->coulomb_head_plus.next;
		phead = &cs->coulomb_head_plus;
		for (pos = phead->next; pos != phead;) {
			struct list_head *ptmp;

			ptr = container_of(pos, struct gauge_consumer, list);
			if (ptr->end <= car) {
				ptmp = pos;
				pos = pos->next;
				list_del_init(ptmp);
				dev_info(gm->dev,
					"[%s]+ %s s:%ld e:%ld car:%d %d int:%d timeout\n",
					__func__,
					ptr->name,
					ptr->start, ptr->end, car,
					cs->pre_coulomb, ptr->variable);
				if (ptr->callback) {
					mutex_unlock(&cs->coulomb_lock);
					ptr->callback(ptr);
					mutex_lock(&cs->coulomb_lock);
					pos = cs->coulomb_head_plus.next;
				}
			} else
				break;
		}

		if (list_empty(&cs->coulomb_head_plus) != true) {
			pos = cs->coulomb_head_plus.next;
			ptr = container_of(pos, struct gauge_consumer, list);
			hw_car = ptr->end - car;
			dev_info(gm->dev,
				"[%s]+ %s %ld %ld %d now:%d dif:%d\n",
				__func__,
					ptr->name,
					ptr->start, ptr->end,
					ptr->variable, car, hw_car);
			mutex_lock(&cs->hw_coulomb_lock);
			gauge_set_property(GAUGE_PROP_COULOMB_HT_INTERRUPT,
				hw_car);
			mutex_unlock(&cs->hw_coulomb_lock);
		} else
			dev_info(gm->dev, "+ list is empty\n");
	} else
		dev_info(gm->dev, "+ list is empty\n");

	if (list_empty(&cs->coulomb_head_minus) != true) {
		pos = cs->coulomb_head_minus.next;
		phead = &cs->coulomb_head_minus;
		for (pos = phead->next; pos != phead;) {
			struct list_head *ptmp;

			ptr = container_of(pos, struct gauge_consumer, list);
			if (ptr->end >= car) {
				ptmp = pos;
				pos = pos->next;
				list_del_init(ptmp);
				dev_info(gm->dev,
					"[%s]- %s s:%ld e:%ld car:%d %d int:%d timeout\n",
					__func__,
					ptr->name,
					ptr->start, ptr->end,
					car, cs->pre_coulomb, ptr->variable);
				if (ptr->callback) {
					mutex_unlock(&cs->coulomb_lock);
					ptr->callback(ptr);
					mutex_lock(&cs->coulomb_lock);
					pos = cs->coulomb_head_minus.next;
				}

			} else
				break;
		}

		if (list_empty(&cs->coulomb_head_minus) != true) {
			pos = cs->coulomb_head_minus.next;
			ptr = container_of(pos, struct gauge_consumer, list);
			hw_car = car - ptr->end;
			dev_info(gm->dev,
				"[%s]- %s %ld %ld %d now:%d dif:%d\n",
				__func__,
				ptr->name,
				ptr->start, ptr->end,
				ptr->variable, car, hw_car);
			mutex_lock(&cs->hw_coulomb_lock);
			gauge_set_property(GAUGE_PROP_COULOMB_LT_INTERRUPT,
				hw_car);
			mutex_unlock(&cs->hw_coulomb_lock);
		} else
			dev_info(gm->dev, "- list is empty\n");
	} else
		dev_info(gm->dev, "- list is empty\n");

	cs->pre_coulomb = car;
}

static int gauge_coulomb_thread(void *arg)
{
	struct mtk_coulomb_service *cs = (struct mtk_coulomb_service *)arg;
	unsigned long flags = 0;
	struct timespec start, end, duraction;
	struct mtk_battery *gm;

	gm = get_mtk_battery();
	dev_info(gm->dev, "[%s]=>\n", __func__);
	while (1) {
		wait_event(cs->wait_que, (cs->coulomb_thread_timeout == true));
		cs->coulomb_thread_timeout = false;
		get_monotonic_boottime(&start);

		mutex_lock(&cs->coulomb_lock);
		gauge_coulomb_int_handler(cs);
		mutex_unlock(&cs->coulomb_lock);

		spin_lock_irqsave(&cs->slock, flags);
		__pm_relax(&cs->wlock);
		spin_unlock_irqrestore(&cs->slock, flags);

		get_monotonic_boottime(&end);
		duraction = timespec_sub(end, start);

		dev_info(gm->dev,
			"%s time:%d ms\n",
			__func__,
			(int)(duraction.tv_nsec / 1000000));
	}

	return 0;
}

static irqreturn_t coulomb_irq(int irq, void *data)
{
	wake_up_gauge_coulomb();

	return IRQ_HANDLED;
}

void gauge_coulomb_service_init(struct platform_device *pdev)
{
	int val;
	struct mtk_battery *gm;
	struct mtk_coulomb_service *cs;
	int ret;

	gm = dev_get_drvdata(&pdev->dev);
	pr_debug("gauge coulomb_service_init\n");
	cs = &gm->cs;
	INIT_LIST_HEAD(&cs->coulomb_head_minus);
	INIT_LIST_HEAD(&cs->coulomb_head_plus);
	mutex_init(&cs->coulomb_lock);
	mutex_init(&cs->hw_coulomb_lock);
	spin_lock_init(&cs->slock);
	wakeup_source_init(&cs->wlock, "gauge coulomb wakelock");
	init_waitqueue_head(&cs->wait_que);
	kthread_run(gauge_coulomb_thread, cs, "gauge_coulomb_thread");

	ret = devm_request_threaded_irq(&gm->gauge->pdev->dev,
	gm->gauge->coulomb_h_irq,
	NULL, coulomb_irq,
	IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
	"mtk_gauge_coulomb_high",
	gm);

	if (ret)
		pr_notice("failed to request coulomb h irq\n");

	ret = devm_request_threaded_irq(&pdev->dev,
	gm->gauge->coulomb_l_irq,
	NULL, coulomb_irq,
	IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
	"mtk_gauge_coulomb_low",
	gm);

	if (ret)
		pr_notice("failed to request coulomb l irq\n");

	gauge_get_property(GAUGE_PROP_COULOMB, &val);
	cs->pre_coulomb = val;
	cs->init = true;
}
