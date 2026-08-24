// Copyright (c) 2026 The Firo developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "evo/specialtx.h"
#include "hash.h"
#include "llmq/quorums_blockprocessor.h"
#include "net.h"
#include "net_processing.h"
#include "netbase.h"
#include "primitives/block.h"
#include "protocol.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "validation.h"
#include "version.h"

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace llmq
{

struct CQuorumBlockProcessorTestAccess {
    static void SetMined(CQuorumBlockProcessor& processor, Consensus::LLMQType llmqType, const uint256& quorumHash, bool mined)
    {
        LOCK(processor.minableCommitmentsCs);
        processor.hasMinedCommitmentCache[std::make_pair(llmqType, quorumHash)] = mined;
    }

    static void ClearMinedCache(CQuorumBlockProcessor& processor, Consensus::LLMQType llmqType, const uint256& quorumHash)
    {
        LOCK(processor.minableCommitmentsCs);
        processor.hasMinedCommitmentCache.erase(std::make_pair(llmqType, quorumHash));
    }

    static void WriteMined(CQuorumBlockProcessor& processor, const CFinalCommitment& qc, const uint256& minedBlockHash)
    {
        processor.evoDb.Write(
            std::make_pair(std::string("q_mc"), std::make_pair(qc.llmqType, qc.quorumHash)),
            std::make_pair(qc, minedBlockHash));
    }

    static void StoreMined(CQuorumBlockProcessor& processor, const CFinalCommitment& qc, const uint256& minedBlockHash)
    {
        auto dbTx = processor.evoDb.BeginTransaction();
        WriteMined(processor, qc, minedBlockHash);
        dbTx->Commit();
        ClearMinedCache(processor, (Consensus::LLMQType)qc.llmqType, qc.quorumHash);
    }

    static std::pair<size_t, size_t> MinableSizes(CQuorumBlockProcessor& processor)
    {
        LOCK(processor.minableCommitmentsCs);
        return std::make_pair(processor.minableCommitmentsByQuorum.size(), processor.minableCommitments.size());
    }
};

} // namespace llmq

namespace
{

struct QuorumBlockProcessorTestingSetup : BasicTestingSetup {
    std::vector<std::unique_ptr<CBlockIndex> > blockIndexes;
    llmq::CQuorumBlockProcessor processor;

    explicit QuorumBlockProcessorTestingSetup(int chainHeight = 0) : BasicTestingSetup(CBaseChainParams::REGTEST),
                                                                     processor(*evoDb)
    {
        g_connman = std::make_unique<CConnman>(0x1337, 0x1337);
        RegisterNodeSignals(GetNodeSignals());

        LOCK(cs_main);
        if (!mapBlockIndex.empty() || chainActive.Tip() != nullptr) {
            throw std::runtime_error("unexpected pre-existing test chain");
        }

        CBlockIndex* previous = nullptr;
        for (int height = 0; height <= chainHeight; ++height) {
            uint256 hash;
            const unsigned int encodedHeight = static_cast<unsigned int>(height + 1);
            for (size_t byte = 0; byte < sizeof(encodedHeight); ++byte) {
                hash.begin()[byte] = static_cast<unsigned char>(encodedHeight >> (byte * 8));
            }

            auto index = std::make_unique<CBlockIndex>();
            index->nHeight = height;
            index->pprev = previous;

            auto inserted = mapBlockIndex.emplace(hash, index.get());
            if (!inserted.second) {
                throw std::runtime_error("duplicate synthetic block hash");
            }
            index->phashBlock = &inserted.first->first;
            previous = index.get();
            blockIndexes.emplace_back(std::move(index));
        }
        chainActive.SetTip(previous);
    }

    ~QuorumBlockProcessorTestingSetup()
    {
        UnregisterNodeSignals(GetNodeSignals());
        LOCK(cs_main);
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }
};

llmq::CFinalCommitment MakeNonNullCommitment(const Consensus::LLMQParams& params, const uint256& quorumHash, size_t signerCount)
{
    llmq::CFinalCommitment qc(params, quorumHash);
    for (size_t i = 0; i < signerCount; ++i) {
        qc.signers.at(i) = true;
    }
    return qc;
}

CBlock MakeCommitmentBlock(const llmq::CFinalCommitment& qc)
{
    llmq::CFinalCommitmentTxPayload payload;
    payload.commitment = qc;

    CMutableTransaction tx;
    tx.nVersion = 3;
    tx.nType = TRANSACTION_QUORUM_COMMITMENT;
    SetTxPayload(tx, payload);

    CBlock block;
    block.vtx.emplace_back(MakeTransactionRef(tx));
    return block;
}

int ProcessCommitmentFromPeer(llmq::CQuorumBlockProcessor& processor, CConnman& connman, const llmq::CFinalCommitment& qc, int peerVersion)
{
    static NodeId nextNodeId = 10000;
    const NodeId nodeId = nextNodeId++;

    CService service;
    if (!Lookup("127.0.0.1", service, Params().GetDefaultPort(), false)) {
        throw std::runtime_error("failed to construct test peer address");
    }
    CAddress address(service, NODE_NETWORK);
    CNode peer(nodeId, NODE_NETWORK, chainActive.Height(), INVALID_SOCKET, address, 0, 0, "", true);
    peer.SetSendVersion(std::min(peerVersion, PROTOCOL_VERSION));
    peer.SetRecvVersion(std::min(peerVersion, PROTOCOL_VERSION));
    peer.nVersion = peerVersion;
    peer.fSuccessfullyConnected = true;

    GetNodeSignals().InitializeNode(&peer, connman);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << qc;
    processor.ProcessMessage(&peer, NetMsgType::QFCOMMITMENT, stream, connman);

    CNodeStateStats stats;
    const bool found = GetNodeStateStats(nodeId, stats);
    bool updateConnectionTime = false;
    GetNodeSignals().FinalizeNode(nodeId, updateConnectionTime);
    if (!found) {
        throw std::runtime_error("test peer state not found");
    }
    return stats.nMisbehavior;
}

} // namespace

