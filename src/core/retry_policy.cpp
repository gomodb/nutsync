#include "core/retry_policy.h"

#include <QList>
#include <QString>

namespace NutSync::RetryPolicy {

bool isRetryable(RemoteErrorClass errorClass)
{
    switch (errorClass) {
    case RemoteErrorClass::Network:
    case RemoteErrorClass::Timeout:
    case RemoteErrorClass::RateLimited:
    case RemoteErrorClass::Server:
        return true;
    default:
        return false;
    }
}

} // namespace NutSync::RetryPolicy
