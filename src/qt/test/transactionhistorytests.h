// Copyright (c) 2026 The Firo developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FIRO_QT_TEST_TRANSACTIONHISTORYTESTS_H
#define FIRO_QT_TEST_TRANSACTIONHISTORYTESTS_H

#include <QObject>

class TransactionHistoryTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initializesWatchOnlyFlag();
    void yieldsAndPreservesRows();
    void retriesContendedLocks_data();
    void retriesContendedLocks();
    void handlesLiveChanges();
    void emptyHistoryAndDestruction();
};

#endif
