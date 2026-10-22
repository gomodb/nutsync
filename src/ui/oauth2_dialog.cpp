#include "ui/oauth2_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef NUTSYNC_HAVE_NETWORKAUTH
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#endif

namespace NutSync {

OAuth2Dialog::OAuth2Dialog(const SyncPair &pair, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("OAuth2 登录"));
    resize(620, 360);
    auto *layout = new QVBoxLayout(this);
    auto *hint = new QLabel(
        tr("使用授权码 + PKCE 登录。令牌只写入此同步对的密码/钥匙串字段，不会写入配置文件。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout();
    m_authorizationUrl = new QLineEdit(pair.oauthAuthorizationUrl, this);
    m_authorizationUrl->setPlaceholderText(QStringLiteral("https://id.example.com/authorize"));
    m_tokenUrl = new QLineEdit(pair.oauthTokenUrl, this);
    m_tokenUrl->setPlaceholderText(QStringLiteral("https://id.example.com/token"));
    m_clientId = new QLineEdit(pair.oauthClientId, this);
    m_scope = new QLineEdit(pair.oauthScope, this);
    m_scope->setPlaceholderText(QStringLiteral("openid webdav"));
    form->addRow(tr("授权地址"), m_authorizationUrl);
    form->addRow(tr("令牌地址"), m_tokenUrl);
    form->addRow(tr("客户端 ID"), m_clientId);
    form->addRow(tr("Scope"), m_scope);
    layout->addLayout(form);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *login = new QPushButton(tr("在浏览器中登录"), this);
    buttons->addButton(login, QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(login, &QPushButton::clicked, this, &OAuth2Dialog::startLogin);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

#ifdef NUTSYNC_HAVE_NETWORKAUTH
    m_oauth = new QOAuth2AuthorizationCodeFlow(this);
    m_replyHandler = new QOAuthHttpServerReplyHandler(0, this);
    m_replyHandler->setCallbackHost(QStringLiteral("127.0.0.1"));
    m_oauth->setReplyHandler(m_replyHandler);
    m_oauth->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    connect(m_oauth, &QOAuth2AuthorizationCodeFlow::granted, this, [this]() {
        m_accessToken = m_oauth->token();
        if (m_accessToken.isEmpty()) {
            setStatus(tr("授权服务器没有返回 access token"), true);
            return;
        }
        setStatus(tr("登录成功"), false);
        accept();
    });
    connect(m_oauth, &QOAuth2AuthorizationCodeFlow::requestFailed, this,
            [this](QAbstractOAuth::Error) {
                setStatus(tr("OAuth2 授权失败"), true);
            });
#else
    login->setEnabled(false);
    setStatus(tr("此构建未启用 Qt NetworkAuth，无法执行 OAuth2 登录。"), true);
#endif
}

QString OAuth2Dialog::authorizationUrl() const
{
    return m_authorizationUrl->text().trimmed();
}

QString OAuth2Dialog::tokenUrl() const
{
    return m_tokenUrl->text().trimmed();
}

QString OAuth2Dialog::clientId() const
{
    return m_clientId->text().trimmed();
}

QString OAuth2Dialog::scope() const
{
    return m_scope->text().trimmed();
}

void OAuth2Dialog::startLogin()
{
#ifdef NUTSYNC_HAVE_NETWORKAUTH
    const QUrl authorization(authorizationUrl());
    const QUrl token(tokenUrl());
    if (!authorization.isValid() || authorization.host().isEmpty() || token.host().isEmpty()
        || clientId().isEmpty()) {
        setStatus(tr("请填写有效的授权地址、令牌地址和客户端 ID。"), true);
        return;
    }
    m_oauth->setAuthorizationUrl(authorization);
    m_oauth->setTokenUrl(token);
    m_oauth->setClientIdentifier(clientId());
    if (!scope().isEmpty())
        m_oauth->setScope(scope());
    setStatus(tr("正在打开浏览器，请在浏览器中完成登录…"), false);
    m_oauth->grant();
#endif
}

void OAuth2Dialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #c62828;")
                                  : QStringLiteral("color: #2e7d32;"));
}

} // namespace NutSync
