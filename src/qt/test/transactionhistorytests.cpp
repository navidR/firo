// Copyright (c) 2026 The Firo developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionhistorytests.h"

#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "transactionview.h"
#include "walletmodel.h"
#include "script/standard.h"
#include "validation.h"
#include "wallet/test/wallet_test_fixture.h"
#include "wallet/wallet.h"

#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <array>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace {
CTransactionRef MakeHistoryTransaction(uint32_t number, const CScript& script)
{
    CMutableTransaction tx;
    tx.nLockTime = number;
    tx.vin.emplace_back(COutPoint(uint256S("01"), number));
    tx.vout.emplace_back(COIN, script);
    tx.vout.emplace_back(2 * COIN, script);
    return MakeTransactionRef(tx);
}

CScript PopulateHistory(CWallet& wallet, int count)
{
    LOCK2(cs_main, wallet.cs_wallet);
    CKey key;
    std::array<unsigned char, 32> secret{};
    secret.back() = 1;
    key.Set(secret.begin(), secret.end(), true);
    if (!wallet.AddKeyPubKey(key, key.GetPubKey()))
        throw std::runtime_error("Cannot add history test key");
    const CScript script = GetScriptForDestination(key.GetPubKey().GetID());
    for (int i = 1; i <= count; ++i) {
        if (!wallet.LoadToWallet(CWalletTx(&wallet, MakeHistoryTransaction(i, script))))
            throw std::runtime_error("Cannot load history test transaction");
    }
    return script;
}

QList<TransactionRecord> ReferenceHistory(CWallet& wallet)
{
    LOCK2(cs_main, wallet.cs_wallet);
    QList<TransactionRecord> result;
    for (const auto& entry : wallet.mapWallet) {
        if (TransactionRecord::showTransaction(entry.second))
            result.append(TransactionRecord::decomposeTransaction(&wallet, entry.second));
    }
    return result;
}

void CompareHistory(TransactionTableModel& model, const QList<TransactionRecord>& expected)
{
    QCOMPARE(model.rowCount(QModelIndex()), expected.size());
    for (int row = 0; row < expected.size(); ++row) {
        const auto index = model.index(row, 0);
        QCOMPARE(index.data(TransactionTableModel::TxIDRole).toString(), expected[row].getTxID());
        QCOMPARE(index.data(TransactionTableModel::AmountRole).toLongLong(), expected[row].credit + expected[row].debit);
        QCOMPARE(index.data(TransactionTableModel::TypeRole).toInt(), int(expected[row].type));
        QCOMPARE(index.data(TransactionTableModel::AddressRole).toString(), QString::fromStdString(expected[row].address));
        QCOMPARE(index.data(TransactionTableModel::WatchonlyRole).toBool(), expected[row].involvesWatchAddress);
    }
}

// Keep the test focused on history loading, not periodic wallet balance work.
void StopBalancePolling(WalletModel& model)
{
    for (auto* timer : model.findChildren<QTimer*>(QString(), Qt::FindDirectChildrenOnly))
        timer->stop();
}
}

void TransactionHistoryTests::yieldsAndPreservesRows()
{
    WalletTestingSetup fixture(CBaseChainParams::REGTEST);
    PopulateHistory(*pwalletMain, 250);
    const auto expected = ReferenceHistory(*pwalletMain);
    const std::unique_ptr<const PlatformStyle> style(PlatformStyle::instantiate("other"));
    OptionsModel options;
    WalletModel walletModel(style.get(), pwalletMain, &options);
    StopBalancePolling(walletModel);
    auto* model = walletModel.getTransactionTableModel();
    QSignalSpy loaded(model, &TransactionTableModel::historyLoaded);
    TransactionView view(style.get());
    view.setModel(&walletModel);
    QSignalSpy messages(&view, &TransactionView::message);
    view.exportClicked();
    QCOMPARE(messages.count(), 1);
    QVERIFY(messages.at(0).at(1).toString().contains("loading"));
    QCOMPARE(model->rowCount(QModelIndex()), 0);
    QVERIFY(model->processingQueuedTransactions());

    int rowsAtYield = -1;
    bool queuedYield = false;
    bool allInitialRowsSuppressed = true;
    connect(model, &QAbstractItemModel::rowsInserted, model, [&] {
        allInitialRowsSuppressed &= model->processingQueuedTransactions();
        if (!queuedYield) {
            queuedYield = true;
            QTimer::singleShot(0, model, [&] { rowsAtYield = model->rowCount(QModelIndex()); });
        }
    });
    // A rescan's flag must not turn off startup-notification suppression.
    model->setProcessingQueuedTransactions(false);
    QTRY_VERIFY_WITH_TIMEOUT(!model->processingQueuedTransactions(), 10000);
    QVERIFY(rowsAtYield > 0);
    QVERIFY(rowsAtYield < expected.size());
    QVERIFY(allInitialRowsSuppressed);
    QCOMPARE(loaded.count(), 1);
    CompareHistory(*model, expected);
}

void TransactionHistoryTests::initializesWatchOnlyFlag()
{
    const TransactionRecord empty;
    const TransactionRecord received(uint256S("01"), 123);
    const TransactionRecord sparkSelf(uint256S("02"), 123, TransactionRecord::SpendSparkToSelf, "", -100, 0);
    QVERIFY(!empty.involvesWatchAddress);
    QVERIFY(!received.involvesWatchAddress);
    QVERIFY(!sparkSelf.involvesWatchAddress);
}

