#pragma once
#include "config.h"
#include <WebServer.h>

void webserver_init(AppConfig& cfg);
void webserver_handle();
void webserver_stop();
bool webserver_running();
