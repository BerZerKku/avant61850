/** @file
 */

/*

   Flexibilis PPx Time Stamper Linux driver

   Copyright (C) 2015 Flexibilis Oy

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License version 2
   as published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/// Uncomment to enable debug messages
//#define DEBUG

#include <linux/interrupt.h>
#include <linux/sched.h>

#include "flx_fpts_types.h"
#include "flx_fpts_if.h"
#include "flx_fpts_api.h"
#include "flx_fpts_interrupt.h"

/**
 * Read event from FPTS to driver's representation.
 * @param dp Device privates.
 * @param event Place for event data.
 */
static int flx_fpts_get_event(struct flx_fpts_dev_priv *dp,
                              struct flx_fpts_event *event)
{
    int ret;

    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC0);
    if (ret < 0)
        return ret;
    event->sec = ret;
    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC1);
    if (ret < 0)
        return ret;
    event->sec |= (uint64_t)ret << 16;
    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_SEC2);
    if (ret < 0)
        return ret;
    event->sec |= (uint64_t)ret << 32;
    event->sec &= FPTS_TS_SEC_MASK;

    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_NSEC0);
    if (ret < 0)
        return ret;
    event->nsec = ret;
    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_NSEC1);
    if (ret < 0)
        return ret;
    event->nsec |= (uint32_t)ret << 16;
    event->nsec &= FPTS_TS_NSEC_MASK;

    // Normalize nanoseconds.
    while (event->nsec >= 1000000000u) {
        event->nsec -= 1000000000u;
        event->sec++;
    }

    ret = flx_fpts_read_reg(dp, FPTS_REG_PCNT0);
    if (ret < 0)
        return ret;
    event->counter = ret;
    ret = flx_fpts_read_reg(dp, FPTS_REG_PCNT1);
    if (ret < 0)
        return ret;
    event->counter |= (uint32_t)ret << 16;

    dp->last_event = *event;

    return 0;
}

/**
 * Read new event from FPTS registers if event is available.
 * Actual work to do from interrupt or interrupt work handler.
 * This can be called from HW interrupt handler when memory-mapped
 * I/O is in use, or from interrupt work handler.
 * @param dp Device privates.
 */
static void flx_fpts_check_event(struct flx_fpts_dev_priv *dp)
{
    struct flx_fpts_event *event = NULL;
#ifndef CONFIG_FLX_BUS
    unsigned long int flags = 0;
#endif
    int ret;

    dev_dbg(&dp->pdev->dev, "%s() New work buffers %u ready %u read %u\n",
            __func__, dp->buf_size, dp->buf_count, dp->read_count);

    // Ensure there is room for new event and get pointer to it.
    flx_fpts_spin_lock(&dp->buf_lock, flags);

    if (dp->buf_count > 0 && dp->read_count == dp->buf_count) {
        dp->buf_count = 0;
        dp->read_count = 0;
    }

    if (dp->buf_count < dp->buf_size)
        event = &dp->buf[dp->buf_count];

    flx_fpts_spin_unlock(&dp->buf_lock, flags);

    if (!event) {
        dev_dbg(&dp->pdev->dev, "%s() No free buffers\n", __func__);
        return;
    }

    // Must verify that we really have a new event.
    ret = flx_fpts_read_reg(dp, FPTS_REG_TS_CTRL);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "%s() Control reg read error\n", __func__);
        return;
    }
    if (ret & FPTS_TS_CTRL_GET_TS) {
        dev_dbg(&dp->pdev->dev, "%s() No new events\n", __func__);
        return;
    }

    ret = flx_fpts_read_reg(dp, FPTS_REG_INT_STAT);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "%s() Interrupt status reg read error\n",
                 __func__);
        return;
    }
    if (!(ret & FPTS_INT_TS)) {
        dev_dbg(&dp->pdev->dev, "%s() No new events\n", __func__);
        return;
    }

    dev_dbg(&dp->pdev->dev, "%s() Get new event %u at %p\n",
            __func__, dp->buf_count, event);

    ret = flx_fpts_get_event(dp, event);
    if (ret) {
        dev_dbg(&dp->pdev->dev, "%s() Failed to read event\n", __func__);
        event = NULL;
    }

    // Acknowledge interrupt.
    ret = flx_fpts_write_reg(dp, FPTS_REG_INT_STAT, 0);
    if (ret < 0) {
        dev_dbg(&dp->pdev->dev, "%s() Interrupt status reg write error\n",
                __func__);
    }

    // Order next event.
    ret = flx_fpts_write_reg(dp, FPTS_REG_TS_CTRL, FPTS_TS_CTRL_GET_TS);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "%s() Control reg write error\n", __func__);
    }
    if (!event) {
        // No event data available or error, ignore this event.
        return;
    }

    dev_dbg(&dp->pdev->dev, "%s() Event %u %llu s %u ns count %u\n",
            __func__, dp->buf_count,
            event->sec, event->nsec, event->counter);

    // We have a new event. Tell readers.
    flx_fpts_spin_lock(&dp->buf_lock, flags);
    dp->buf_count++;
    flx_fpts_spin_unlock(&dp->buf_lock, flags);

    dev_dbg(&dp->pdev->dev, "%s() Wakeup reader\n", __func__);

    wake_up_interruptible(&dp->read_waitq);

    return;
}

#ifdef CONFIG_FLX_BUS
/**
 * Interrupt work handler.
 * This is used when registers cannot be access directly from interrupt
 * handler.
 * @param work Interrupt work structure within device privates.
 */
