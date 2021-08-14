/*
 * Copyright (c) 2016, NVIDIA CORPORATION, All rights reserved.
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "bootctrl_nvidia.h"

int main(int argc, char *argv[])
{
    smd_partition_t bootC;

    if (argc < 2) {
        printf("Usage: nv_smd_generator <out_file>\n");
        return -1;
    }

    bootC.slot_info[0].priority = 15;
    bootC.slot_info[0].retry_count = 7;
    bootC.slot_info[0].boot_successful = 1;
    strncpy(bootC.slot_info[0].suffix, BOOTCTRL_SUFFIX_A, 2);

    bootC.slot_info[1].priority = 10;
    bootC.slot_info[1].retry_count = 7;
    bootC.slot_info[1].boot_successful = 1;
    strncpy(bootC.slot_info[1].suffix, BOOTCTRL_SUFFIX_B, 2);

    bootC.magic = BOOTCTRL_MAGIC;
    bootC.version = BOOTCTRL_VERSION;
    bootC.num_slots = MAX_SLOTS;

    FILE* fout = fopen(argv[1], "w+");
    fwrite(&bootC, sizeof(smd_partition_t), 1, fout);
    fclose(fout);
    return 0;
}
