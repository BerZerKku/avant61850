/** @file
 */

/*

   Flexibilis Real-Time Clock Linux driver

   Copyright (C) 2008 Flexibilis Oy

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

#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "flx_time_nco_types.h"

/* read_nco_time() */
#include "flx_time_nco.h"

int flx_time_print_nco_status(struct seq_file *m,
                              struct flx_time_comp_priv_common *cpc)
{
    struct flx_time_comp_priv *cp = (struct flx_time_comp_priv *) cpc;
    struct flx_time_get_data time;

    seq_printf(m, "\n");
    seq_printf(m, "Component index: %i\n", cp->common.prop.index);
    seq_printf(m, " name          : %s\n", cp->common.prop.name);
    if (cp->id >= 0xff00) {
        seq_printf(m, " device id     : 0x%04x\n", cp->id);
        seq_printf(m, " revision id   :    N/A\n");
    } else {
        seq_printf(m, " device id     : 0x%04x\n",
                   (flx_nco_read32(cp,
                                   GENERAL_REG) >> DEVID_SHIFT) & DEVID_MASK);
        seq_printf(m, " revision id   :   0x%02x\n",
                   (flx_nco_read32(cp,
                                   GENERAL_REG) >> REVID_SHIFT) & REVID_MASK);
    }
    seq_printf(m, " properties    :   0x%02x\n",
               cp->common.prop.properties);
    seq_printf(m, "\n");

    read_nco_time(cp, &time);

    seq_printf(m, " Time read:\n");

    seq_printf(m, "  seconds      : %lld\n", time.timestamp.sec);

    seq_printf(m, "  nanoseconds  : %d\n", time.timestamp.nsec);
    seq_printf(m, "  subnsecs     : 0x%04x\n", time.timestamp.subnsec);
    seq_printf(m, "  clk cycle cnt: 0x%016llx\n", time.counter);
    seq_printf(m, "\n");

    seq_printf(m, " Register content:\n");
    seq_printf(m, "  nco subnsec reg      :     0x%08x\n",
               flx_nco_read32(cp, NCO_SUBNSEC_REG));
    seq_printf(m, "  nco nsec reg         :     0x%08x\n",
               flx_nco_read32(cp, NCO_NSEC_REG));
    seq_printf(m, "  nco sec reg          : 0x%04x%08x\n",
               flx_nco_read32(cp, NCO_SEC_HI_REG),
               flx_nco_read32(cp, NCO_SEC_REG));
    seq_printf(m, "  nco cccnt reg        : 0x%04x%08x\n",
               flx_nco_read32(cp, NCO_CCCNT_HI_REG),
               flx_nco_read32(cp, NCO_CCCNT_REG));
    seq_printf(m, "  nco step subnsec reg :     0x%08x\n",
               flx_nco_read32(cp, NCO_STEP_SUBNSEC_REG));
    seq_printf(m, "  nco step nsec reg    :           0x%02x\n",
               flx_nco_read32(cp, NCO_STEP_NSEC_REG));

    seq_printf(m, "  nco adj nsec reg     :     0x%08x\n",
               flx_nco_read32(cp, NCO_ADJ_NSEC_REG));
    seq_printf(m, "  nco adj sec reg      : 0x%04x%08x\n",
               flx_nco_read32(cp, NCO_ADJ_SEC_HI_REG),
               flx_nco_read32(cp, NCO_ADJ_SEC_REG));

    seq_printf(m, "  nco cmd reg          :           0x%02x\n",
               flx_nco_read32(cp, NCO_CMD_REG));


    seq_printf(m, "\n");
    return 0;
}
