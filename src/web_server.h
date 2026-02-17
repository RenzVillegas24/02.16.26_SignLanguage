/*
 * @file web_server.h
 * @brief WiFi AP + HTTP server for WEB predict mode
 */
#pragma once

#include <Arduino.h>
#include "config.h"

void web_server_start();                       // Start WiFi AP + HTTP server
void web_server_stop();                        // Stop WiFi AP + server
void web_server_update(const SensorData &d,    // Push data to connected clients
                       const char *gesture);
bool web_server_is_running();
int  web_server_num_clients();                 // Number of stations connected to AP
String web_server_get_url();                   // e.g. "http://192.168.4.1"
