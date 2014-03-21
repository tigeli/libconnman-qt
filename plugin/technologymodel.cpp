/*
 * Copyright © 2010, Intel Corporation.
 * Copyright © 2012, Jolla.
 *
 * This program is licensed under the terms and conditions of the
 * Apache License, version 2.0.  The full text of the Apache License is at
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 */

#include <QDebug>
#include "technologymodel.h"

#define CONNECT_TECHNOLOGY_SIGNALS(tech) \
    connect(tech, \
        SIGNAL(poweredChanged(bool)), \
        this, \
        SLOT(changedPower(bool))); \
    connect(tech, \
        SIGNAL(connectedChanged(bool)), \
        this, \
        SLOT(changedConnected(bool))); \
    connect(tech, \
            SIGNAL(scanFinished()), \
            this, \
            SLOT(finishedScan()));


#define DISCONNECT_TECHNOLOGY_SIGNALS(tech) \
    disconnect(tech, SIGNAL(poweredChanged(bool)), \
               this, SLOT(changedPower(bool))); \
    disconnect(tech, SIGNAL(scanFinished()), \
               this, SLOT(finishedScan()))


TechnologyModel::TechnologyModel(QAbstractListModel* parent)
  : QAbstractListModel(parent),
    m_manager(NULL),
    m_tech(NULL),
    m_scanning(false),
    m_changesInhibited(false),
    m_uneffectedChanges(false)
{
    m_manager = NetworkManagerFactory::createInstance();

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    setRoleNames(roleNames());
#endif
    connect(m_manager, SIGNAL(availabilityChanged(bool)),
            this, SLOT(managerAvailabilityChanged(bool)));

    connect(m_manager,
            SIGNAL(technologiesChanged()),
            this,
            SLOT(updateTechnologies()));

    connect(m_manager,
            SIGNAL(servicesChanged()),
            this,
            SLOT(updateServiceList()));
}

TechnologyModel::~TechnologyModel()
{
}

QHash<int, QByteArray> TechnologyModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ServiceRole] = "networkService";
    return roles;
}

QVariant TechnologyModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case ServiceRole:
        return QVariant::fromValue(static_cast<QObject *>(m_services.value(index.row())));
    }

    return QVariant();
}

int TechnologyModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    return m_services.count();
}

int TechnologyModel::count() const
{
    return rowCount();
}

const QString TechnologyModel::name() const
{
    return m_techname;
}

bool TechnologyModel::isAvailable() const
{
    return m_manager->isAvailable();
}

bool TechnologyModel::isConnected() const
{
    if (m_tech) {
        return m_tech->connected();
    } else {
        qWarning() << "Can't get: technology is NULL";
        return false;
    }
}

bool TechnologyModel::isPowered() const
{
    if (m_tech) {
        return m_tech->powered();
    } else {
        qWarning() << "Can't get: technology is NULL";
        return false;
    }
}

bool TechnologyModel::isScanning() const
{
    return m_scanning;
}

bool TechnologyModel::changesInhibited() const
{
    return m_changesInhibited;
}

void TechnologyModel::setPowered(const bool &powered)
{
    if (m_tech) {
        m_tech->setPowered(powered);
    } else {
        qWarning() << "Can't set: technology is NULL";
    }
}

void TechnologyModel::refresh()
{
    QStringList netTypes = m_manager->technologiesList();

    bool oldPowered(false);
    bool oldConnected(false);

    bool badName = false;
    if (!netTypes.contains(m_techname)) {
        qDebug() << m_techname <<  "is not a known technology name:" << netTypes;
        badName = true;
    }

    if (m_tech) {
        if (m_scanning) {
            m_scanning = false;
            Q_EMIT scanningChanged(m_scanning);
        }

        oldPowered = m_tech->powered();
        oldConnected = m_tech->connected();
        DISCONNECT_TECHNOLOGY_SIGNALS(m_tech);
        m_tech = 0;
    }
    if (badName)
        return;

    m_tech = m_manager->getTechnology(m_techname);

    if (!m_tech) {
        return;
    } else {
        Q_EMIT nameChanged(m_techname);
        if (oldPowered != m_tech->powered()) {
            Q_EMIT poweredChanged(!oldPowered);
        }
        if (oldConnected != m_tech->connected()) {
            Q_EMIT connectedChanged(!oldConnected);
        }
        CONNECT_TECHNOLOGY_SIGNALS(m_tech);
        updateServiceList();
    }
}

void TechnologyModel::setName(const QString &name)
{
    if (m_techname == name || name.isEmpty()) {
        return;
    }
    m_techname = name;
    if (m_manager->isAvailable()) {
        refresh();
    }
}

void TechnologyModel::setChangesInhibited(bool b)
{
    if (m_changesInhibited != b) {
        m_changesInhibited = b;
        Q_EMIT changesInhibitedChanged(m_changesInhibited);

        if (!m_changesInhibited && m_uneffectedChanges) {
            m_uneffectedChanges = false;
            updateServiceList();
        }
    }
}

