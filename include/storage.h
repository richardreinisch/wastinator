#pragma once
#include "config.h"

void storage_init();
void storage_load(AppConfig& cfg);
void storage_save(const AppConfig& cfg);
void storage_reset(AppConfig& cfg);
