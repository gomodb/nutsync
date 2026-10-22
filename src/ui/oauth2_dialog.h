#pragma once

#include "core/sync_types.h"

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;

#ifdef NUTSYNC_HAVE_NETWORKAUTH
class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;
#endif

namespace NutSync {

class OAuth2Dialog : public QDialog
{
    Q_OBJECT

public:
    explicit OAuth2Dialog(const SyncPair &pair, QWidget *parent = nullptr);

    QString accessToken() const { return m_accessToken; }
    QString authorizationUrl() const;
    QString tokenUrl() const;
    QString clientId() const;
    QString scope() const;

private:
    void startLogin();
    void setStatus(const QString &text, bool error);

    QLineEdit *m_authorizationUrl = nullptr;
    QLineEdit *m_tokenUrl = nullptr;
    QLineEdit *m_clientId = nullptr;
    QLineEdit *m_scope = nullptr;
    QLabel *m_status = nullptr;
    QString m_accessToken;
#ifdef NUTSYNC_HAVE_NETWORKAUTH
    QOAuth2AuthorizationCodeFlow *m_oauth = nullptr;
    QOAuthHttpServerReplyHandler *m_replyHandler = nullptr;
#endif
};

} // namespace NutSync
