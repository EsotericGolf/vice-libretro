/*
 * drive-snapshot.c - Hardware-level disk drive emulation, snapshot module.
 *
 * Written by
 *  Andreas Boose <viceteam@t-online.de>
 *
 * This file is part of VICE, the Versatile Commodore Emulator.
 * See README for copyright notice.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA.
 *
 */

#include "vice.h"

#include <stdio.h>
#include <stdlib.h>

#include "archdep.h"
#include "attach.h"
#include "diskconstants.h"
#include "diskimage.h"
#include "drive-snapshot.h"
#include "drive-sound.h"
#include "drive.h"
#include "drivecpu.h"
#include "drivecpu65c02.h"
#include "drivemem.h"
#include "driverom.h"
#include "drivetypes.h"
#include "gcr.h"
#include "iecbus.h"
#include "iecdrive.h"
#include "lib.h"
#include "log.h"
#include "machine-bus.h"
#include "machine-drive.h"
#include "resources.h"
#include "rotation.h"
#include "snapshot.h"
#include "types.h"
#include "vdrive-bam.h"
#include "vdrive-snapshot.h"
#include "zfile.h"
#include "p64.h"

#ifdef __LIBRETRO__
#include "libretro-core.h"
extern char dc_savestate_filename[RETRO_PATH_MAX];
#endif

/* Currently the drive snapshot only handles 2 drives.  */

/* Logging.  */
static log_t drive_snapshot_log = LOG_ERR;

static int drive_snapshot_write_image_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_write_gcrimage_module(snapshot_t *s,
                                                unsigned int dnr);
static int drive_snapshot_write_p64image_module(snapshot_t *s,
                                                unsigned int dnr);
static int drive_snapshot_read_image_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_read_gcrimage_module(snapshot_t *s, unsigned int dnr);
static int drive_snapshot_read_p64image_module(snapshot_t *s, unsigned int dnr);

/*
This is the format of the DRIVE snapshot module.

Name                 Type   Size   Description

SyncFactor           DWORD  1      sync factor main cpu <-> drive cpu

Accum                DWORD  2
AttachClk            CLOCK  2      write protect handling on attach
BitsMoved            DWORD  2      number of bits moved since last access
ByteReady            BYTE   2      flag: Byte ready
ClockFrequency       BYTE   2      current clock frequency
CurrentHalfTrack     WORD   2      current half track of the r/w head
DetachClk            CLOCK  2      write protect handling on detach
DiskID1              BYTE   2      disk ID1
DiskID2              BYTE   2      disk ID2
ExtendImagePolicy    BYTE   2      Is extending the disk image allowed
FinishByte           BYTE   2      flag: Mode changed, finish byte
GCRHeadOffset        DWORD  2      offset from the begin of the track
GCRRead              BYTE   2      next value to read from disk
GCRWriteValue        BYTE   2      next value to write to disk
IdlingMethod         BYTE   2      What idle methode do we use
LastMode             BYTE   2      flag: Was the last mode read or write
ParallelCableEnabled BYTE   2      flag: Is the parallel cable enabed
ReadOnly             BYTE   2      flag: This disk is read only
RotationLastClk      CLOCK  2
RotationTablePtr     DWORD  2      pointer to the rotation table
                                   (offset to the rotation table is saved)
Type                 DWORD  2      drive type

*/

#define DRIVE_SNAP_MAJOR 1
#define DRIVE_SNAP_MINOR 5

