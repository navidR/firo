// Copyright (c) 2018 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evodb.h"

CEvoDB* evoDb;
static const std::string EVODB_POPBLOCKS_RECOVERY =
    "popblocks_recovery";
static const std::string EVODB_POPBLOCKS_MEMPOOL_CLEANUP =
    "popblocks_mempool_cleanup";

namespace
{

static const uint8_t POPBLOCKS_RECOVERY_VERSION = 1;

struct PopBlocksRecoveryMarker
{
    uint8_t version{POPBLOCKS_RECOVERY_VERSION};
    bool forgetData{false};
    uint256 initialTip;
    uint256 targetTip;
    std::vector<unsigned char> cleanupData;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(version);
        READWRITE(forgetData);
        READWRITE(initialTip);
        READWRITE(targetTip);
        READWRITE(cleanupData);
    }
};

} // namespace

CEvoDB::CEvoDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    db(fMemory ? "" : (GetDataDir() / "evodb"), nCacheSize, fMemory, fWipe),
    rootBatch(db),
    rootDBTransaction(db, rootBatch),
    curDBTransaction(rootDBTransaction, rootDBTransaction)
{
}

bool CEvoDB::CommitRootTransaction()
{
    LOCK(cs);
    assert(curDBTransaction.IsClean());
    rootDBTransaction.Commit();
    bool ret = db.WriteBatch(rootBatch);
    rootBatch.Clear();
    return ret;
}

bool CEvoDB::ReadBestBlock(uint256& hash)
{
    return Read(EVODB_BEST_BLOCK, hash);
}

bool CEvoDB::VerifyBestBlock(const uint256& hash)
{
    // Make sure evodb is consistent.
    // If we already have best block hash saved, the previous block should match it.
    uint256 hashBestBlock;
    bool fHasBestBlock = ReadBestBlock(hashBestBlock);
    uint256 hashBlockIndex = fHasBestBlock ? hash : uint256();
    assert(hashBestBlock == hashBlockIndex);

    return fHasBestBlock;
}

void CEvoDB::WriteBestBlock(const uint256& hash)
{
    Write(EVODB_BEST_BLOCK, hash);
}

bool CEvoDB::WritePopBlocksRecovery(
    const uint256& initialTip,
    const uint256& targetTip,
    bool forgetData,
    const std::vector<unsigned char>& cleanupData)
{
    LOCK(cs);
    PopBlocksRecoveryMarker marker;
    marker.forgetData = forgetData;
    marker.initialTip = initialTip;
    marker.targetTip = targetTip;
    marker.cleanupData = cleanupData;
    return db.Write(
        EVODB_POPBLOCKS_RECOVERY,
        marker,
        true);
}

bool CEvoDB::ReadPopBlocksRecovery(
    uint256& initialTip,
    uint256& targetTip,
    bool& forgetData,
    std::vector<unsigned char>& cleanupData,
    bool& legacy)
{
    LOCK(cs);
    PopBlocksRecoveryMarker marker;
    if (db.Read(EVODB_POPBLOCKS_RECOVERY, marker)) {
        if (marker.version != POPBLOCKS_RECOVERY_VERSION)
            return false;
        initialTip = marker.initialTip;
        targetTip = marker.targetTip;
        forgetData = marker.forgetData;
        cleanupData = std::move(marker.cleanupData);
        legacy = false;
        return true;
    }

    std::pair<uint256, uint256> tips;
    if (db.Read(EVODB_POPBLOCKS_RECOVERY, tips)) {
        initialTip = tips.first;
        targetTip = tips.second;
        forgetData = false;
        cleanupData.clear();
        legacy = true;
        return true;
    }

    // Recognize an interrupted marker written by an earlier development
    // build. Its missing target makes automatic recovery unsafe.
    targetTip.SetNull();
    forgetData = false;
    cleanupData.clear();
    legacy = true;
    return db.Read(EVODB_POPBLOCKS_RECOVERY, initialTip);
}

bool CEvoDB::HasPopBlocksRecovery()
{
    LOCK(cs);
    return db.Exists(EVODB_POPBLOCKS_RECOVERY);
}

bool CEvoDB::ErasePopBlocksRecovery()
{
    LOCK(cs);
    return db.Erase(EVODB_POPBLOCKS_RECOVERY, true);
}

bool CEvoDB::WritePopBlocksMempoolCleanup()
{
    LOCK(cs);
    return db.Write(
        EVODB_POPBLOCKS_MEMPOOL_CLEANUP,
        true,
        true);
}

bool CEvoDB::HasPopBlocksMempoolCleanup()
{
    LOCK(cs);
    return db.Exists(EVODB_POPBLOCKS_MEMPOOL_CLEANUP);
}

bool CEvoDB::ErasePopBlocksMempoolCleanup()
{
    LOCK(cs);
    return db.Erase(
        EVODB_POPBLOCKS_MEMPOOL_CLEANUP,
        true);
}

void CEvoDB::CommitTransaction(CurTransaction & tx) {
    LOCK(cs);
    tx.Commit();
}

void CEvoDB::ClearTransaction(CurTransaction & tx) {
    LOCK(cs);
    tx.Clear();
}
