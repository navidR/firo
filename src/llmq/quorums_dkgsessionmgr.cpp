// Copyright (c) 2018-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "quorums_dkgsessionmgr.h"
#include "quorums_blockprocessor.h"
#include "quorums_debug.h"
#include "quorums_init.h"
#include "quorums_utils.h"

#include "chainparams.h"
#include "net_processing.h"
#include "validation.h"

namespace llmq
{

CDKGSessionManager* quorumDKGSessionManager;

static const std::string DB_VVEC = "qdkg_V";
static const std::string DB_SKCONTRIB = "qdkg_S";

namespace
{

// Bound well-formed messages from quorum parameters before deserialization or retention.
size_t MaxDKGMessageSize(const std::string& strCommand, const Consensus::LLMQParams& params)
{
    constexpr size_t COMPACT_SIZE = 5;
    constexpr size_t PREFIX_SIZE = 1 + 32 + 32;
    constexpr size_t PUBLIC_KEY_SIZE = BLS_CURVE_PUBKEY_SIZE;
    constexpr size_t SIGNATURE_SIZE = BLS_CURVE_SIG_SIZE;
    constexpr size_t SECRET_KEY_SIZE = BLS_CURVE_SECKEY_SIZE;
    constexpr size_t ENCRYPTED_BLOB_SIZE = COMPACT_SIZE + 128;
    constexpr size_t SLACK_SIZE = 1024;
    constexpr size_t HARD_CEILING = size_t{1} << 20;

    const size_t quorumSize = params.size > 0 ? static_cast<size_t>(params.size) : 0;
    const size_t threshold = params.threshold > 0 ? static_cast<size_t>(params.threshold) : 0;

    size_t cap;
    if (strCommand == NetMsgType::QCONTRIB) {
        cap = PREFIX_SIZE + COMPACT_SIZE + threshold * PUBLIC_KEY_SIZE + PUBLIC_KEY_SIZE + 32 + COMPACT_SIZE + quorumSize * ENCRYPTED_BLOB_SIZE + SIGNATURE_SIZE;
    } else if (strCommand == NetMsgType::QJUSTIFICATION) {
        cap = PREFIX_SIZE + COMPACT_SIZE + quorumSize * (4 + SECRET_KEY_SIZE) + SIGNATURE_SIZE;
    } else if (strCommand == NetMsgType::QCOMPLAINT) {
        cap = PREFIX_SIZE + 2 * (COMPACT_SIZE + (quorumSize + 7) / 8) + SIGNATURE_SIZE;
    } else if (strCommand == NetMsgType::QPCOMMITMENT) {
        cap = PREFIX_SIZE + COMPACT_SIZE + (quorumSize + 7) / 8 + PUBLIC_KEY_SIZE + 32 + 2 * SIGNATURE_SIZE;
    } else {
        return HARD_CEILING;
    }

    cap += SLACK_SIZE;
    return cap < HARD_CEILING ? cap : HARD_CEILING;
}

// Deserialize a copy and check only parameter-derived shape; signature verification remains on the DKG worker.
bool CheckDKGMessageStructure(const std::string& strCommand, const CDataStream& vRecv, const Consensus::LLMQParams& params, uint256& quorumHashRet)
{
    const size_t quorumSize = params.size > 0 ? static_cast<size_t>(params.size) : 0;
    const size_t minQuorumSize = params.minSize > 0 ? static_cast<size_t>(params.minSize) : 0;
    const size_t threshold = params.threshold > 0 ? static_cast<size_t>(params.threshold) : 0;

    try {
        CDataStream copy(vRecv.begin(), vRecv.end(), vRecv.GetType(), vRecv.GetVersion());
        if (strCommand == NetMsgType::QCONTRIB) {
            CDKGContribution contribution;
            copy >> contribution;
            quorumHashRet = contribution.quorumHash;
            return contribution.vvec && contribution.vvec->size() == threshold && contribution.contributions && contribution.contributions->blobs.size() >= minQuorumSize && contribution.contributions->blobs.size() <= quorumSize;
        }
        if (strCommand == NetMsgType::QCOMPLAINT) {
            CDKGComplaint complaint;
            copy >> complaint;
            quorumHashRet = complaint.quorumHash;
            return complaint.badMembers.size() == quorumSize && complaint.complainForMembers.size() == quorumSize;
        }
        if (strCommand == NetMsgType::QJUSTIFICATION) {
            CDKGJustification justification;
            copy >> justification;
            quorumHashRet = justification.quorumHash;
            return justification.contributions.size() <= quorumSize;
        }
        if (strCommand == NetMsgType::QPCOMMITMENT) {
            CDKGPrematureCommitment commitment;
            copy >> commitment;
            quorumHashRet = commitment.quorumHash;
            return commitment.validMembers.size() == quorumSize;
        }
    } catch (const std::exception&) {
    }

    return false;
}

} // namespace

CDKGSessionManager::CDKGSessionManager(CDBWrapper& _llmqDb, CBLSWorker& _blsWorker) :
    llmqDb(_llmqDb),
    blsWorker(_blsWorker)
{
}

CDKGSessionManager::~CDKGSessionManager()
{
}

void CDKGSessionManager::StartMessageHandlerPool()
{
    for (const auto& qt : Params().GetConsensus().llmqs) {
        dkgSessionHandlers.emplace(std::piecewise_construct,
                std::forward_as_tuple(qt.first),
                std::forward_as_tuple(qt.second, messageHandlerPool, blsWorker, *this));
    }

    messageHandlerPool.resize(2);
    RenameThreadPool(messageHandlerPool, "firo-q-msg");
}

void CDKGSessionManager::StopMessageHandlerPool()
{
    messageHandlerPool.stop(true);
}

void CDKGSessionManager::UpdatedBlockTip(const CBlockIndex* pindexNew, bool fInitialDownload)
{
    FIRO_UNUSED const auto& consensus = Params().GetConsensus();

    CleanupCache();

    if (fInitialDownload)
        return;
    if (!deterministicMNManager->IsDIP3Enforced(pindexNew->nHeight))
        return;

    for (auto& qt : dkgSessionHandlers) {
        qt.second.UpdatedBlockTip(pindexNew);
    }
}

void CDKGSessionManager::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (!deterministicMNManager->IsDIP3Enforced())
        return;