int drive_snapshot_write_module(snapshot_t *s, int save_disks, int save_roms)
{
    int i;
    char snap_module_name[] = "DRIVE";
    snapshot_module_t *m;
    uint32_t rotation_table_ptr[NUM_DISK_UNITS];
    uint8_t GCR_image[NUM_DISK_UNITS], P64_image[NUM_DISK_UNITS];
    int drive_true_emulation;
    int sync_factor;
    drive_t *drive;

    resources_get_int("DriveTrueEmulation", &drive_true_emulation);

#ifdef __LIBRETRO__
    if (vdrive_snapshot_module_write(s, 1 ? 10 : 8) < 0) {
#else
    if (vdrive_snapshot_module_write(s, drive_true_emulation ? 10 : 8) < 0) {
#endif
        return -1;
    }

#ifndef __LIBRETRO__
    if (!drive_true_emulation) {
        return 0;
    }
#endif

    drive_gcr_data_writeback_all();

    /* TODO: drive 1? Is that loop for dual drives? or else?
       below there is NUM_DRIVES proposed
       NOTE(rhialto): I suspect that it was indeed about units 8 and 9. */

    rotation_table_get(rotation_table_ptr);

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        drive = diskunit_context[i]->drives[0];
        GCR_image[i] = (drive->GCR_image_loaded == 0 || !save_disks) ? 0 : 1;
        P64_image[i] = (drive->P64_image_loaded == 0 || !save_disks) ? 0 : 1;
    }

    m = snapshot_module_create(s, snap_module_name, DRIVE_SNAP_MAJOR,
                               DRIVE_SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

    resources_get_int("MachineVideoStandard", &sync_factor);

    if (SMW_DW(m, (uint32_t)sync_factor) < 0) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        return -1;
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        diskunit_context_t *unit = diskunit_context[i];
        drive = unit->drives[0];
        if (0
            || SMW_DW(m, (uint32_t)(drive->attach_clk)) < 0
            || SMW_B(m, (uint8_t)(drive->byte_ready_level)) < 0
            || SMW_B(m, (uint8_t)(unit->clock_frequency)) < 0
            || SMW_W(m, (uint16_t)(drive->current_half_track + (drive->side * DRIVE_HALFTRACKS_1571))) < 0
            || SMW_DW(m, (uint32_t)(drive->detach_clk)) < 0
            || SMW_B(m, (uint8_t)0) < 0
            || SMW_B(m, (uint8_t)0) < 0
            || SMW_B(m, (uint8_t)(drive->extend_image_policy)) < 0
            || SMW_DW(m, (uint32_t)(drive->GCR_head_offset)) < 0
            || SMW_B(m, (uint8_t)(drive->GCR_read)) < 0
            || SMW_B(m, (uint8_t)(drive->GCR_write_value)) < 0
            || SMW_B(m, (uint8_t)(unit->idling_method)) < 0
            || SMW_B(m, (uint8_t)(unit->parallel_cable)) < 0
            || SMW_B(m, (uint8_t)(drive->read_only)) < 0
            || SMW_DW(m, (uint32_t)(rotation_table_ptr[i])) < 0
            || SMW_DW(m, (uint32_t)(unit->type)) < 0

            /* rotation */
            || SMW_DW(m, (uint32_t)(drive->snap_accum)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_rotation_last_clk)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_bit_counter)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_zero_count)) < 0
            || SMW_W(m, (uint16_t)(drive->snap_last_read_data)) < 0
            || SMW_B(m, (uint8_t)(drive->snap_last_write_data)) < 0
            || SMW_DW(m, (uint8_t)(drive->snap_seed)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_speed_zone)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_ue7_dcba)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_ue7_counter)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_uf4_counter)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_fr_randcount)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_filter_counter)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_filter_state)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_filter_last_state)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_write_flux)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_PulseHeadPosition)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_xorShift32)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_so_delay)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_cycle_index)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_ref_advance)) < 0
            || SMW_DW(m, (uint32_t)(drive->snap_req_ref_cycles)) < 0

            ) {
            if (m != NULL) {
                snapshot_module_close(m);
            }
            return -1;
        }
    }

    /* new snapshot members */
    for (i = 0; i < NUM_DISK_UNITS; i++) {
        drive = diskunit_context[i]->drives[0];
        if (0
            || SMW_DW(m, (uint32_t)(drive->attach_detach_clk)) < 0
            ) {
            if (m != NULL) {
                snapshot_module_close(m);
            }
            return -1;
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        drive = diskunit_context[i]->drives[0];
        if (0
            || SMW_B(m, (uint8_t)(drive->byte_ready_edge)) < 0
            || SMW_B(m, (uint8_t)(drive->byte_ready_active)) < 0
            ) {
            if (m != NULL) {
                snapshot_module_close(m);
            }
            return -1;
        }
    }

    if (snapshot_module_close(m) < 0) {
        return -1;
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        diskunit_context_t *unit = diskunit_context[i];
        drive = unit->drives[0];

        if (unit->enable) {
            if (unit->type == DRIVE_TYPE_2000 || unit->type == DRIVE_TYPE_4000 ||
                unit->type == DRIVE_TYPE_CMDHD) {
                if (drivecpu65c02_snapshot_write_module(diskunit_context[i], s) < 0) {
                    return -1;
                }
            } else {
                if (drivecpu_snapshot_write_module(diskunit_context[i], s) < 0) {
                    return -1;
                }
            }
            if (machine_drive_snapshot_write(diskunit_context[i], s) < 0) {
                return -1;
            }
        }
    }

    if (save_disks) {
        int d;
        for (d = 0; d < NUM_DISK_UNITS; d++) {
            if (GCR_image[d] > 0) {
                if (drive_snapshot_write_gcrimage_module(s, d) < 0) {
                    return -1;
                }
            } else if (P64_image[d] > 0) {
                if (drive_snapshot_write_p64image_module(s, d) < 0) {
                    return -1;
                }
            } else {
                if (drive_snapshot_write_image_module(s, d) < 0) {
                    return -1;
                }
            }
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        diskunit_context_t *unit =  diskunit_context[i];
        drive = unit->drives[0];

        if (save_roms && unit->enable) {
            if (driverom_snapshot_write(s, drive) < 0) {
                return -1;
            }
        }
    }

    return 0;
}

int drive_snapshot_read_module(snapshot_t *s)
{
    uint8_t major_version, minor_version;
    int i;
    snapshot_module_t *m;
    char snap_module_name[] = "DRIVE";
    uint32_t rotation_table_ptr[NUM_DISK_UNITS];
    CLOCK attach_clk[NUM_DISK_UNITS];
    CLOCK detach_clk[NUM_DISK_UNITS];
    CLOCK attach_detach_clk[NUM_DISK_UNITS];
    int drive_true_emulation;
    int sync_factor;
    drive_t *drive;
    diskunit_context_t *unit;
    int dummy;
    int half_track[NUM_DISK_UNITS];

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        /* If this module is not found true emulation is off.  */
#ifndef __LIBRETRO__
        resources_set_int("DriveTrueEmulation", 0);
#endif
        return 0;
    }

    drive_gcr_data_writeback_all();

    /* reject snapshot modules newer than what we can handle (this VICE is too old) */
    if (snapshot_version_is_bigger(major_version, minor_version, DRIVE_SNAP_MAJOR, DRIVE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_HIGHER_VERSION);
        snapshot_module_close(m);
        return -1;
    }

    /* reject snapshot modules older than what we can handle (the snapshot is too old) */
    if (snapshot_version_is_smaller(major_version, minor_version, DRIVE_SNAP_MAJOR, DRIVE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        snapshot_module_close(m);
        return -1;
    }

#ifndef __LIBRETRO__
    /* If this module exists true emulation is enabled.  */
    /* XXX drive_true_emulation = 1 */
    resources_set_int("DriveTrueEmulation", 1);
#endif

    if (SMR_DW_INT(m, &sync_factor) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        unit = diskunit_context[i];
        drive = unit->drives[0];

        /* Partially read 1.0 snapshots */
        if (snapshot_version_is_equal(major_version, minor_version, 1, 0)) {
            if (0
                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_DW_INT(m, &dummy) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(unit->clock_frequency)) < 0
                || SMR_W_INT(m, &half_track[i]) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_B_INT(m, &dummy) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(unit->idling_method)) < 0
                || SMR_B_INT(m, &dummy) < 0
                || SMR_B_INT(m, &(unit->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(unit->type)) < 0
                ) {
                snapshot_module_close(m);
                return -1;
            }

            /* Partially read 1.1 snapshots */
        } else if (snapshot_version_is_equal(major_version, minor_version, 1, 1)) {
            if (0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(unit->clock_frequency)) < 0
                || SMR_W_INT(m, &half_track[i]) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(unit->idling_method)) < 0
                || SMR_B_INT(m, &(unit->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(unit->type)) < 0

                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW_INT(m, &(drive->snap_bit_counter)) < 0
                || SMR_DW_INT(m, &(drive->snap_zero_count)) < 0
                || SMR_W_INT(m, &(drive->snap_last_read_data)) < 0
                || SMR_B(m, &(drive->snap_last_write_data)) < 0
                || SMR_DW_INT(m, &(drive->snap_seed)) < 0
                ) {
                snapshot_module_close(m);
                return -1;
            }

            /* Partially read 1.2 snapshots */
        } else if (snapshot_version_is_equal(major_version, minor_version, 1, 2)) {
            if (0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(unit->clock_frequency)) < 0
                || SMR_W_INT(m, &half_track[i]) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(unit->idling_method)) < 0
                || SMR_B_INT(m, &(unit->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(unit->type)) < 0

                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW_INT(m, &(drive->snap_bit_counter)) < 0
                || SMR_DW_INT(m, &(drive->snap_zero_count)) < 0
                || SMR_W_INT(m, &(drive->snap_last_read_data)) < 0
                || SMR_B(m, &(drive->snap_last_write_data)) < 0
                || SMR_DW_INT(m, &(drive->snap_seed)) < 0
                || SMR_DW(m, &(drive->snap_speed_zone)) < 0
                || SMR_DW(m, &(drive->snap_ue7_dcba)) < 0
                || SMR_DW(m, &(drive->snap_ue7_counter)) < 0
                || SMR_DW(m, &(drive->snap_uf4_counter)) < 0
                || SMR_DW(m, &(drive->snap_fr_randcount)) < 0
                || SMR_DW(m, &(drive->snap_filter_counter)) < 0
                || SMR_DW(m, &(drive->snap_filter_state)) < 0
                || SMR_DW(m, &(drive->snap_filter_last_state)) < 0
                || SMR_DW(m, &(drive->snap_write_flux)) < 0
                || SMR_DW(m, &(drive->snap_PulseHeadPosition)) < 0
                || SMR_DW(m, &(drive->snap_xorShift32)) < 0
                ) {
                snapshot_module_close(m);
                return -1;
            }
        } else if (snapshot_version_is_equal(major_version, minor_version, 1, 3)) {
            if (0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(unit->clock_frequency)) < 0
                || SMR_W_INT(m, &half_track[i]) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(unit->idling_method)) < 0
                || SMR_B_INT(m, &(unit->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(unit->type)) < 0

                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW_INT(m, &(drive->snap_bit_counter)) < 0
                || SMR_DW_INT(m, &(drive->snap_zero_count)) < 0
                || SMR_W_INT(m, &(drive->snap_last_read_data)) < 0
                || SMR_B(m, &(drive->snap_last_write_data)) < 0
                || SMR_DW_INT(m, &(drive->snap_seed)) < 0
                || SMR_DW(m, &(drive->snap_speed_zone)) < 0
                || SMR_DW(m, &(drive->snap_ue7_dcba)) < 0
                || SMR_DW(m, &(drive->snap_ue7_counter)) < 0
                || SMR_DW(m, &(drive->snap_uf4_counter)) < 0
                || SMR_DW(m, &(drive->snap_fr_randcount)) < 0
                || SMR_DW(m, &(drive->snap_filter_counter)) < 0
                || SMR_DW(m, &(drive->snap_filter_state)) < 0
                || SMR_DW(m, &(drive->snap_filter_last_state)) < 0
                || SMR_DW(m, &(drive->snap_write_flux)) < 0
                || SMR_DW(m, &(drive->snap_PulseHeadPosition)) < 0
                || SMR_DW(m, &(drive->snap_xorShift32)) < 0
                || SMR_DW(m, &(drive->snap_so_delay)) < 0
                ) {
                snapshot_module_close(m);
                return -1;
            }
        } else {
            if (0
                || SMR_DW(m, &(attach_clk[i])) < 0
                || SMR_B_INT(m, (int *)&(drive->byte_ready_level)) < 0
                || SMR_B_INT(m, &(unit->clock_frequency)) < 0
                || SMR_W_INT(m, &half_track[i]) < 0
                || SMR_DW(m, &(detach_clk[i])) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B(m, (uint8_t *)&dummy) < 0
                || SMR_B_INT(m, &(drive->extend_image_policy)) < 0
                || SMR_DW_UINT(m, &(drive->GCR_head_offset)) < 0
                || SMR_B(m, &(drive->GCR_read)) < 0
                || SMR_B(m, &(drive->GCR_write_value)) < 0
                || SMR_B_INT(m, &(unit->idling_method)) < 0
                || SMR_B_INT(m, &(unit->parallel_cable)) < 0
                || SMR_B_INT(m, &(drive->read_only)) < 0
                || SMR_DW(m, &rotation_table_ptr[i]) < 0
                || SMR_DW_UINT(m, &(unit->type)) < 0

                || SMR_DW_UL(m, &(drive->snap_accum)) < 0
                || SMR_DW(m, &(drive->snap_rotation_last_clk)) < 0
                || SMR_DW_INT(m, &(drive->snap_bit_counter)) < 0
                || SMR_DW_INT(m, &(drive->snap_zero_count)) < 0
                || SMR_W_INT(m, &(drive->snap_last_read_data)) < 0
                || SMR_B(m, &(drive->snap_last_write_data)) < 0
                || SMR_DW_INT(m, &(drive->snap_seed)) < 0
                || SMR_DW(m, &(drive->snap_speed_zone)) < 0
                || SMR_DW(m, &(drive->snap_ue7_dcba)) < 0
                || SMR_DW(m, &(drive->snap_ue7_counter)) < 0
                || SMR_DW(m, &(drive->snap_uf4_counter)) < 0
                || SMR_DW(m, &(drive->snap_fr_randcount)) < 0
                || SMR_DW(m, &(drive->snap_filter_counter)) < 0
                || SMR_DW(m, &(drive->snap_filter_state)) < 0
                || SMR_DW(m, &(drive->snap_filter_last_state)) < 0
                || SMR_DW(m, &(drive->snap_write_flux)) < 0
                || SMR_DW(m, &(drive->snap_PulseHeadPosition)) < 0
                || SMR_DW(m, &(drive->snap_xorShift32)) < 0
                || SMR_DW(m, &(drive->snap_so_delay)) < 0
                || SMR_DW(m, &(drive->snap_cycle_index)) < 0
                || SMR_DW(m, &(drive->snap_ref_advance)) < 0
                || SMR_DW(m, &(drive->snap_req_ref_cycles)) < 0
                ) {
                snapshot_module_close(m);
                return -1;
            }
        }
    }

    /* this one is new, so don't test so stay compatible with old snapshots */
    for (i = 0; i < NUM_DISK_UNITS; i++) {
        drive = diskunit_context[i]->drives[0];
        SMR_DW(m, &(attach_detach_clk[i]));
    }

    /* these are even newer */
    for (i = 0; i < NUM_DISK_UNITS; i++) {
        drive = diskunit_context[i]->drives[0];
        SMR_B_INT(m, (int *)&(drive->byte_ready_edge));
        SMR_B_INT(m, (int *)&(drive->byte_ready_active));
    }

    snapshot_module_close(m);
    m = NULL;

    rotation_table_set(rotation_table_ptr);

    /* TODO: make a loop */
    unit = diskunit_context[0];

    switch (unit->type) {
        case DRIVE_TYPE_1540:
        case DRIVE_TYPE_1541:
        case DRIVE_TYPE_1541II:
        case DRIVE_TYPE_1551:
        case DRIVE_TYPE_1570:
        case DRIVE_TYPE_1571:
        case DRIVE_TYPE_1571CR:
        case DRIVE_TYPE_1581:
        case DRIVE_TYPE_2000:
        case DRIVE_TYPE_4000:
        case DRIVE_TYPE_CMDHD:
        case DRIVE_TYPE_2031:
        case DRIVE_TYPE_1001:
        case DRIVE_TYPE_2040:
        case DRIVE_TYPE_3040:
        case DRIVE_TYPE_4040:
        case DRIVE_TYPE_8050:
        case DRIVE_TYPE_8250:
        case DRIVE_TYPE_9000:
#ifndef __LIBRETRO__
            unit->enable = 1;
#endif
            machine_drive_rom_setup_image(0);
            drivemem_init(diskunit_context[0]);
            resources_set_int("Drive8IdleMethod", unit->idling_method);
            driverom_initialize_traps(diskunit_context[0]);
            drive_set_active_led_color(unit->type, 0);
            machine_bus_status_drivetype_set(8, 1);
            break;
        case DRIVE_TYPE_NONE:
            drive_disable(diskunit_context[0]);
            machine_bus_status_drivetype_set(8, 0);
            break;
        default:
            return -1;
    }

    unit = diskunit_context[1];

    switch (unit->type) {
        case DRIVE_TYPE_1540:
        case DRIVE_TYPE_1541:
        case DRIVE_TYPE_1541II:
        case DRIVE_TYPE_1551:
        case DRIVE_TYPE_1570:
        case DRIVE_TYPE_1571:
        case DRIVE_TYPE_1581:
        case DRIVE_TYPE_2000:
        case DRIVE_TYPE_4000:
        case DRIVE_TYPE_CMDHD:
        case DRIVE_TYPE_2031:
        case DRIVE_TYPE_1001:
        case DRIVE_TYPE_2040:
        case DRIVE_TYPE_3040:
        case DRIVE_TYPE_4040:
        case DRIVE_TYPE_8050:
        case DRIVE_TYPE_8250:
        case DRIVE_TYPE_9000:
            unit->enable = 1;
            machine_drive_rom_setup_image(1);
            drivemem_init(diskunit_context[1]);
            resources_set_int("Drive9IdleMethod", unit->idling_method);
            driverom_initialize_traps(diskunit_context[1]);
            drive_set_active_led_color(unit->type, 1);
            machine_bus_status_drivetype_set(9, 1);
            break;
        case DRIVE_TYPE_NONE:
            drive_disable(diskunit_context[1]);
            machine_bus_status_drivetype_set(9, 0);
            break;
        default:
            return -1;
    }

    unit = diskunit_context[2];

    switch (unit->type) {
        case DRIVE_TYPE_1540:
        case DRIVE_TYPE_1541:
        case DRIVE_TYPE_1541II:
        case DRIVE_TYPE_1551:
        case DRIVE_TYPE_1570:
        case DRIVE_TYPE_1571:
        case DRIVE_TYPE_1571CR:
        case DRIVE_TYPE_1581:
        case DRIVE_TYPE_2000:
        case DRIVE_TYPE_4000:
        case DRIVE_TYPE_CMDHD:
        case DRIVE_TYPE_2031:
        case DRIVE_TYPE_1001:
        case DRIVE_TYPE_2040:
        case DRIVE_TYPE_3040:
        case DRIVE_TYPE_4040:
        case DRIVE_TYPE_8050:
        case DRIVE_TYPE_8250:
        case DRIVE_TYPE_9000:
            unit->enable = 1;
            machine_drive_rom_setup_image(2);
            drivemem_init(diskunit_context[2]);
            resources_set_int("Drive10IdleMethod", unit->idling_method);
            driverom_initialize_traps(diskunit_context[2]);
            drive_set_active_led_color(unit->type, 0);
            machine_bus_status_drivetype_set(10, 1);
            break;
        case DRIVE_TYPE_NONE:
            drive_disable(diskunit_context[2]);
            machine_bus_status_drivetype_set(10, 0);
            break;
        default:
            return -1;
    }

    unit = diskunit_context[3];

    switch (unit->type) {
        case DRIVE_TYPE_1540:
        case DRIVE_TYPE_1541:
        case DRIVE_TYPE_1541II:
        case DRIVE_TYPE_1551:
        case DRIVE_TYPE_1570:
        case DRIVE_TYPE_1571:
        case DRIVE_TYPE_1581:
        case DRIVE_TYPE_2000:
        case DRIVE_TYPE_4000:
        case DRIVE_TYPE_CMDHD:
        case DRIVE_TYPE_2031:
        case DRIVE_TYPE_1001:
        case DRIVE_TYPE_2040:
        case DRIVE_TYPE_3040:
        case DRIVE_TYPE_4040:
        case DRIVE_TYPE_8050:
        case DRIVE_TYPE_8250:
        case DRIVE_TYPE_9000:
            unit->enable = 1;
            machine_drive_rom_setup_image(3);
            drivemem_init(diskunit_context[3]);
            resources_set_int("Drive11IdleMethod", unit->idling_method);
            driverom_initialize_traps(diskunit_context[3]);
            drive_set_active_led_color(unit->type, 1);
            machine_bus_status_drivetype_set(11, 1);
            break;
        case DRIVE_TYPE_NONE:
            drive_disable(diskunit_context[3]);
            machine_bus_status_drivetype_set(11, 0);
            break;
        default:
            return -1;
    }

    /* Clear parallel cable before undumping parallel port values.  */
    for (i = 0; i < DRIVE_PC_NUM; i++) {
        parallel_cable_drive_write(i, 0xff, PARALLEL_WRITE, 0);
        parallel_cable_drive_write(i, 0xff, PARALLEL_WRITE, 1);
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        unit = diskunit_context[i];

        if (unit->enable) {
            if (unit->type == DRIVE_TYPE_2000 || unit->type == DRIVE_TYPE_4000 ||
                unit->type == DRIVE_TYPE_CMDHD) {
                if (drivecpu65c02_snapshot_read_module(diskunit_context[i], s) < 0) {
                    return -1;
                }
            } else {
                if (drivecpu_snapshot_read_module(diskunit_context[i], s) < 0) {
#ifndef __LIBRETRO__
                    return -1;
#endif
                }
            }
            if (machine_drive_snapshot_read(diskunit_context[i], s) < 0) {
#ifndef __LIBRETRO__
                return -1;
#endif
            }
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        if (drive_snapshot_read_image_module(s, i) < 0
            || drive_snapshot_read_gcrimage_module(s, i) < 0
            || drive_snapshot_read_p64image_module(s, i) < 0) {
            return -1;
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        if (driverom_snapshot_read(s, diskunit_context[i]->drives[0]) < 0) {
            return -1;
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        unit = diskunit_context[i];
        drive = unit->drives[0];

        if (unit->type != DRIVE_TYPE_NONE) {
            drive_enable(diskunit_context[i]);
            drive->attach_clk = attach_clk[i];
            drive->detach_clk = detach_clk[i];
            drive->attach_detach_clk = attach_detach_clk[i];
        }
    }

    for (i = 0; i < NUM_DISK_UNITS; i++) {
        int side = 0;
        unit = diskunit_context[i];
        drive = unit->drives[0];

        if (unit->type == DRIVE_TYPE_1570
            || unit->type == DRIVE_TYPE_1571
            || unit->type == DRIVE_TYPE_1571CR) {
            if (half_track[i] > (DRIVE_HALFTRACKS_1571 + 1)) {
                side = 1;
                half_track[i] -= DRIVE_HALFTRACKS_1571;
            }
        }
        /* TODO: drive 1? */
        drive_set_half_track(half_track[i], side, drive);
        resources_set_int("MachineVideoStandard", sync_factor);
    }

    /* stop currently active drive sounds (bug #3539422)
     * FIXME: when the drive sound emulation becomes more precise, we might
     *        want/need to save a snapshot of its current state too
     */
    drive_sound_stop();

    iec_update_ports_embedded();
    drive_update_ui_status();

#ifdef __LIBRETRO__
    if (vdrive_snapshot_module_read(s, 1 ? 10 : 8) < 0) {
#else
    resources_get_int("DriveTrueEmulation", &drive_true_emulation);

    if (vdrive_snapshot_module_read(s, drive_true_emulation ? 10 : 8) < 0) {
#endif
        return -1;
    }

    return 0;
}

/* -------------------------------------------------------------------- */
/* read/write "normal" disk image snapshot module */

#define IMAGE_SNAP_MAJOR 1
#define IMAGE_SNAP_MINOR 0

/*
 * This image format is pretty simple:
 *
 * WORD Type            Disk image type (1581, 8050, 8250)
 * 256 * blocks(disk image type) BYTE
 *                      disk image
 *
 */

static int drive_snapshot_write_image_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    uint8_t sector_data[0x100];
    uint16_t word;
    disk_addr_t dadr;
    int rc;
    drive_t *drive;

    /* TODO: drive 1? */
    drive = diskunit_context[dnr]->drives[0];

    if (drive->image == NULL || diskunit_context[dnr]->type == DRIVE_TYPE_CMDHD) {
        sprintf(snap_module_name, "NOIMAGE%u", dnr);
    } else {
        sprintf(snap_module_name, "IMAGE%u", dnr);
    }

    m = snapshot_module_create(s, snap_module_name, IMAGE_SNAP_MAJOR,
                               IMAGE_SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

    if (drive->image == NULL || diskunit_context[dnr]->type == DRIVE_TYPE_CMDHD) {
        if (snapshot_module_close(m) < 0) {
            return -1;
        }

        return 0;
    }

    word = drive->image->type;
    SMW_W(m, word);

    /* we use the return code to step through the tracks. So we do not
       need any geometry info. */
    for (dadr.track = 1;; dadr.track++) {
        rc = 0;
        for (dadr.sector = 0;; dadr.sector++) {
            rc = disk_image_read_sector(drive->image, sector_data, &dadr);
            if (rc == 0) {
                SMW_BA(m, sector_data, 0x100);
            } else {
                break;
            }
        }
        if (dadr.sector == 0) {
            break;
        }
    }

    if (snapshot_module_close(m) < 0) {
        return -1;
    }
    return 0;
}

static int drive_snapshot_read_image_module(snapshot_t *s, unsigned int dnr)
{
    uint8_t major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    uint16_t word;
    char *filename = NULL;
    char *request_str;
    int len = 0;
    FILE *fp;
    uint8_t sector_data[0x100];
    disk_addr_t dadr;
    int rc;
    drive_t *drive;

    /* TODO: drive 1? */
    drive = diskunit_context[dnr]->drives[0];

    sprintf(snap_module_name, "NOIMAGE%u", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m != NULL) {
        /* do not detach an existing DHD image as they aren't saved in the snapshot */
        if (diskunit_context[dnr]->type != DRIVE_TYPE_CMDHD) {
            file_system_detach_disk(dnr + 8, 0);
        }
        file_system_detach_disk(dnr + 8, 1);
        snapshot_module_close(m);
        return 0;
    }

    sprintf(snap_module_name, "IMAGE%u", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        return 0;
    }

    /* reject snapshot modules newer than what we can handle (this VICE is too old) */
    if (snapshot_version_is_bigger(major_version, minor_version, IMAGE_SNAP_MAJOR, IMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_HIGHER_VERSION);
        snapshot_module_close(m);
        return -1;
    }

    /* reject snapshot modules older than what we can handle (the snapshot is too old) */
    if (snapshot_version_is_smaller(major_version, minor_version, IMAGE_SNAP_MAJOR, IMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        snapshot_module_close(m);
        return -1;
    }

    if (SMR_W(m, &word) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    switch (word) {
        case 1581:
            len = D81_FILE_SIZE;
            break;
        case 8050:
            len = D80_FILE_SIZE;
            break;
        case 8250:
            len = D82_FILE_SIZE;
            break;
        case 9000:
            len = drive->image->tracks * drive->image->sectors * 256;
            break;
        default:
            log_error(drive_snapshot_log,
                      "Snapshot of disk image unknown (type %d)",
                      (int)word);
            snapshot_module_close(m);
            return -1;
    }

    /* create temporary file of the right size */
    fp = archdep_mkstemp_fd(&filename, MODE_WRITE);

    if (fp == NULL) {
        log_error(drive_snapshot_log, "Could not create temporary file!");
        snapshot_module_close(m);
        return -1;
    }

    /* blow up the file to needed size */
    if (fseek(fp, len - 1, SEEK_SET) < 0
        || (fputc(0, fp) == EOF)) {
        log_error(drive_snapshot_log, "Could not create large temporary file");
        fclose(fp);
        lib_free(filename);
        snapshot_module_close(m);
        return -1;
    }

    fclose(fp);
    lib_free(filename);

    if (file_system_attach_disk(dnr + 8, 0, filename) < 0) {
        log_error(drive_snapshot_log, "Invalid Disk Image");
        lib_free(filename);
        snapshot_module_close(m);
        return -1;
    }
    /* TODO: drive 1 */

    request_str = lib_msprintf("Disk image unit #%d imported from snapshot",
                               dnr + 8);
    zfile_close_action(filename, ZFILE_REQUEST, request_str);
    lib_free(request_str);

    /* we use the return code to step through the tracks. So we do not
       need any geometry info. */
    SMR_BA(m, sector_data, 0x100);
    for (dadr.track = 1;; dadr.track++) {
        rc = 0;
        for (dadr.sector = 0;; dadr.sector++) {
            rc = disk_image_write_sector(drive->image, sector_data, &dadr);
            if (rc == 0) {
                SMR_BA(m, sector_data, 0x100);
            } else {
                break;
            }
        }
        if (dadr.sector == 0) {
            break;
        }
    }

    /* TODO: drive 1 */
    vdrive_bam_reread_bam(dnr + 8, 0);

    snapshot_module_close(m);
    m = NULL;

    return 0;
}

/* -------------------------------------------------------------------- */
/* read/write GCR disk image snapshot module */

#define GCRIMAGE_SNAP_MAJOR 3
#define GCRIMAGE_SNAP_MINOR 1

static int drive_snapshot_write_gcrimage_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    uint8_t *data;
    unsigned int i;
    drive_t *drive;
    uint32_t num_half_tracks, track_size;

    drive = diskunit_context[dnr]->drives[0];
    sprintf(snap_module_name, "GCRIMAGE%u", dnr);

    m = snapshot_module_create(s, snap_module_name, GCRIMAGE_SNAP_MAJOR,
                               GCRIMAGE_SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

#ifdef __LIBRETRO__
    char filename[RETRO_PATH_MAX] = {0};
    snprintf(filename, sizeof(filename), "___%s", path_basename(file_system_get_disk_name(dnr+8, 0)));
    SMW_BA(m, (const unsigned char *)filename, RETRO_PATH_MAX);
    snapshot_module_close(m);
    return 0;
#else
    num_half_tracks = MAX_TRACKS_1571 * 2;

    /* Write general data */
    if (SMW_DW(m, num_half_tracks) < 0) {
        snapshot_module_close(m);
        return -1;
    }

    /* Write half track data */
    for (i = 0; i < num_half_tracks; i++) {
        data = drive->gcr->tracks[i].data;
        track_size = data ? drive->gcr->tracks[i].size : 0;
        if (0
            || SMW_DW(m, (uint32_t)track_size) < 0
            || (track_size && SMW_BA(m, data, track_size) < 0)
            ) {
            break;
        }
    }

    if (snapshot_module_close(m) < 0 || (i != num_half_tracks)) {
        return -1;
    }

    return 0;
#endif
}

static int drive_snapshot_read_gcrimage_module(snapshot_t *s, unsigned int dnr)
{
    uint8_t major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    uint8_t *data;
    unsigned int i;
    drive_t *drive;
    uint32_t num_half_tracks, track_size;

    drive = diskunit_context[dnr]->drives[0];
    sprintf(snap_module_name, "GCRIMAGE%u", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        return 0;
    }

    /* reject snapshot modules newer than what we can handle (this VICE is too old) */
    if (snapshot_version_is_bigger(major_version, minor_version, GCRIMAGE_SNAP_MAJOR, GCRIMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_HIGHER_VERSION);
        snapshot_module_close(m);
        return -1;
    }

    /* reject snapshot modules older than what we can handle (the snapshot is too old) */
    if (snapshot_version_is_smaller(major_version, minor_version, GCRIMAGE_SNAP_MAJOR, GCRIMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        snapshot_module_close(m);
        return -1;
    }

#ifdef __LIBRETRO__
    int c = 0;
    char filename[RETRO_PATH_MAX] = {0};
    for (i = 0; i < RETRO_PATH_MAX; i++)
    {
        if (snapshot_module_read_byte_into_int(m, &c) < 0)
            c = 0;

        if (i < 3)
        {
            if (c != '_')
                break;
            else
                continue;
        }

        if (!c)
        {
            filename[i] = '\0';
            break;
        }

        filename[i-3] = c;
    }

    if (filename[0] != '\0')
        snprintf(dc_savestate_filename, sizeof(dc_savestate_filename), "%s", filename);
    else
    {
        snapshot_module_close(m);
        m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);

        if (0
            || SMR_DW(m, &num_half_tracks) < 0
            || num_half_tracks > MAX_GCR_TRACKS) {
            snapshot_module_close(m);
            return -1;
        }

        for (i = 0; i < num_half_tracks; i++) {
            if (SMR_DW(m, &track_size) < 0
                || track_size > NUM_MAX_MEM_BYTES_TRACK) {
                snapshot_module_close(m);
                return -1;
            }

            if (track_size) {
                if (drive->gcr->tracks[i].data == NULL) {
                    drive->gcr->tracks[i].data = lib_calloc(1, track_size);
                } else if (drive->gcr->tracks[i].size != (int)track_size) {
                    drive->gcr->tracks[i].data = lib_realloc(drive->gcr->tracks[i].data, track_size);
                }
                memset(drive->gcr->tracks[i].data, 0, track_size);
            } else {
                if (drive->gcr->tracks[i].data) {
                    lib_free(drive->gcr->tracks[i].data);
                    drive->gcr->tracks[i].data = NULL;
                }
            }
            data = drive->gcr->tracks[i].data;
            drive->gcr->tracks[i].size = track_size;

            if (track_size && SMR_BA(m, data, track_size) < 0) {
                snapshot_module_close(m);
                return -1;
            }
        }
        for (; i < MAX_GCR_TRACKS; i++) {
            if (drive->gcr->tracks[i].data) {
                lib_free(drive->gcr->tracks[i].data);
                drive->gcr->tracks[i].data = NULL;
                drive->gcr->tracks[i].size = 0;
            }
        }
    }
    snapshot_module_close(m);

    drive->GCR_image_loaded = 1;
    drive->complicated_image_loaded = 1;
    drive->image = NULL;
#else
    if (0
        || SMR_DW(m, &num_half_tracks) < 0
        || num_half_tracks > MAX_GCR_TRACKS) {
        snapshot_module_close(m);
        return -1;
    }

    for (i = 0; i < num_half_tracks; i++) {
        if (SMR_DW(m, &track_size) < 0
            || track_size > NUM_MAX_MEM_BYTES_TRACK) {
            snapshot_module_close(m);
            return -1;
        }

        if (track_size) {
            if (drive->gcr->tracks[i].data == NULL) {
                drive->gcr->tracks[i].data = lib_calloc(1, track_size);
            } else if (drive->gcr->tracks[i].size != (int)track_size) {
                drive->gcr->tracks[i].data = lib_realloc(drive->gcr->tracks[i].data, track_size);
            }
            memset(drive->gcr->tracks[i].data, 0, track_size);
        } else {
            if (drive->gcr->tracks[i].data) {
                lib_free(drive->gcr->tracks[i].data);
                drive->gcr->tracks[i].data = NULL;
            }
        }
        data = drive->gcr->tracks[i].data;
        drive->gcr->tracks[i].size = track_size;

        if (track_size && SMR_BA(m, data, track_size) < 0) {
            snapshot_module_close(m);
            return -1;
        }
    }
    for (; i < MAX_GCR_TRACKS; i++) {
        if (drive->gcr->tracks[i].data) {
            lib_free(drive->gcr->tracks[i].data);
            drive->gcr->tracks[i].data = NULL;
            drive->gcr->tracks[i].size = 0;
        }
    }
    snapshot_module_close(m);

    drive->GCR_image_loaded = 1;
    drive->complicated_image_loaded = 1; /* TODO: verify if it's really like this */
    drive->image = NULL;
#endif

    return 0;
}

/* -------------------------------------------------------------------- */
/* read/write P64 disk image snapshot module */

#define P64IMAGE_SNAP_MAJOR 1
#define P64IMAGE_SNAP_MINOR 0

static int drive_snapshot_write_p64image_module(snapshot_t *s, unsigned int dnr)
{
    char snap_module_name[10];
    snapshot_module_t *m;
    drive_t *drive;
    TP64MemoryStream P64MemoryStreamInstance;
    PP64Image P64Image;

    drive = diskunit_context[dnr]->drives[0];
    sprintf(snap_module_name, "P64IMAGE%u", dnr);

    m = snapshot_module_create(s, snap_module_name, GCRIMAGE_SNAP_MAJOR,
                               GCRIMAGE_SNAP_MINOR);
    if (m == NULL) {
        return -1;
    }

    P64Image = (void*)drive->p64;

    if (P64Image == NULL) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        return -1;
    }

    P64MemoryStreamCreate(&P64MemoryStreamInstance);
    P64MemoryStreamClear(&P64MemoryStreamInstance);
    if (!P64ImageWriteToStream(P64Image, &P64MemoryStreamInstance)) {
        P64MemoryStreamDestroy(&P64MemoryStreamInstance);
        return -1;
    }

    if (SMW_DW(m, P64MemoryStreamInstance.Size) < 0 ||
        SMW_BA(m, P64MemoryStreamInstance.Data, P64MemoryStreamInstance.Size) < 0) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        P64MemoryStreamDestroy(&P64MemoryStreamInstance);
        return -1;
    }

    P64MemoryStreamDestroy(&P64MemoryStreamInstance);

    if (snapshot_module_close(m) < 0) {
        return -1;
    }

    return 0;
}

static int drive_snapshot_read_p64image_module(snapshot_t *s, unsigned int dnr)
{
    uint8_t major_version, minor_version;
    snapshot_module_t *m;
    char snap_module_name[10];
    uint8_t *tmpbuf;
    drive_t *drive;
    TP64MemoryStream P64MemoryStreamInstance;
    PP64Image P64Image;
    uint32_t size;

    drive = diskunit_context[dnr]->drives[0];
    sprintf(snap_module_name, "P64IMAGE%u", dnr);

    m = snapshot_module_open(s, snap_module_name,
                             &major_version, &minor_version);
    if (m == NULL) {
        return 0;
    }

    P64Image = (void*)drive->p64;

    if (P64Image == NULL) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        return -1;
    }

    /* reject snapshot modules newer than what we can handle (this VICE is too old) */
    if (snapshot_version_is_bigger(major_version, minor_version, P64IMAGE_SNAP_MAJOR, P64IMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_HIGHER_VERSION);
        snapshot_module_close(m);
        return -1;
    }

    /* reject snapshot modules older than what we can handle (the snapshot is too old) */
    if (snapshot_version_is_smaller(major_version, minor_version, P64IMAGE_SNAP_MAJOR, P64IMAGE_SNAP_MINOR)) {
        snapshot_set_error(SNAPSHOT_MODULE_INCOMPATIBLE);
        snapshot_module_close(m);
        return -1;
    }

    if (SMR_DW(m, &size) < 0) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        return -1;
    }

    tmpbuf = lib_malloc(size);

    if (SMR_BA(m, tmpbuf, size) < 0) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        lib_free(tmpbuf);
        return -1;
    }

    P64MemoryStreamCreate(&P64MemoryStreamInstance);
    P64MemoryStreamClear(&P64MemoryStreamInstance);
    P64MemoryStreamWrite(&P64MemoryStreamInstance, tmpbuf, size);
    P64MemoryStreamSeek(&P64MemoryStreamInstance, 0);
    if (!P64ImageReadFromStream(P64Image, &P64MemoryStreamInstance)) {
        if (m != NULL) {
            snapshot_module_close(m);
        }
        lib_free(tmpbuf);
        P64MemoryStreamDestroy(&P64MemoryStreamInstance);
        return -1;
    }

    P64MemoryStreamDestroy(&P64MemoryStreamInstance);

    snapshot_module_close(m);
    m = NULL;

    lib_free(tmpbuf);

    drive->P64_image_loaded = 1;
    drive->complicated_image_loaded = 1;
    drive->image = NULL;

    return 0;
}
