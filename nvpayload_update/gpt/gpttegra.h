/*
    Implementation of GPTData class derivative with handling for Tegra quirks
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

#ifndef __GPTDATATEGRA_H
#define __GPTDATATEGRA_H

#include "gpt.h"

using namespace std;

class GPTDataTegra : public GPTData {
   protected:
   public:
      GPTDataTegra(void);
      ~GPTDataTegra(void);

      int LoadTegraGPTData(void);

      uint64_t GetOffset(uint8_t index);
      uint64_t GetSize(uint8_t index);
      bool MatchPartition(std::string part_name, uint8_t *index);
}; // class GPTDataTegra

#endif // __GPTDATATEGRA_H
