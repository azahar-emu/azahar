#pragma once

#include <rc_client.h>

extern rc_client_t* g_client;

void initialize_retroachievements_client();
void shutdown_retroachievements_client();