BOOST_AUTO_TEST_SUITE(quorums_blockprocessor_tests)

BOOST_AUTO_TEST_CASE(stale_and_mined_commitments_are_rejected_early)
{
    QuorumBlockProcessorTestingSetup setup(96);
    const auto& params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60);
    BOOST_CHECK(PROTOCOL_VERSION >= QFCOMMIT_STALE_REPROP_BAN_VERSION);

    uint256 staleQuorumHash;
    uint256 boundaryQuorumHash;
    {
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(chainActive.Height(), 96);
        staleQuorumHash = chainActive[48]->GetBlockHash();
        boundaryQuorumHash = chainActive[72]->GetBlockHash();
    }

    const auto staleQc = MakeNonNullCommitment(params, staleQuorumHash, 1);
    const auto boundaryQc = MakeNonNullCommitment(params, boundaryQuorumHash, 1);

    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(setup.processor, *g_connman, staleQc, QFCOMMIT_STALE_REPROP_BAN_VERSION - 1), 0);
    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(setup.processor, *g_connman, staleQc, QFCOMMIT_STALE_REPROP_BAN_VERSION), 100);

    // Exactly one DKG interval old is not stale, so this invalid commitment reaches normal verification.
    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(setup.processor, *g_connman, boundaryQc, QFCOMMIT_STALE_REPROP_BAN_VERSION - 1), 100);

    llmq::CQuorumBlockProcessorTestAccess::SetMined(setup.processor, params.type, boundaryQuorumHash, true);
    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(setup.processor, *g_connman, boundaryQc, QFCOMMIT_STALE_REPROP_BAN_VERSION), 0);
}

BOOST_AUTO_TEST_CASE(peer_prefers_commitments_with_more_signers)
{
    QuorumBlockProcessorTestingSetup setup(96);
    const auto& params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60);

    uint256 quorumHash;
    {
        LOCK(cs_main);
        quorumHash = chainActive[72]->GetBlockHash();
    }

    setup.processor.AddMinableCommitment(MakeNonNullCommitment(params, quorumHash, 3));

    // A weaker candidate is ignored before signature verification.
    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(
                          setup.processor, *g_connman, MakeNonNullCommitment(params, quorumHash, 2), PROTOCOL_VERSION),
        0);
    // A stronger candidate reaches normal verification; this synthetic commitment is invalid.
    BOOST_CHECK_EQUAL(ProcessCommitmentFromPeer(
                          setup.processor, *g_connman, MakeNonNullCommitment(params, quorumHash, 4), PROTOCOL_VERSION),
        100);
}

