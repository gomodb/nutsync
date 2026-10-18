#include "core/retry_policy.h"

#include <QList>
#include <QString>

namespace NutSync::RetryPolicy {

bool hasPermanentHttpError(const SyncSummary &summary)
{
    static const QList<int> permanentStatuses = {301, 302, 303, 307, 308, 400, 401,
                                                 403, 404, 405, 409, 412, 423, 507};
    for (const QString &detail : summary.errorDetails) {
        for (int status : permanentStatuses) {
            if (detail.contains(QStringLiteral("HTTP %1").arg(status)))
                return true;
        }
    }
    return false;
}

int backoffSeconds(int baseSeconds, int attempt, int maxSeconds)
{
    if (baseSeconds <= 0 || attempt <= 0)
        return 0;
    const int exponent = qMin(attempt - 1, 5);
    return qMin(maxSeconds, baseSeconds * (1 << exponent));
}

} // namespace NutSync::RetryPolicy