    if (strCommand != NetMsgType::QCONTRIB
        && strCommand != NetMsgType::QCOMPLAINT
        && strCommand != NetMsgType::QJUSTIFICATION
        && strCommand != NetMsgType::QPCOMMITMENT
        && strCommand != NetMsgType::QWATCH) {
        return;
    }

    if (strCommand == NetMsgType::QWATCH) {
        pfrom->qwatch = true;
        return;
    }

    uint256 senderProTxHash;
    {
        LOCK(pfrom->cs_mnauth);
        senderProTxHash = pfrom->verifiedProRegTxHash;
    }
    if (senderProTxHash.IsNull()) {
        LOCK(cs_main);
        Misbehaving(pfrom->id, 10);
        return;
    }

    if (vRecv.size() < 1) {
        LOCK(cs_main);
        Misbehaving(pfrom->id, 100);
        return;
    }

    // peek into the message and see which LLMQType it is. First byte of all messages is always the LLMQType
    Consensus::LLMQType llmqType = (Consensus::LLMQType)*vRecv.begin();
    if (!dkgSessionHandlers.count(llmqType)) {
        LOCK(cs_main);
        Misbehaving(pfrom->id, 100);
        return;
    }

    const auto& params = Params().GetConsensus().llmqs.at(llmqType);
    if (vRecv.size() > MaxDKGMessageSize(strCommand, params)) {
        LOCK(cs_main);
        Misbehaving(pfrom->id, 100);
        return;
    }

    uint256 messageQuorumHash;
    if (!CheckDKGMessageStructure(strCommand, vRecv, params, messageQuorumHash)) {
        LOCK(cs_main);
        Misbehaving(pfrom->id, 100);
        return;
    }

    dkgSessionHandlers.at(llmqType).ProcessMessage(
        pfrom, senderProTxHash, messageQuorumHash, strCommand, vRecv, connman);
}

bool CDKGSessionManager::AlreadyHave(const CInv& inv) const
{
    for (const auto& p : dkgSessionHandlers) {
        auto& dkgType = p.second;
        if (dkgType.pendingContributions.HasSeen(inv.hash)
            || dkgType.pendingComplaints.HasSeen(inv.hash)
            || dkgType.pendingJustifications.HasSeen(inv.hash)
            || dkgType.pendingPrematureCommitments.HasSeen(inv.hash)) {
            return true;
        }
    }
    return false;
}

