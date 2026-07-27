/*
  SITL replacement for the per MCU eeprom.h
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void read_flash_bin(uint8_t *data, uint32_t add, int out_buff_len);
bool save_flash_nolib(const uint8_t *data, uint32_t length, uint32_t add);
