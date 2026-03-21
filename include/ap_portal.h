#pragma once

#include "config_manager.h"
#include <IPAddress.h>

// Starts the AP, DNS server, and web server.
// Blocks until the user submits the configuration form.
// Saves the new config and restarts the device.
void runAPPortal(const AppConfig &currentCfg);