BOOST_AUTO_TEST_CASE(minable_commitment_cache_lifecycle)
{
    QuorumBlockProcessorTestingSetup setup;
    const auto& params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60);
    const uint256 quorumHash = GetRandHash();
    const auto oldQc = MakeNonNullCommitment(params, quorumHash, 3);
    const auto betterQc = MakeNonNullCommitment(params, quorumHash, 4);
    const uint256 oldHash = ::SerializeHash(oldQc);
    const uint256 betterHash = ::SerializeHash(betterQc);

    setup.processor.AddMinableCommitment(oldQc);
    setup.processor.AddMinableCommitment(betterQc);

    BOOST_CHECK(!setup.processor.HasMinableCommitment(oldHash));
    BOOST_CHECK(setup.processor.HasMinableCommitment(betterHash));
    const auto sizesAfterReplacement = llmq::CQuorumBlockProcessorTestAccess::MinableSizes(setup.processor);
    BOOST_CHECK_EQUAL(sizesAfterReplacement.first, 1);
    BOOST_CHECK_EQUAL(sizesAfterReplacement.second, 1);

    // A rejected or validation-only block does not call the post-connect hook, so the candidate remains.
    BOOST_CHECK(setup.processor.HasMinableCommitment(betterHash));

    // Committing a block with either known candidate removes the selected commitment for this quorum.
    {
        LOCK(cs_main);
        setup.processor.RemoveMinableCommitments(MakeCommitmentBlock(oldQc));
    }
    BOOST_CHECK(!setup.processor.HasMinableCommitment(oldHash));
    BOOST_CHECK(!setup.processor.HasMinableCommitment(betterHash));
    const auto sizesAfterRemoval = llmq::CQuorumBlockProcessorTestAccess::MinableSizes(setup.processor);
    BOOST_CHECK_EQUAL(sizesAfterRemoval.first, 0);
    BOOST_CHECK_EQUAL(sizesAfterRemoval.second, 0);

    // Late peer or DKG completion must not repopulate an already-mined quorum.
    llmq::CQuorumBlockProcessorTestAccess::SetMined(setup.processor, params.type, quorumHash, true);
    setup.processor.AddMinableCommitment(oldQc);
    BOOST_CHECK(!setup.processor.HasMinableCommitment(oldHash));
    const auto sizesAfterLateAdd = llmq::CQuorumBlockProcessorTestAccess::MinableSizes(setup.processor);
    BOOST_CHECK_EQUAL(sizesAfterLateAdd.first, 0);
    BOOST_CHECK_EQUAL(sizesAfterLateAdd.second, 0);

    // Undoing the mined state permits the commitment to be restored for a reorg.
    llmq::CQuorumBlockProcessorTestAccess::SetMined(setup.processor, params.type, quorumHash, false);
    setup.processor.AddMinableCommitment(oldQc);
    BOOST_CHECK(setup.processor.HasMinableCommitment(oldHash));
    const auto sizesAfterUndo = llmq::CQuorumBlockProcessorTestAccess::MinableSizes(setup.processor);
    BOOST_CHECK_EQUAL(sizesAfterUndo.first, 1);
    BOOST_CHECK_EQUAL(sizesAfterUndo.second, 1);
}

BOOST_AUTO_TEST_CASE(validation_only_undo_preserves_mined_cache_state)
{
    const int dip3Height = Params().GetConsensus().DIP0003Height;
    QuorumBlockProcessorTestingSetup setup(dip3Height);
    const auto& params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60);
    const uint256 quorumHash = GetRandHash();
    const auto qc = MakeNonNullCommitment(params, quorumHash, 3);
    const uint256 commitmentHash = ::SerializeHash(qc);
    const CBlock block = MakeCommitmentBlock(qc);
    const CBlockIndex* pindex = setup.blockIndexes.at(dip3Height).get();

    llmq::CQuorumBlockProcessorTestAccess::StoreMined(setup.processor, qc, block.GetHash());
    BOOST_REQUIRE(setup.processor.HasMinedCommitment(params.type, quorumHash));

    {
        LOCK(cs_main);
        auto dbTx = evoDb->BeginTransaction();
        BOOST_REQUIRE(setup.processor.UndoBlock(block, pindex));
        // The transaction is deliberately rolled back, as in VerifyDB level 3.
    }

    BOOST_CHECK(setup.processor.HasMinedCommitment(params.type, quorumHash));
    BOOST_CHECK(!setup.processor.HasMinableCommitment(commitmentHash));

    {
        LOCK(cs_main);
        auto dbTx = evoDb->BeginTransaction();
        BOOST_REQUIRE(setup.processor.UndoBlock(block, pindex));
        dbTx->Commit();
        setup.processor.AddMinableCommitments(block, pindex);
    }

    BOOST_CHECK(!setup.processor.HasMinedCommitment(params.type, quorumHash));
    BOOST_CHECK(setup.processor.HasMinableCommitment(commitmentHash));
}

BOOST_AUTO_TEST_CASE(mined_cache_lookup_waits_for_block_transaction)
{
    QuorumBlockProcessorTestingSetup setup;
    const auto& params = Params().GetConsensus().llmqs.at(Consensus::LLMQ_5_60);
    const uint256 quorumHash = GetRandHash();
    const auto qc = MakeNonNullCommitment(params, quorumHash, 3);

    std::atomic<bool> queryFinished{false};
    bool queryResult = true;
    std::promise<void> queryStarted;
    auto queryStartedFuture = queryStarted.get_future();
    std::thread queryThread;

    {
        LOCK(cs_main);
        auto dbTx = evoDb->BeginTransaction();
        llmq::CQuorumBlockProcessorTestAccess::WriteMined(setup.processor, qc, GetRandHash());
        llmq::CQuorumBlockProcessorTestAccess::ClearMinedCache(setup.processor, params.type, quorumHash);

        queryThread = std::thread([&] {
            queryStarted.set_value();
            queryResult = setup.processor.HasMinedCommitment(params.type, quorumHash);
            queryFinished = true;
        });
        queryStartedFuture.wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // The query must wait for cs_main and must not cache the transaction-local write.
        BOOST_CHECK(!queryFinished);
        // The transaction is deliberately rolled back before cs_main is released.
    }

    queryThread.join();
    BOOST_CHECK(!queryResult);
    BOOST_CHECK(!setup.processor.HasMinedCommitment(params.type, quorumHash));
}

BOOST_AUTO_TEST_SUITE_END()
