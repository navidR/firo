// Copyright (c) 2026 The Firo developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "llmq/quorums_dkgsessionhandler.h"
#include "llmq/quorums_dkgsessionmgr.h"
#include "llmq/quorums_init.h"
#include "net_processing.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{

NodeId nextNodeId = 100000;

CDataStream Payload(uint8_t tag)
{
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << tag;
    return payload;
}

uint256 TaggedHash(uint8_t tag)
{
    uint256 hash;
    hash.begin()[0] = tag;
    return hash;
}

uint256 RawHash(const CDataStream& payload)
{
    CHashWriter hw(SER_GETHASH, 0);
    hw.write(payload.data(), payload.size());
    return hw.GetHash();
}

class TestNode
{
private:
    CConnman& connman;

public:
    CNode node;

    explicit TestNode(CConnman& _connman) : connman(_connman),
                                            node(nextNodeId++, NODE_NETWORK, 0, INVALID_SOCKET, CAddress(), 0, 0, "", true)
    {
        GetNodeSignals().InitializeNode(&node, connman);
    }

    ~TestNode()
    {
        bool updateConnectionTime;
        GetNodeSignals().FinalizeNode(node.GetId(), updateConnectionTime);
    }

    void SetVerifiedIdentity(const uint256& proTxHash)
    {
        LOCK(node.cs_mnauth);
        node.verifiedProRegTxHash = proTxHash;
    }

    int MisbehaviorScore() const
    {
        CNodeStateStats stats;
        if (!GetNodeStateStats(node.GetId(), stats)) {
            return -1;
        }
        return stats.nMisbehavior;
    }
};

class ScopedDIP3Enforcement
{
private:
    Consensus::Params& consensus;
    int oldEnforcementHeight;

public:
    ScopedDIP3Enforcement() : consensus(const_cast<Consensus::Params&>(Params().GetConsensus())),
                              oldEnforcementHeight(consensus.DIP0003EnforcementHeight)
    {
        consensus.DIP0003EnforcementHeight = 0;
        deterministicMNManager->UpdatedBlockTip(chainActive.Tip());
    }

    ~ScopedDIP3Enforcement()
    {
        consensus.DIP0003EnforcementHeight = oldEnforcementHeight;
    }
};

} // namespace

