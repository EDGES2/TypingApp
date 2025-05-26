#ifndef STATS_HANDLER_H
#define STATS_HANDLER_H

#include "app_context.h"

void CalculateAndPrintAppStats(AppContext *appCtx,
                               const char* actual_stats_f_path); // Path to the statistics file is needed

#endif // STATS_HANDLER_H