void TechnologyModel::requestScan()
{
    if (m_tech) {
        m_tech->scan();
        m_scanning = true;
        Q_EMIT scanningChanged(m_scanning);
    }
}

void TechnologyModel::updateTechnologies()
{
    NetworkTechnology *test = NULL;
    if (m_tech) {
        if ((test = m_manager->getTechnology(m_techname)) == NULL) {
            // if wifi is set and manager doesn't return a wifi, it means
            // that wifi was removed
            DISCONNECT_TECHNOLOGY_SIGNALS(m_tech);
            m_tech = NULL;
            Q_EMIT technologiesChanged();

            if (m_scanning) {
                m_scanning = false;
                Q_EMIT scanningChanged(m_scanning);
            }
        }
    } else {
        if ((test = m_manager->getTechnology(m_techname)) != NULL) {
            // if wifi is not set and manager returns a wifi, it means
            // that wifi was added
            m_tech = test;
            CONNECT_TECHNOLOGY_SIGNALS(m_tech);
            Q_EMIT technologiesChanged();
        }
    }
}

void TechnologyModel::managerAvailabilityChanged(bool available)
{
    Q_EMIT availabilityChanged(available);

    if (!available && m_scanning) {
        m_scanning = false;
        Q_EMIT scanningChanged(m_scanning);
    }
    if (available) {
        refresh();
    }
}

NetworkService *TechnologyModel::get(int index) const
{
    if (index < 0 || index > m_services.count())
        return 0;
    return m_services.value(index);
}

int TechnologyModel::indexOf(const QString &dbusObjectPath) const
{
    int idx(-1);

    Q_FOREACH (NetworkService *service, m_services) {
        idx++;
        if (service->path() == dbusObjectPath) return idx;
    }

    return -1;
}

void TechnologyModel::updateServiceList()
{
    if (m_changesInhibited) {
        m_uneffectedChanges = true;
        return;
    }

    if (m_techname.isEmpty())
        return;

    int num_old = m_services.count();

    foreach(const NetworkService *s, m_services) {
        disconnect(s, SIGNAL(destroyed(QObject*)),
                   this, SLOT(networkServiceDestroyed(QObject*)));
    }

    const QVector<NetworkService *> new_services = m_manager->getServices(m_techname);
    int num_new = new_services.count();

    // Since m_changesInhibited can also inhibit updates
    // about removed/deleted services, connect destroyed.
    foreach(const NetworkService *s, new_services) {
        connect(s, SIGNAL(destroyed(QObject*)),
                this, SLOT(networkServiceDestroyed(QObject*)));
    }

    for (int i = 0; i < num_new; i++) {
        int j = m_services.indexOf(new_services.value(i));
        if (j == -1) {
            // wifi service not found -> remove from list
            beginInsertRows(QModelIndex(), i, i);
            m_services.insert(i, new_services.value(i));
            endInsertRows();
        } else if (i != j) {
            // wifi service changed its position -> move it
            NetworkService* service = m_services.value(j);
            beginMoveRows(QModelIndex(), j, j, QModelIndex(), i);
            m_services.remove(j);
            m_services.insert(i, service);
            endMoveRows();
        }
    }
    // After loop:
    // m_services contains [new_services, old_services \ new_services]

    int num_union = m_services.count();
    if (num_union > num_new) {
        beginRemoveRows(QModelIndex(), num_new, num_union - 1);
        m_services.remove(num_new, num_union - num_new);
        endRemoveRows();
    }

    if (num_new != num_old)
        Q_EMIT countChanged();
}

void TechnologyModel::changedPower(bool b)
{
    NetworkTechnology *tech = qobject_cast<NetworkTechnology *>(sender());
    if (tech->type() != m_tech->type())
        return;

    Q_EMIT poweredChanged(b);

    if (!b && m_scanning) {
        m_scanning = false;
        Q_EMIT scanningChanged(m_scanning);
    }
}

void TechnologyModel::changedConnected(bool b)
{
    NetworkTechnology *tech = qobject_cast<NetworkTechnology *>(sender());
    if (tech->type() == m_tech->type())
        Q_EMIT connectedChanged(b);
}

void TechnologyModel::finishedScan()
{
    NetworkTechnology *tech = qobject_cast<NetworkTechnology *>(sender());
    if (tech->type() != m_tech->type())
        return;

    Q_EMIT scanRequestFinished();

    if (m_scanning) {
        m_scanning = false;
        Q_EMIT scanningChanged(m_scanning);
    }
}

void TechnologyModel::networkServiceDestroyed(QObject *service)
{
    // Should this trigger an assert so that m_services is only changed
    // via updateServiceList ?
    int ind = m_services.indexOf(static_cast<NetworkService*>(service));
    if (ind>=0) {
        qWarning() << "out-of-band removal of network service" << service;
        beginRemoveRows(QModelIndex(), ind, ind);
        m_services.remove(ind);
        endRemoveRows();
    }
}
