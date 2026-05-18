#ifndef SETTINGS_H
#define SETTINGS_H

#include <Preferences.h>
#include "config.h"

Preferences prefs;

// Текущие настройки в RAM
char cfg_ssid[64];
char cfg_password[64];
char cfg_host_ip[32];
unsigned int cfg_udp_port;
unsigned int cfg_host_port;

void settings_load() {
  prefs.begin("teletrack", false);
  
  // Читаем в буфер, если ключа нет — используем дефолт
  if (!prefs.getString("ssid", cfg_ssid, sizeof(cfg_ssid)))
    strncpy(cfg_ssid, DEFAULT_SSID, sizeof(cfg_ssid));
    
  if (!prefs.getString("password", cfg_password, sizeof(cfg_password)))
    strncpy(cfg_password, DEFAULT_PASSWORD, sizeof(cfg_password));
    
  if (!prefs.getString("host_ip", cfg_host_ip, sizeof(cfg_host_ip)))
    strncpy(cfg_host_ip, DEFAULT_HOST_IP, sizeof(cfg_host_ip));
    
  cfg_udp_port  = prefs.getUInt("udp_port",  DEFAULT_UDP_PORT);
  cfg_host_port = prefs.getUInt("host_port", DEFAULT_HOST_PORT);
  
  prefs.end();
}

void settings_save_ssid(const char* val) {
  prefs.begin("teletrack", false);
  prefs.putString("ssid", val);
  prefs.end();
  strncpy(cfg_ssid, val, sizeof(cfg_ssid));
}

void settings_save_password(const char* val) {
  prefs.begin("teletrack", false);
  prefs.putString("password", val);
  prefs.end();
  strncpy(cfg_password, val, sizeof(cfg_password));
}

void settings_save_host_ip(const char* val) {
  prefs.begin("teletrack", false);
  prefs.putString("host_ip", val);
  prefs.end();
  strncpy(cfg_host_ip, val, sizeof(cfg_host_ip));
}

void settings_save_port(const char* key, unsigned int val, unsigned int* target) {
  prefs.begin("teletrack", false);
  prefs.putUInt(key, val);
  prefs.end();
  *target = val;
}

void settings_reset() {
  prefs.begin("teletrack", false);
  prefs.clear();
  prefs.end();
  settings_load();
}

#endif
