#include "ProfileManager.h"
#include <QDateTime>

ProfileManager::ProfileManager(QObject* parent)
    : QObject(parent)
{
    load();
}

QString ProfileManager::getConfigPath() const
{
    return QDir::homePath() + "/.mimo_monitor/profiles.json";
}

void ProfileManager::load()
{
    QString path = getConfigPath();
    QFile file(path);
    
    if (!file.exists()) {
        m_profiles.clear();
        m_activeIndex = 0;
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        m_profiles.clear();
        m_activeIndex = 0;
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull()) {
        m_profiles.clear();
        m_activeIndex = 0;
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray profilesArray = root["profiles"].toArray();

    m_profiles.clear();
    for (const auto& val : profilesArray) {
        m_profiles.append(CookieProfile::fromJson(val.toObject()));
    }
    m_activeIndex = root["active_index"].toInt(0);
}

void ProfileManager::save()
{
    QString path = getConfigPath();
    QDir dir = QFileInfo(path).absolutePath();
    
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QJsonObject root;
    QJsonArray profilesArray;
    
    for (const auto& profile : m_profiles) {
        profilesArray.append(profile.toJson());
    }
    
    root["profiles"] = profilesArray;
    root["active_index"] = m_activeIndex;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }
}

CookieProfile* ProfileManager::getActive()
{
    if (m_profiles.isEmpty() || m_activeIndex < 0 || m_activeIndex >= m_profiles.size()) {
        return nullptr;
    }
    return &m_profiles[m_activeIndex];
}

void ProfileManager::add(const QString& name, const QString& cookieStr)
{
    CookieProfile profile;
    profile.name = name;
    profile.cookieStr = cookieStr;
    profile.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    m_profiles.append(profile);
    m_activeIndex = m_profiles.size() - 1;
    save();
}

void ProfileManager::remove(int index)
{
    if (index < 0 || index >= m_profiles.size()) {
        return;
    }

    m_profiles.removeAt(index);
    
    if (m_activeIndex >= m_profiles.size()) {
        m_activeIndex = qMax(0, m_profiles.size() - 1);
    }
    save();
}

void ProfileManager::setActive(int index)
{
    if (index >= 0 && index < m_profiles.size()) {
        m_activeIndex = index;
        save();
    }
}

void ProfileManager::markError(const QString& errorMsg)
{
    CookieProfile* profile = getActive();
    if (profile) {
        profile->lastError = errorMsg;
        save();
    }
}

void ProfileManager::markSuccess()
{
    CookieProfile* profile = getActive();
    if (profile) {
        profile->lastUsed = QDateTime::currentDateTime().toString(Qt::ISODate);
        profile->lastError.clear();
        save();
    }
}
