#pragma once

#include "core/sync_types.h"

namespace NutSync::RetryPolicy {

bool hasPermanentHttpError(const SyncSummary &summary);
int backoffSeconds(int baseSeconds, int attempt, int maxSeconds = 600);

} // namespace NutSync::RetryPolicy
