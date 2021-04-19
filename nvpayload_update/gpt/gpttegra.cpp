/*
    Copyright (C) 2021 The LineageOS Project

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

/* Implementation of GPTData class derivative with handling for Tegra quirks */

#include "gpttegra.h"

#include <iostream>

using namespace std;

/********************************************
 *                                          *
 * GPTDataTegra class and related structures *
 *                                          *
 ********************************************/

GPTDataTegra::GPTDataTegra(void) : GPTData() {
} // default constructor

GPTDataTegra::~GPTDataTegra(void) {
} // default destructor

// Loads the GPT, as much as possible. Returns 1 if this seems to have
// succeeded, 0 if there are obvious problems....
int GPTDataTegra::LoadTegraGPTData(void) {
   int allOK = ForceLoadGPTData();

   // For emmc boot devices, Tegra combines boot0 and boot1, which breaks
   // normal gpt library calculations. Adjust some header locations to match
   // what is expected.
   if (device.find("boot1") != std::string::npos &&
       secondHeader.lastUsableLBA > diskSize) {
      mainHeader.lastUsableLBA -= diskSize;
      secondHeader.lastUsableLBA -= diskSize;
      secondHeader.partitionEntriesLBA -= diskSize;
   }

   allOK = LoadSecondTableAsMain() && allOK;

   return allOK;
} // GPTData::LoadTegraGPTData()

uint64_t GPTDataTegra::GetOffset(uint8_t index) {
   return partitions[index].GetFirstLBA() * GetBlockSize();
} // GPTData::GetOffset()

uint64_t GPTDataTegra::GetSize(uint8_t index) {
   return partitions[index].GetLengthLBA() * GetBlockSize();
} // GPTData::GetSize()

bool GPTDataTegra::MatchPartition(std::string part_name, uint8_t *index) {
   for (uint8_t i = 0; i < numParts; i++) {
      if (part_name.compare(partitions[i].GetDescription()) == 0) {
         *index = i;
         return true;
      }
   }

   *index = 0;
   return false;
} // GPTData::MatchPartition()
