// Copyright (c) 2025 The Firo Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_QTCOMPAT_H
#define BITCOIN_QT_QTCOMPAT_H

#include <QDateTime>
#include <QtGlobal>

/**
 * Qt compatibility layer for handling differences between Qt 5 and Qt 6
 */

// QDateTime compatibility
#if QT_VERSION >= 0x060000
#define QT_DATETIME_FROM_TIME_T(timestamp) QDateTime::fromSecsSinceEpoch(timestamp)
#define QT_DATETIME_TO_TIME_T(datetime) (datetime).toSecsSinceEpoch()
#else
#define QT_DATETIME_FROM_TIME_T(timestamp) QDateTime::fromTime_t(timestamp)
#define QT_DATETIME_TO_TIME_T(datetime) (datetime).toTime_t()
#endif

#endif // BITCOIN_QT_QTCOMPAT_H