BOOST_FIXTURE_TEST_SUITE(llmq_dkg_intake_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(pending_quota_survives_reconnect)
{
    llmq::CDKGPendingMessages pending(1);
    const uint256 identity = TaggedHash(40);

    CDataStream first = Payload(1);
    pending.PushPendingMessage(1, identity, first);

    CDataStream second = Payload(2);
    pending.PushPendingMessage(2, identity, second);

    CDataStream otherIdentity = Payload(3);
    pending.PushPendingMessage(3, TaggedHash(42), otherIdentity);

    BOOST_CHECK_EQUAL(pending.PopPendingMessages(3).size(), 2U);
}

BOOST_AUTO_TEST_CASE(duplicate_does_not_consume_quota)
{
    llmq::CDKGPendingMessages pending(2);
    const uint256 identity = TaggedHash(41);

    CDataStream first = Payload(1);
    pending.PushPendingMessage(1, identity, first);

    CDataStream duplicate = Payload(1);
    pending.PushPendingMessage(1, identity, duplicate);

    CDataStream second = Payload(2);
    pending.PushPendingMessage(1, identity, second);

    BOOST_CHECK_EQUAL(pending.PopPendingMessages(3).size(), 2U);
}

BOOST_AUTO_TEST_CASE(manager_rejects_unsafe_intake)
{
    ScopedDIP3Enforcement dip3Enforcement;
    llmq::quorumDKGSessionManager->StartMessageHandlerPool();

    const auto llmqType = Consensus::LLMQ_50_60;
    const auto& params = Params().GetConsensus().llmqs.at(llmqType);

    TestNode unverified(*connman);
    CDataStream qwatch(SER_NETWORK, PROTOCOL_VERSION);
    llmq::quorumDKGSessionManager->ProcessMessage(&unverified.node, NetMsgType::QWATCH, qwatch, *connman);
    BOOST_CHECK(unverified.node.qwatch);
    BOOST_CHECK_EQUAL(unverified.MisbehaviorScore(), 0);

    CDataStream unauthenticated = Payload(static_cast<uint8_t>(llmqType));
    llmq::quorumDKGSessionManager->ProcessMessage(&unverified.node, NetMsgType::QCONTRIB, unauthenticated, *connman);
    BOOST_CHECK_EQUAL(unverified.MisbehaviorScore(), 10);

    TestNode oversizedNode(*connman);
    oversizedNode.SetVerifiedIdentity(TaggedHash(1));
    CDataStream oversized = Payload(static_cast<uint8_t>(llmqType));
    oversized.resize((size_t{1} << 20) + 1);
    llmq::quorumDKGSessionManager->ProcessMessage(&oversizedNode.node, NetMsgType::QCONTRIB, oversized, *connman);
    BOOST_CHECK_EQUAL(oversizedNode.MisbehaviorScore(), 100);

    TestNode malformedNode(*connman);
    malformedNode.SetVerifiedIdentity(TaggedHash(2));
    CDataStream malformed = Payload(static_cast<uint8_t>(llmqType));
    llmq::quorumDKGSessionManager->ProcessMessage(&malformedNode.node, NetMsgType::QCONTRIB, malformed, *connman);
    BOOST_CHECK_EQUAL(malformedNode.MisbehaviorScore(), 100);

    llmq::CDKGContribution contribution;
    contribution.llmqType = static_cast<uint8_t>(llmqType);
    contribution.quorumHash = TaggedHash(10);
    contribution.proTxHash = TaggedHash(11);
    contribution.vvec = std::make_shared<BLSVerificationVector>();
    contribution.contributions = std::make_shared<CBLSIESMultiRecipientObjects<CBLSSecretKey> >();
    CDataStream badContribution(SER_NETWORK, PROTOCOL_VERSION);
    badContribution << contribution;

    llmq::CDKGComplaint complaint;
    complaint.llmqType = static_cast<uint8_t>(llmqType);
    complaint.quorumHash = TaggedHash(12);
    complaint.proTxHash = TaggedHash(13);
    CDataStream badComplaint(SER_NETWORK, PROTOCOL_VERSION);
    badComplaint << complaint;

    llmq::CDKGJustification justification;
    justification.llmqType = static_cast<uint8_t>(llmqType);
    justification.quorumHash = TaggedHash(14);
    justification.proTxHash = TaggedHash(15);
    justification.contributions.resize(static_cast<size_t>(params.size) + 1);
    CDataStream badJustification(SER_NETWORK, PROTOCOL_VERSION);
    badJustification << justification;

    llmq::CDKGPrematureCommitment commitment;
    commitment.llmqType = static_cast<uint8_t>(llmqType);
    commitment.quorumHash = TaggedHash(16);
    commitment.proTxHash = TaggedHash(17);
    CDataStream badCommitment(SER_NETWORK, PROTOCOL_VERSION);
    badCommitment << commitment;

    struct BadShape {
        const char* command;
        CDataStream payload;
    };
    std::vector<BadShape> badShapes;
    badShapes.push_back({NetMsgType::QCONTRIB, std::move(badContribution)});
    badShapes.push_back({NetMsgType::QCOMPLAINT, std::move(badComplaint)});
    badShapes.push_back({NetMsgType::QJUSTIFICATION, std::move(badJustification)});
    badShapes.push_back({NetMsgType::QPCOMMITMENT, std::move(badCommitment)});

    uint8_t identityTag = 20;
    for (auto& badShape : badShapes) {
        TestNode node(*connman);
        node.SetVerifiedIdentity(TaggedHash(identityTag++));
        llmq::quorumDKGSessionManager->ProcessMessage(&node.node, badShape.command, badShape.payload, *connman);
        BOOST_CHECK_EQUAL(node.MisbehaviorScore(), 100);
    }

    auto checkAcceptedShape = [&](const char* command, int invType, CDataStream& payload) {
        TestNode node(*connman);
        node.SetVerifiedIdentity(TaggedHash(identityTag++));
        const uint256 hash = RawHash(payload);
        llmq::quorumDKGSessionManager->ProcessMessage(&node.node, command, payload, *connman);
        BOOST_CHECK_EQUAL(node.MisbehaviorScore(), 0);
        BOOST_CHECK(!llmq::quorumDKGSessionManager->AlreadyHave(CInv(invType, hash)));
    };

    auto checkRejectedShape = [&](const char* command, CDataStream& payload) {
        TestNode node(*connman);
        node.SetVerifiedIdentity(TaggedHash(identityTag++));
        llmq::quorumDKGSessionManager->ProcessMessage(&node.node, command, payload, *connman);
        BOOST_CHECK_EQUAL(node.MisbehaviorScore(), 100);
    };

    auto contributionWithBlobCount = [&](size_t blobCount) {
        llmq::CDKGContribution result;
        result.llmqType = static_cast<uint8_t>(llmqType);
        result.quorumHash = TaggedHash(identityTag++);
        result.proTxHash = TaggedHash(identityTag++);
        result.vvec = std::make_shared<BLSVerificationVector>(params.threshold);
        result.contributions = std::make_shared<CBLSIESMultiRecipientObjects<CBLSSecretKey> >();
        result.contributions->blobs.resize(blobCount);
        CDataStream serialized(SER_NETWORK, PROTOCOL_VERSION);
        serialized << result;
        return serialized;
    };

    CDataStream minimumContribution = contributionWithBlobCount(params.minSize);
    checkAcceptedShape(NetMsgType::QCONTRIB, MSG_QUORUM_CONTRIB, minimumContribution);

    CDataStream undersizedContribution = contributionWithBlobCount(params.minSize - 1);
    checkRejectedShape(NetMsgType::QCONTRIB, undersizedContribution);

    CDataStream oversizedContribution = contributionWithBlobCount(params.size + 1);
    checkRejectedShape(NetMsgType::QCONTRIB, oversizedContribution);

    uint8_t messageTag = 50;
    for (const auto& llmqEntry : Params().GetConsensus().llmqs) {
        const auto configuredType = llmqEntry.first;
        const auto& configuredParams = llmqEntry.second;

        llmq::CDKGContribution maxContribution;
        maxContribution.llmqType = static_cast<uint8_t>(configuredType);
        maxContribution.quorumHash = TaggedHash(messageTag++);
        maxContribution.proTxHash = TaggedHash(messageTag++);
        maxContribution.vvec = std::make_shared<BLSVerificationVector>(configuredParams.threshold);
        maxContribution.contributions = std::make_shared<CBLSIESMultiRecipientObjects<CBLSSecretKey> >();
        maxContribution.contributions->blobs.resize(configuredParams.size);
        for (auto& blob : maxContribution.contributions->blobs) {
            blob.resize(128);
        }
        CDataStream serializedContribution(SER_NETWORK, PROTOCOL_VERSION);
        serializedContribution << maxContribution;
        checkAcceptedShape(NetMsgType::QCONTRIB, MSG_QUORUM_CONTRIB, serializedContribution);

        llmq::CDKGComplaint maxComplaint(configuredParams);
        maxComplaint.llmqType = static_cast<uint8_t>(configuredType);
        maxComplaint.quorumHash = TaggedHash(messageTag++);
        maxComplaint.proTxHash = TaggedHash(messageTag++);
        CDataStream serializedComplaint(SER_NETWORK, PROTOCOL_VERSION);
        serializedComplaint << maxComplaint;
        checkAcceptedShape(NetMsgType::QCOMPLAINT, MSG_QUORUM_COMPLAINT, serializedComplaint);

        llmq::CDKGJustification maxJustification;
        maxJustification.llmqType = static_cast<uint8_t>(configuredType);
        maxJustification.quorumHash = TaggedHash(messageTag++);
        maxJustification.proTxHash = TaggedHash(messageTag++);
        maxJustification.contributions.resize(configuredParams.size);
        CDataStream serializedJustification(SER_NETWORK, PROTOCOL_VERSION);
        serializedJustification << maxJustification;
        checkAcceptedShape(NetMsgType::QJUSTIFICATION, MSG_QUORUM_JUSTIFICATION, serializedJustification);

        llmq::CDKGPrematureCommitment maxCommitment(configuredParams);
        maxCommitment.llmqType = static_cast<uint8_t>(configuredType);
        maxCommitment.quorumHash = TaggedHash(messageTag++);
        maxCommitment.proTxHash = TaggedHash(messageTag++);
        CDataStream serializedCommitment(SER_NETWORK, PROTOCOL_VERSION);
        serializedCommitment << maxCommitment;
        checkAcceptedShape(NetMsgType::QPCOMMITMENT, MSG_QUORUM_PREMATURE_COMMITMENT, serializedCommitment);
    }

    llmq::CDKGComplaint validShape(params);
    validShape.llmqType = static_cast<uint8_t>(llmqType);
    validShape.quorumHash = TaggedHash(30);
    validShape.proTxHash = TaggedHash(31);
    CDataStream serializedValidShape(SER_NETWORK, PROTOCOL_VERSION);
    serializedValidShape << validShape;

    constexpr size_t compactSize = 5;
    constexpr size_t prefixSize = 1 + 32 + 32;
    constexpr size_t signatureSize = BLS_CURVE_SIG_SIZE;
    constexpr size_t slackSize = 1024;
    const size_t quorumSize = static_cast<size_t>(params.size);
    const size_t complaintCap = prefixSize + 2 * (compactSize + (quorumSize + 7) / 8) + signatureSize + slackSize;

    for (int delta = -1; delta <= 1; ++delta) {
        TestNode node(*connman);
        node.SetVerifiedIdentity(TaggedHash(identityTag++));
        CDataStream payload = serializedValidShape;
        payload.resize(complaintCap + delta);
        const uint256 hash = RawHash(payload);
        llmq::quorumDKGSessionManager->ProcessMessage(&node.node, NetMsgType::QCOMPLAINT, payload, *connman);
        BOOST_CHECK_EQUAL(node.MisbehaviorScore(), delta > 0 ? 100 : 0);
        BOOST_CHECK(!llmq::quorumDKGSessionManager->AlreadyHave(CInv(MSG_QUORUM_COMPLAINT, hash)));
    }
}

BOOST_AUTO_TEST_SUITE_END()
