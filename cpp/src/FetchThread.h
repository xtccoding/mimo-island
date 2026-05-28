#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonObject>
#include <QJsonDocument>
#include "ProfileManager.h"

struct FetchResult {
    bool ok = false;
    QString msg;
    int code = 0;
    
    double planUsed = 0;
    double planTotal = 0;
    double planPercent = 0;
    
    double compUsed = 0;
    double compTotal = 0;
    double compPercent = 0;
    
    double monthUsed = 0;
    double monthTotal = 0;
    double monthPercent = 0;
};

class FetchWorker : public QObject {
    Q_OBJECT

public:
    explicit FetchWorker(ProfileManager* pm, QObject* parent = nullptr);

public slots:
    void fetch();

signals:
    void dataFetched(const FetchResult& result);

private:
    ProfileManager* m_pm;
    QNetworkAccessManager* m_manager;
};
