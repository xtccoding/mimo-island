#include "FetchThread.h"
#include <QNetworkRequest>
#include <QJsonArray>

FetchWorker::FetchWorker(ProfileManager* pm, QObject* parent)
    : QObject(parent)
    , m_pm(pm)
    , m_manager(new QNetworkAccessManager(this))
{
}

void FetchWorker::fetch()
{
    CookieProfile* profile = m_pm->getActive();
    
    if (!profile) {
        FetchResult result;
        result.ok = false;
        result.msg = "请添加Cookie";
        emit dataFetched(result);
        return;
    }

    QNetworkRequest request(QUrl("https://platform.xiaomimimo.com/api/v1/tokenPlan/usage"));
    request.setRawHeader("accept", "*/*");
    request.setRawHeader("accept-language", "zh");
    request.setRawHeader("content-type", "application/json");
    request.setRawHeader("x-timezone", "Asia/Shanghai");
    request.setRawHeader("user-agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36");
    request.setRawHeader("referer", "https://platform.xiaomimimo.com/console/plan-manage");
    request.setRawHeader("cookie", profile->cookieStr.toUtf8());

    QNetworkReply* reply = m_manager->get(request);
    
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        FetchResult result;

        if (reply->error() != QNetworkReply::NoError) {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString errorString = reply->errorString();
            
            if (statusCode == 401) {
                m_pm->markError("401");
                result.ok = false;
                result.msg = "Cookie已过期";
                result.code = 401;
            } else if (statusCode > 0) {
                m_pm->markError(QString::number(statusCode));
                result.ok = false;
                result.msg = QString("HTTP %1").arg(statusCode);
            } else {
                // 网络错误（无HTTP状态码）
                m_pm->markError("NET");
                result.ok = false;
                result.msg = QString("网络错误: %1").arg(errorString.left(30));
            }
            
            reply->deleteLater();
            emit dataFetched(result);
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isNull()) {
            m_pm->markError("JSON解析失败");
            result.ok = false;
            result.msg = "JSON解析失败";
            emit dataFetched(result);
            return;
        }

        QJsonObject root = doc.object();
        QJsonObject dataObj = root["data"].toObject();

        // 获取月度总用量
        QJsonObject monthUsage = dataObj["monthUsage"].toObject();
        QJsonArray monthItems = monthUsage["items"].toArray();
        
        for (const auto& item : monthItems) {
            QJsonObject obj = item.toObject();
            if (obj["name"].toString() == "month_total_token") {
                result.monthUsed = obj["used"].toDouble();
                result.monthTotal = obj["limit"].toDouble();
                result.monthPercent = obj["percent"].toDouble() * 100;
                break;
            }
        }

        // 获取套餐和补偿用量
        QJsonObject usage = dataObj["usage"].toObject();
        QJsonArray items = usage["items"].toArray();
        
        for (const auto& item : items) {
            QJsonObject obj = item.toObject();
            QString name = obj["name"].toString();
            
            if (name == "plan_total_token") {
                result.planUsed = obj["used"].toDouble();
                result.planTotal = obj["limit"].toDouble();
                result.planPercent = obj["percent"].toDouble() * 100;
            } else if (name == "compensation_total_token") {
                result.compUsed = obj["used"].toDouble();
                result.compTotal = obj["limit"].toDouble();
                result.compPercent = obj["percent"].toDouble() * 100;
            }
        }

        m_pm->markSuccess();
        result.ok = true;
        emit dataFetched(result);
    });
}
