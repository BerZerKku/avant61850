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

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <asm/uaccess.h>

#include "flx_fpts_types.h"
#include "flx_fpts_if.h"
#include "flx_fpts_api.h"
#include "flx_fpts_interrupt.h"
#include "flx_fpts_char.h"

/// Maximum number of events to buffer
#define FLX_FPTS_EVENT_BUF_SIZE 16

/**
 * Enable FPTS to record events and generate interrupts.
 * @param dp Device privates.
 */
static int flx_fpts_enable_device(struct flx_fpts_dev_priv *dp)
{
    int ret = -ENODEV;

    dev_dbg(&dp->pdev->dev, "%s() Start recording events\n", __func__);

    dp->buf = kzalloc(FLX_FPTS_EVENT_BUF_SIZE*sizeof(*dp->buf),
                      GFP_KERNEL);
    if (!dp->buf) {
        return -ENOMEM;
    }
    dp->buf_count = 0;
    dp->read_count = 0;
    dp->buf_size = FLX_FPTS_EVENT_BUF_SIZE;

    ret = flx_fpts_init_interrupt(dp);
    if (ret)
        goto err_interrupt;

    ret = flx_fpts_write_reg(dp, FPTS_REG_TS_CTRL, FPTS_TS_CTRL_GET_TS);
    if (ret < 0)
        goto err_enable;

    return ret;

err_enable:
    flx_fpts_cleanup_interrupt(dp);
    flx_fpts_write_reg(dp, FPTS_REG_TS_CTRL, 0);

err_interrupt:
    dp->buf_size = 0;
    kfree(dp->buf);
    dp->buf = NULL;
    return ret;
}

/**
 * Stop FPTS from recording events and generating interrupts.
 * @param dp Device privates.
 */
static void flx_fpts_disable_device(struct flx_fpts_dev_priv *dp)
{
    dev_dbg(&dp->pdev->dev, "%s() Stop recording events\n", __func__);

    flx_fpts_cleanup_interrupt(dp);

    flx_fpts_write_reg(dp, FPTS_REG_TS_CTRL, 0);

    dp->buf_count = 0;
    dp->read_count = 0;
    dp->buf_size = 0;

    kfree(dp->buf);
    dp->buf = NULL;

    return;
}

/**
 * Character device open handler.
 * Interrupts are enabled if not already done.
 */
static int flx_fpts_open(struct inode *inode, struct file *filp)
{
    unsigned int minor = iminor(inode);
    struct flx_fpts_drv_priv *drv =
        container_of(inode->i_cdev, struct flx_fpts_drv_priv, cdev);
    struct flx_fpts_dev_priv *tmp = NULL;
    struct flx_fpts_dev_priv *dp = NULL;
    unsigned int use_count;
    int ret = -ENODEV;

    // Check that device exists.
    if (minor >= FLX_FPTS_MAX_DEVICES || !test_bit(minor, drv->used_devices))
        return -ENODEV;

    list_for_each_entry(tmp, &drv->devices, list) {
        if (tmp->dev_num == minor) {
            dp = tmp;
            break;
        }
    }

    if (!dp)
        return -ENXIO;

    mutex_lock(&dp->read_lock);

    spin_lock(&dp->buf_lock);
    use_count = dp->use_count++;
    spin_unlock(&dp->buf_lock);

    if (use_count == 0) {
        // Always start with default settings.
        if (dp->irq)
            dp->mode = FLX_FPTS_MODE_INTERRUPT;
        else
            dp->mode = FLX_FPTS_MODE_POLL;
        dp->poll_interval = HZ/2;

        ret = flx_fpts_enable_device(dp);
        if (ret) {
            spin_lock(&dp->buf_lock);
            dp->use_count--;
            spin_unlock(&dp->buf_lock);
            goto out;
        }
    }

    filp->private_data = dp;

    ret = 0;

out:
    mutex_unlock(&dp->read_lock);
    return ret;
}

/**
 * Helper function to detect if new event data is available.
 */
static inline bool flx_fpts_is_readable(struct flx_fpts_dev_priv *dp)
{
    bool ret;

    ret = dp->buf_count > dp->read_count || dp->mode == FLX_FPTS_MODE_DIRECT;

    return ret;
}

/**
 * Character device poll handler for select/poll system calls.
 */
