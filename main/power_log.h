#pragma once
struct State;struct cJSON;
void power_log_init();
void power_log_tick(const State&,bool may_write);
cJSON* power_log_info();
cJSON* power_log_read(unsigned after,unsigned limit);
