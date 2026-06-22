#pragma once

#include "octoprint_monitor.h"

/* Load config from CONFIG_PATH into app->ip and app->apikey.
 * Returns true if the file was read and at least ip= was found. */
bool config_load(OctoPrintApp* app);

/* Write current app->ip and app->apikey back to CONFIG_PATH. */
bool config_save(OctoPrintApp* app);
