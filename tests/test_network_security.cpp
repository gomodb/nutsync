#include "test_network_security.h"

#include "core/network_utils.h"
#include "core/secured_network_access_manager.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>

namespace NutSync::Test {

void TestNetworkSecurity::classifiesLoopbackHosts()
{
    QVERIFY(NetworkUtils::isLoopbackHost(QStringLiteral("localhost")));
    QVERIFY(NetworkUtils::isLoopbackHost(QStringLiteral("127.0.0.1")));
    QVERIFY(NetworkUtils::isLoopbackHost(QStringLiteral("::1")));
    QVERIFY(!NetworkUtils::isLoopbackHost(QStringLiteral("example.com")));
    QVERIFY(
        NetworkUtils::allowsCleartextCredentials(QUrl(QStringLiteral("http://127.0.0.1/dav/"))));
    QVERIFY(
        NetworkUtils::allowsCleartextCredentials(QUrl(QStringLiteral("https://example.com/dav/"))));
    QVERIFY(
        !NetworkUtils::allowsCleartextCredentials(QUrl(QStringLiteral("http://example.com/dav/"))));
}

void TestNetworkSecurity::doesNotFollowCrossOriginRedirects()
{
    QTcpServer target;
    QVERIFY(target.listen(QHostAddress::LocalHost, 0));
    QByteArray capturedAuthorization;
    bool targetHit = false;
    connect(&target, &QTcpServer::newConnection, &target, [&]() {
        while (QTcpSocket *socket = target.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                QByteArray data = socket->property("request").toByteArray();
                data += socket->readAll();
                socket->setProperty("request", data);
                if (!data.contains("\r\n\r\n"))
                    return;

                targetHit = true;
                for (const QByteArray &line : data.split('\n')) {
                    if (line.toLower().startsWith("authorization:")) {
                        capturedAuthorization = line.mid(14).trimmed();
                    }
                }
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n"
                              "Connection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
        }
    });

    QTcpServer redirect;
    QVERIFY(redirect.listen(QHostAddress::LocalHost, 0));
    const QString redirectUrl =
        QStringLiteral("http://127.0.0.1:%1/dav/").arg(redirect.serverPort());
    const QString targetUrl = QStringLiteral("http://127.0.0.1:%1/dav/").arg(target.serverPort());
    connect(&redirect, &QTcpServer::newConnection, &redirect, [&]() {
        while (QTcpSocket *socket = redirect.nextPendingConnection()) {
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                QByteArray data = socket->property("request").toByteArray();
                data += socket->readAll();
                socket->setProperty("request", data);
                if (!data.contains("\r\n\r\n") || socket->property("responded").toBool())
                    return;
                socket->setProperty("responded", true);
                const QByteArray response = "HTTP/1.1 302 Found\r\nLocation: " + targetUrl.toUtf8()
                    + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                socket->write(response);
                socket->disconnectFromHost();
            });
        }
    });

    SecuredNetworkAccessManager manager{QUrl(redirectUrl)};
    QNetworkRequest request(QUrl(redirectUrl + QStringLiteral("sub/")));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setRawHeader("Authorization", "Basic dGVzdDp0ZXN0");
    QNetworkReply *reply = manager.get(request);
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    QVERIFY(!targetHit);
    QCOMPARE(capturedAuthorization, QByteArray());
    reply->deleteLater();
}

} // namespace NutSync::Test
