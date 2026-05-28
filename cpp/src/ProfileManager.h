#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

struct CookieProfile {
    QString name;
    QString cookieStr;
    QString createdAt;
    QString lastUsed;
    QString lastError;

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["cookie_str"] = cookieStr;
        obj["created_at"] = createdAt;
        obj["last_used"] = lastUsed;
        obj["last_error"] = lastError;
        return obj;
    }

    static CookieProfile fromJson(const QJsonObject& obj) {
        CookieProfile p;
        p.name = obj["name"].toString();
        p.cookieStr = obj["cookie_str"].toString();
        p.createdAt = obj["created_at"].toString();
        p.lastUsed = obj["last_used"].toString();
        p.lastError = obj["last_error"].toString();
        return p;
    }
};

class ProfileManager : public QObject {
    Q_OBJECT

public:
    explicit ProfileManager(QObject* parent = nullptr);

    void load();
    void save();
    
    CookieProfile* getActive();
    int getActiveIndex() const { return m_activeIndex; }
    const QVector<CookieProfile>& getProfiles() const { return m_profiles; }
    
    void add(const QString& name, const QString& cookieStr);
    void remove(int index);
    void rename(int index, const QString& newName);
    void setActive(int index);
    
    void markError(const QString& errorMsg);
    void markSuccess();

private:
    QString getConfigPath() const;
    
    QVector<CookieProfile> m_profiles;
    int m_activeIndex = 0;
};