bool CDKGSessionManager::GetContribution(const uint256& hash, CDKGContribution& ret) const
{
    for (const auto& p : dkgSessionHandlers) {
        auto& dkgType = p.second;
        LOCK2(dkgType.cs, dkgType.curSession->invCs);
        if (dkgType.phase < QuorumPhase_Initialized || dkgType.phase > QuorumPhase_Contribute) {
            continue;
        }
        auto it = dkgType.curSession->contributions.find(hash);
        if (it != dkgType.curSession->contributions.end()) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

bool CDKGSessionManager::GetComplaint(const uint256& hash, CDKGComplaint& ret) const
{
    for (const auto& p : dkgSessionHandlers) {
        auto& dkgType = p.second;
        LOCK2(dkgType.cs, dkgType.curSession->invCs);
        if (dkgType.phase < QuorumPhase_Contribute || dkgType.phase > QuorumPhase_Complain) {
            continue;
        }
        auto it = dkgType.curSession->complaints.find(hash);
        if (it != dkgType.curSession->complaints.end()) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

bool CDKGSessionManager::GetJustification(const uint256& hash, CDKGJustification& ret) const
{
    for (const auto& p : dkgSessionHandlers) {
        auto& dkgType = p.second;
        LOCK2(dkgType.cs, dkgType.curSession->invCs);
        if (dkgType.phase < QuorumPhase_Complain || dkgType.phase > QuorumPhase_Justify) {
            continue;
        }
        auto it = dkgType.curSession->justifications.find(hash);
        if (it != dkgType.curSession->justifications.end()) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

bool CDKGSessionManager::GetPrematureCommitment(const uint256& hash, CDKGPrematureCommitment& ret) const
{
    for (const auto& p : dkgSessionHandlers) {
        auto& dkgType = p.second;
        LOCK2(dkgType.cs, dkgType.curSession->invCs);
        if (dkgType.phase < QuorumPhase_Justify || dkgType.phase > QuorumPhase_Commit) {
            continue;
        }
        auto it = dkgType.curSession->prematureCommitments.find(hash);
        if (it != dkgType.curSession->prematureCommitments.end() && dkgType.curSession->validCommitments.count(hash)) {
            ret = it->second;
            return true;
        }
    }
    return false;
}

void CDKGSessionManager::WriteVerifiedVvecContribution(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, const BLSVerificationVectorPtr& vvec)
{
    llmqDb.Write(std::make_tuple(DB_VVEC, (uint8_t) llmqType, pindexQuorum->GetBlockHash(), proTxHash), *vvec);
}

void CDKGSessionManager::WriteVerifiedSkContribution(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, const CBLSSecretKey& skContribution)
{
    llmqDb.Write(std::make_tuple(DB_SKCONTRIB, (uint8_t) llmqType, pindexQuorum->GetBlockHash(), proTxHash), skContribution);
}

bool CDKGSessionManager::GetVerifiedContributions(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const std::vector<bool>& validMembers, std::vector<uint16_t>& memberIndexesRet, std::vector<BLSVerificationVectorPtr>& vvecsRet, BLSSecretKeyVector& skContributionsRet)
{
    auto members = CLLMQUtils::GetAllQuorumMembers(llmqType, pindexQuorum);

    memberIndexesRet.clear();
    vvecsRet.clear();
    skContributionsRet.clear();
    memberIndexesRet.reserve(members.size());
    vvecsRet.reserve(members.size());
    skContributionsRet.reserve(members.size());
    for (size_t i = 0; i < members.size(); i++) {
        if (validMembers[i]) {
            BLSVerificationVectorPtr vvec;
            CBLSSecretKey skContribution;
            if (!GetVerifiedContribution(llmqType, pindexQuorum, members[i]->proTxHash, vvec, skContribution)) {
                return false;
            }

            memberIndexesRet.emplace_back(i);
            vvecsRet.emplace_back(vvec);
            skContributionsRet.emplace_back(skContribution);
        }
    }
    return true;
}

bool CDKGSessionManager::GetVerifiedContribution(Consensus::LLMQType llmqType, const CBlockIndex* pindexQuorum, const uint256& proTxHash, BLSVerificationVectorPtr& vvecRet, CBLSSecretKey& skContributionRet)
{
    LOCK(contributionsCacheCs);
    ContributionsCacheKey cacheKey = {llmqType, pindexQuorum->GetBlockHash(), proTxHash};
    auto it = contributionsCache.find(cacheKey);
    if (it != contributionsCache.end()) {
        vvecRet = it->second.vvec;
        skContributionRet = it->second.skContribution;
        return true;
    }

    BLSVerificationVector vvec;
    BLSVerificationVectorPtr vvecPtr;
    CBLSSecretKey skContribution;
    if (llmqDb.Read(std::make_tuple(DB_VVEC, (uint8_t) llmqType, pindexQuorum->GetBlockHash(), proTxHash), vvec)) {
        vvecPtr = std::make_shared<BLSVerificationVector>(std::move(vvec));
    }
    llmqDb.Read(std::make_tuple(DB_SKCONTRIB, (uint8_t) llmqType, pindexQuorum->GetBlockHash(), proTxHash), skContribution);

    it = contributionsCache.emplace(cacheKey, ContributionsCacheEntry{GetTimeMillis(), vvecPtr, skContribution}).first;

    vvecRet = it->second.vvec;
    skContributionRet = it->second.skContribution;

    return true;
}

void CDKGSessionManager::CleanupCache()
{
    LOCK(contributionsCacheCs);
    auto curTime = GetTimeMillis();
    for (auto it = contributionsCache.begin(); it != contributionsCache.end(); ) {
        if (curTime - it->second.entryTime > MAX_CONTRIBUTION_CACHE_TIME) {
            it = contributionsCache.erase(it);
        } else {
            ++it;
        }
    }
}

}
