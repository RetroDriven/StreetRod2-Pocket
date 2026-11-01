/* SPDX-License-Identifier: MIT */
#ifndef SR2_ASSET_CACHE_H
#define SR2_ASSET_CACHE_H
#include <stdint.h>
#include <stdio.h>
unsigned sr2_asset_cache_preload(uint32_t budget);
/* Only exact read-only library names are eligible. Returns a real stdio
 * stream with independent position; NULL retains the original disk path. */
FILE *sr2_asset_cache_open(const char *name);
#endif