static unsigned int flx_fpts_poll(struct file *filp, poll_table *wait)
{
    struct flx_fpts_dev_priv *dp = filp->private_data;
    unsigned int mask = 0;

    if (!dp)
        return -EBADF;

    dev_dbg(&dp->pdev->dev, "%s() Wait event\n", __func__);

    poll_wait(filp, &dp->read_waitq, wait);
    if (flx_fpts_is_readable(dp)) {
        mask |= POLLIN | POLLRDNORM;
    }

    dev_dbg(&dp->pdev->dev, "%s() Exit events %u read %u\n",
            __func__, dp->buf_count, dp->read_count);

    return mask;
}

/**
 * Character device read handler.
 * Gets new event if available and deliveres it to user space.
 * Implements both non-blocking and blocking reads.
 */
static ssize_t flx_fpts_read(struct file *filp,
                             char __user *buf,
                             size_t count, loff_t *f_pos)
{
    struct flx_fpts_dev_priv *dp = filp->private_data;
    unsigned int num_events = 0;
    unsigned int max_events = 0;
    struct flx_fpts_event *event = NULL;
    size_t data_amount = 0;
#ifndef CONFIG_FLX_BUS
    unsigned long int flags = 0;
#endif
    int ret = 0;

    if (!dp)
        return -EBADF;

    max_events = count / sizeof(*event);
    if (max_events == 0)
        return -EINVAL;

    dev_dbg(&dp->pdev->dev, "%s() Read up to %u events\n",
            __func__, max_events);

    ret = mutex_lock_interruptible(&dp->read_lock);
    if (ret == -EINTR)
        return -ERESTARTSYS;

again:
    dev_dbg(&dp->pdev->dev, "%s() Enter loop events %u read %u\n",
            __func__, dp->buf_count, dp->read_count);
    while (!flx_fpts_is_readable(dp)) {
        mutex_unlock(&dp->read_lock);

        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;

        if (dp->mode == FLX_FPTS_MODE_DIRECT) {
            queue_delayed_work(dp->drv->wq, &dp->poll_work, 0);
            return -EAGAIN;
        }

        dev_dbg(&dp->pdev->dev, "%s() Wait new events %u read %u\n",
                __func__, dp->buf_count, dp->read_count);

        ret = wait_event_interruptible(dp->read_waitq,
                                       flx_fpts_is_readable(dp));
        if (ret == -ERESTARTSYS)
            return -ERESTARTSYS;

        ret = mutex_lock_interruptible(&dp->read_lock);
        if (ret == -EINTR)
            return -ERESTARTSYS;
    }

    // Still locked.

    dev_dbg(&dp->pdev->dev, "%s() Got something events %u read %u\n",
            __func__, dp->buf_count, dp->read_count);

    flx_fpts_spin_lock(&dp->buf_lock, flags);

    event = &dp->buf[dp->read_count];
    while (dp->read_count + num_events < dp->buf_count &&
           num_events < max_events) {
        num_events++;
    }

    flx_fpts_spin_unlock(&dp->buf_lock, flags);

    dev_dbg(&dp->pdev->dev, "%s() Preparing %u at %p events %u read %u\n",
            __func__, num_events, event, dp->buf_count, dp->read_count);

    if (num_events == 0) {
        if (dp->mode == FLX_FPTS_MODE_DIRECT) {
            mutex_unlock(&dp->read_lock);
            return -EAGAIN;
        }
        dev_info(&dp->pdev->dev, "%s() False alarm, no event\n", __func__);
        goto again;
    }

    data_amount = num_events * sizeof(*event);
    if (copy_to_user(buf, event, data_amount)) {
        mutex_unlock(&dp->read_lock);
        return -EFAULT;
    }

    flx_fpts_spin_lock(&dp->buf_lock, flags);
    dp->read_count += num_events;
    flx_fpts_spin_unlock(&dp->buf_lock, flags);

    mutex_unlock(&dp->read_lock);

    dev_dbg(&dp->pdev->dev, "%s() Delivering %u events %u read %u\n",
            __func__, num_events, dp->buf_count, dp->read_count);

    *f_pos += data_amount;
    return data_amount;
}

/**
 * Helper function to change operational device settings.
 * @param dp Device privates.
 * @param settings New settings for operation.
 * @return Suitable return value for user space.
 */