void TransactionHistoryTests::retriesContendedLocks_data()
{
    QTest::addColumn<bool>("mainLock");
    QTest::newRow("cs_main") << true;
    QTest::newRow("cs_wallet") << false;
}

void TransactionHistoryTests::retriesContendedLocks()
{
    QFETCH(bool, mainLock);
    WalletTestingSetup fixture(CBaseChainParams::REGTEST);
    PopulateHistory(*pwalletMain, 3);
    const std::unique_ptr<const PlatformStyle> style(PlatformStyle::instantiate("other"));
    OptionsModel options;
    WalletModel walletModel(style.get(), pwalletMain, &options);
    StopBalancePolling(walletModel);
    auto* model = walletModel.getTransactionTableModel();
    auto* timer = model->findChild<QTimer*>();
    QVERIFY(timer);

    std::promise<void> locked, release;
    auto releaseFuture = release.get_future();
    std::atomic<bool> released{false};
    std::thread worker([&] {
        LOCK(mainLock ? cs_main : pwalletMain->cs_wallet);
        locked.set_value();
        // A broken blocking implementation must fail rather than hang the test.
        releaseFuture.wait_for(std::chrono::seconds(2));
        released = true;
    });
    locked.get_future().wait();
    const bool invoked = QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection);
    const bool returnedWhileLocked = !released.load();
    release.set_value();
    worker.join();
    QVERIFY(invoked);
    QVERIFY(returnedWhileLocked);
    QCOMPARE(model->rowCount(QModelIndex()), 0);
    QVERIFY(model->processingQueuedTransactions());
    QTRY_VERIFY_WITH_TIMEOUT(!model->processingQueuedTransactions(), 10000);
    CompareHistory(*model, ReferenceHistory(*pwalletMain));
}

void TransactionHistoryTests::handlesLiveChanges()
{
    WalletTestingSetup fixture(CBaseChainParams::REGTEST);
    const CScript script = PopulateHistory(*pwalletMain, 250);
    const std::unique_ptr<const PlatformStyle> style(PlatformStyle::instantiate("other"));
    OptionsModel options;
    WalletModel walletModel(style.get(), pwalletMain, &options);
    StopBalancePolling(walletModel);
    auto* model = walletModel.getTransactionTableModel();
    auto* timer = model->findChild<QTimer*>();
    QVERIFY(timer);
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QVERIFY(model->rowCount(QModelIndex()) > 0);
    QVERIFY(model->rowCount(QModelIndex()) < 500);

    uint256 first, last;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        first = pwalletMain->mapWallet.begin()->first;
        last = pwalletMain->mapWallet.rbegin()->first;
        // Remove one loaded transaction and one the cursor has not reached.
        pwalletMain->mapWallet.erase(first);
        pwalletMain->NotifyTransactionChanged(pwalletMain, first, CT_DELETED);
        pwalletMain->mapWallet.erase(last);
        pwalletMain->NotifyTransactionChanged(pwalletMain, last, CT_DELETED);

        // Deterministically find additions before and after the entire original
        // key range, regardless of how many records fit in the first batch.
        bool addedBefore = false, addedAfter = false;
        for (uint32_t i = 251; i <= 100000 && !(addedBefore && addedAfter); ++i) {
            const auto tx = MakeHistoryTransaction(i, script);
            const bool before = tx->GetHash() < first && !addedBefore;
            const bool after = last < tx->GetHash() && !addedAfter;
            if (!before && !after)
                continue;
            QVERIFY(pwalletMain->LoadToWallet(CWalletTx(pwalletMain, tx)));
            pwalletMain->NotifyTransactionChanged(pwalletMain, tx->GetHash(), CT_NEW);
            pwalletMain->NotifyTransactionChanged(pwalletMain, tx->GetHash(), CT_UPDATED);
            addedBefore |= before;
            addedAfter |= after;
        }
        QVERIFY(addedBefore && addedAfter);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!model->processingQueuedTransactions(), 10000);
    // Drain notifications even if the last batch finished first.
    QCoreApplication::sendPostedEvents(model, QEvent::MetaCall);
    CompareHistory(*model, ReferenceHistory(*pwalletMain));
}

void TransactionHistoryTests::emptyHistoryAndDestruction()
{
    WalletTestingSetup fixture(CBaseChainParams::REGTEST);
    const std::unique_ptr<const PlatformStyle> style(PlatformStyle::instantiate("other"));
    OptionsModel options;
    {
        WalletModel walletModel(style.get(), pwalletMain, &options);
        StopBalancePolling(walletModel);
        auto* model = walletModel.getTransactionTableModel();
        QTRY_VERIFY_WITH_TIMEOUT(!model->processingQueuedTransactions(), 10000);
        QCOMPARE(model->rowCount(QModelIndex()), 0);
    }
    PopulateHistory(*pwalletMain, 250);
    QPointer<QTimer> pendingTimer;
    {
        WalletModel walletModel(style.get(), pwalletMain, &options);
        pendingTimer = walletModel.getTransactionTableModel()->findChild<QTimer*>();
        QVERIFY(pendingTimer && pendingTimer->isActive());
    }
    QVERIFY(pendingTimer.isNull());
    QTest::qWait(30);
}
