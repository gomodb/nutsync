#pragma once

#include "core/sync_types.h"

namespace NutSync::RetryPolicy {

bool isRetryable(RemoteErrorClass errorClass);

} // namespace NutSync::RetryPolicy