static int flx_fpts_change_settings(struct flx_fpts_dev_priv *dp,
                                    const struct flx_fpts_settings *settings)
{
    int ret = -EINVAL;

    if (settings->mode < FLX_FPTS_MODE_INTERRUPT ||
        settings->mode > FLX_FPTS_MODE_DIRECT)
        return -EINVAL;

    if (settings->mode == FLX_FPTS_MODE_INTERRUPT && !dp->irq)
        return -EINVAL;

    ret = mutex_lock_interruptible(&dp->read_lock);
    if (ret == -EINTR)
        return -ERESTARTSYS;

    flx_fpts_disable_device(dp);

    dp->mode = settings->mode;
    dp->poll_interval = timespec_to_jiffies(&settings->poll_interval);

    dev_dbg(&dp->pdev->dev, "%s() New mode %u poll interval %lu\n",
            __func__, dp->mode, dp->poll_interval);

    ret = flx_fpts_enable_device(dp);
    if (ret)
        goto out;

    // Ensure that existing readers are not stuck.
    if (settings->mode != FLX_FPTS_MODE_INTERRUPT)
        wake_up_interruptible(&dp->read_waitq);

    ret = 0;

out:
    mutex_unlock(&dp->read_lock);
    return ret;
}

/**
 * Handle ioctl requests.
 * @param filp File pointer.
 * @param cmd Ioctl command.
 * @param arg Ioctl argument.
 */
static long int flx_fpts_ioctl(struct file *filp, unsigned int cmd,
                               unsigned long int arg)
{
    struct flx_fpts_dev_priv *dp = filp->private_data;
    const void __user *data = (const void __user *)arg;
    int ret = -EACCES;

    if (!dp)
        return -EBADF;

    if (_IOC_DIR(cmd) & _IOC_READ)
        if (!access_ok(VERIFY_WRITE, data, _IOC_SIZE(cmd)))
            return -EACCES;

    if (_IOC_DIR(cmd) & _IOC_WRITE)
        if (!access_ok(VERIFY_READ, data, _IOC_SIZE(cmd)))
            return -EACCES;

    switch (cmd) {
    case FLX_FPTS_IOCTL_SET_SETTINGS:
        {
            struct flx_fpts_settings settings = {
                .mode = FLX_FPTS_MODE_INTERRUPT,
            };

            if (copy_from_user(&settings, data, sizeof(settings)))
                return -EFAULT;

            ret = flx_fpts_change_settings(dp, &settings);
        }
        break;

    default:
        return -ENOTTY;
    }

    return ret;
}

/**
 * Character device close handler.
 * Interrupts are disabled when device is no longer open by any one.
 * This avoids unnecessary processing when interrupt is shared.
 */
static int flx_fpts_release(struct inode *inode, struct file *filp)
{
    struct flx_fpts_dev_priv *dp = filp->private_data;
    unsigned int use_count;

    if (!dp)
        return -EBADF;

    mutex_lock(&dp->read_lock);

    spin_lock(&dp->buf_lock);
    use_count = --dp->use_count;
    spin_unlock(&dp->buf_lock);

    if (use_count == 0) {
        flx_fpts_disable_device(dp);
    }

    mutex_unlock(&dp->read_lock);

    filp->private_data = NULL;

    return 0;
}

/**
 * Character device file operations.
 */
static const struct file_operations flx_fpts_fops = {
    .owner = THIS_MODULE,
    .poll = &flx_fpts_poll,
    .read = &flx_fpts_read,
    .unlocked_ioctl = &flx_fpts_ioctl,
    .open = &flx_fpts_open,
    .release = &flx_fpts_release,
};

/**
 * Register new character device.
 * Allocates a character device major/minor region dynamically.
 * @param drv Driver privates.
 */
int flx_fpts_register_char_device(struct flx_fpts_drv_priv *drv)
{
    int ret;

    ret = class_register(&drv->class);
    if (ret) {
        pr_err(DRV_NAME ": Failed to register class\n");
        return ret;
    }

    ret = alloc_chrdev_region(&drv->first_devno, 0, FLX_FPTS_MAX_DEVICES,
                              DRV_NAME);
    if (ret) {
        pr_err(DRV_NAME ": Failed to allocate char device numbers\n");
        goto err_alloc;
    }

    cdev_init(&drv->cdev, &flx_fpts_fops);
    drv->cdev.owner = THIS_MODULE;

    ret = cdev_add(&drv->cdev, drv->first_devno, FLX_FPTS_MAX_DEVICES);
    if (ret) {
        pr_err(DRV_NAME ": Failed to register char device\n");
        goto err_add;
    }

    return 0;

err_add:
    unregister_chrdev_region(drv->first_devno, FLX_FPTS_MAX_DEVICES);

err_alloc:
    class_unregister(&drv->class);

    return ret;
}

/**
 * Unregister character device and release major/minor numbers.
 * @param drv Driver privates.
 */
void flx_fpts_unregister_char_device(struct flx_fpts_drv_priv *drv)
{
    cdev_del(&drv->cdev);
    unregister_chrdev_region(drv->first_devno, FLX_FPTS_MAX_DEVICES);
    class_unregister(&drv->class);

    return;
}