static void flx_fpts_interrupt_work(struct work_struct *work)
{
    struct flx_fpts_dev_priv *dp =
        container_of(work, struct flx_fpts_dev_priv, irq_work);

    // Disable interrupt from FPTS until we have handled it.
    flx_fpts_write_reg(dp, FPTS_REG_INT_MASK, 0);

    // Let others use the interrupt line.
    atomic_dec(&dp->irq_disable);
    enable_irq(dp->irq);

    dp->irq_work_count++;

    flx_fpts_check_event(dp);

    // Reenable interrupt from FPTS.
    flx_fpts_write_reg(dp, FPTS_REG_INT_MASK, FPTS_INT_TS);

    return;
}

/**
 * HW interrupt handler for indirect register access.
 * A work is kicked work to handle FPTS interrupts, because
 * registers cannot be accessed from interrupt context.
 * @param irq Interrupt number.
 * @param arg Device privates.
 */
static irqreturn_t flx_fpts_interrupt_indirect(int irq, void *arg)
{
    struct flx_fpts_dev_priv *dp = arg;

    dp->irq_count++;

    // Disable interrupt and kick work to handle interrupt
    // and reenable interrupt again.
    disable_irq_nosync(dp->irq);
    atomic_inc(&dp->irq_disable);

    queue_work(dp->drv->wq, &dp->irq_work);

    return IRQ_HANDLED;
}
#endif

/**
 * Polling mode event read work handler.
 * This is used to read events when interrupts are not used.
 * This is also used in direct mode, in which case work is not periodic.
 * @param work Read work structure within device privates.
 */
static void flx_fpts_poll_work(struct work_struct *work)
{
    struct flx_fpts_dev_priv *dp =
        container_of(work, struct flx_fpts_dev_priv, poll_work.work);

    dp->poll_work_count++;

    flx_fpts_check_event(dp);

    if (dp->mode == FLX_FPTS_MODE_POLL)
        queue_delayed_work(dp->drv->wq, &dp->poll_work, dp->poll_interval);

    return;
}

/**
 * HW interrupt handler.
 * This is used to handle FPTS interrupts when registers can be accessed
 * directly from interrupt handler.
 * @param irq Interrupt number.
 * @param arg Device privates.
 */
static irqreturn_t flx_fpts_interrupt(int irq, void *arg)
{
    struct flx_fpts_dev_priv *dp = arg;

    dp->irq_count++;

    flx_fpts_check_event(dp);

    return IRQ_HANDLED;
}

/**
 * Initialize interrupt handling.
 * Registers interrupt handler and enables FPTS interrupts.
 * @param dp Device privates.
 */
int flx_fpts_init_interrupt(struct flx_fpts_dev_priv *dp)
{
    bool indirect = false;
    int ret;

    // Acknowledge old interrupt.
    ret = flx_fpts_write_reg(dp, FPTS_REG_INT_STAT, 0);
    if (ret < 0) {
        dev_warn(&dp->pdev->dev, "%s() Interrupt status reg write error\n",
                 __func__);
        goto err_ack_int;
    }

    INIT_DELAYED_WORK(&dp->poll_work, &flx_fpts_poll_work);

    if (dp->mode == FLX_FPTS_MODE_POLL) {
        queue_delayed_work(dp->drv->wq, &dp->poll_work, dp->poll_interval);
        return 0;
    }

    if (dp->mode == FLX_FPTS_MODE_INTERRUPT) {
#ifdef CONFIG_FLX_BUS
        if (dp->regs.flx_bus) {
            INIT_WORK(&dp->irq_work, &flx_fpts_interrupt_work);
            atomic_set(&dp->irq_disable, 0);
            ret = request_irq(dp->irq, &flx_fpts_interrupt_indirect,
                              IRQF_SHARED, DRV_NAME, dp);
            indirect = true;
        }
#endif
        if (!indirect) {
            ret = request_irq(dp->irq, &flx_fpts_interrupt,
                              IRQF_SHARED, DRV_NAME, dp);
        }
        if (ret) {
            dev_warn(&dp->pdev->dev, "%s() Failed to register interrupt %u\n",
                     __func__, dp->irq);
            return ret;
        }

        // Enable interrupts.
        ret = flx_fpts_write_reg(dp, FPTS_REG_INT_MASK, FPTS_INT_TS);
        if (ret < 0) {
            dev_warn(&dp->pdev->dev, "%s() Interrupt mask reg write error\n",
                     __func__);
            goto err_enable_int;
        }
    }

    return ret;

err_enable_int:
err_ack_int:
    free_irq(dp->irq, dp);

#ifdef CONFIG_FLX_BUS
    cancel_work_sync(&dp->irq_work);
    flush_workqueue(dp->drv->wq);

    // Ensure disable_irq() will be left balanced.
    while (atomic_dec_return(&dp->irq_disable) >= 0)
        enable_irq(dp->irq);
#endif

    return ret;
}

/**
 * Cleanup interrupt handling.
 * Disables FPTS interrupts and frees interrupt handler.
 * @param dp Device privates.
 */
void flx_fpts_cleanup_interrupt(struct flx_fpts_dev_priv *dp)
{
    cancel_delayed_work_sync(&dp->poll_work);
    flush_workqueue(dp->drv->wq);

    if (dp->mode == FLX_FPTS_MODE_INTERRUPT) {
        // No more interrupts.
        flx_fpts_write_reg(dp, FPTS_REG_INT_MASK, 0);

        free_irq(dp->irq, dp);

#ifdef CONFIG_FLX_BUS
        cancel_work_sync(&dp->irq_work);
        flush_workqueue(dp->drv->wq);

        // Ensure disable_irq() will be left balanced.
        while (atomic_dec_return(&dp->irq_disable) >= 0)
            enable_irq(dp->irq);
#endif

        // Acknowledge possible leftover interrupt.
        flx_fpts_write_reg(dp, FPTS_REG_INT_MASK, 0);
        flx_fpts_write_reg(dp, FPTS_REG_INT_STAT, 0);
    }

    return;
}

