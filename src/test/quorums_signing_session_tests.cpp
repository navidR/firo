// Copyright (c) 2026 The Firo developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq/quorums_signing.h"
#include "llmq/quorums_signing_shares.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace
{

llmq::CSigSesAnn MakeAnn(uint32_t sessionId, uint32_t nonce, Consensus::LLMQType llmqType = Consensus::LLMQ_50_60)
{
    llmq::CSigSesAnn ann;
    ann.sessionId = sessionId;
    ann.llmqType = llmqType;
    ann.quorumHash = uint256S("1");
    ann.id = uint256S("2");
    ann.msgHash = uint256S(strprintf("%x", nonce + 3));
    return ann;
}

llmq::CSigShare MakeSigShare(uint32_t nonce, Consensus::LLMQType llmqType = Consensus::LLMQ_50_60)
{
    llmq::CSigShare sigShare;
    sigShare.llmqType = llmqType;
    sigShare.quorumHash = uint256S("1");
    sigShare.quorumMember = 0;
    sigShare.id = uint256S("2");
    sigShare.msgHash = uint256S(strprintf("%x", nonce + 3));
    sigShare.UpdateKey();
    return sigShare;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(quorums_signing_session_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(announcements_respect_limit_and_allow_refresh)
{
    llmq::CSigSharesNodeState nodeState;

    const auto ann1 = MakeAnn(1, 1);
    const auto ann2 = MakeAnn(2, 2);
    const auto ann3 = MakeAnn(3, 3);
    constexpr size_t maxSessions{2};

    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(ann1, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(ann1);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(), 1U);
    BOOST_CHECK_EQUAL(nodeState.GetAnnouncementSessionCount(Consensus::LLMQ_50_60), 1U);

    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(ann2, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(ann2);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(), maxSessions);
    BOOST_CHECK_EQUAL(nodeState.GetAnnouncementSessionCount(Consensus::LLMQ_50_60), maxSessions);

    BOOST_CHECK(!nodeState.CanCreateSessionFromAnn(ann3, maxSessions));

    auto refresh = ann1;
    refresh.sessionId = 4;
    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(refresh, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(refresh);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(), maxSessions);
    BOOST_CHECK_EQUAL(nodeState.GetAnnouncementSessionCount(Consensus::LLMQ_50_60), maxSessions);
}

BOOST_AUTO_TEST_CASE(limit_ignores_send_only_sessions)
{
    llmq::CSigSharesNodeState nodeState;

    constexpr size_t maxSessions{1};
    const auto sigShare = MakeSigShare(1);
    const auto ann = MakeAnn(1, 2);

    nodeState.GetOrCreateSessionFromShare(sigShare);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(Consensus::LLMQ_50_60), 1U);
    BOOST_CHECK_EQUAL(nodeState.GetAnnouncementSessionCount(Consensus::LLMQ_50_60), 0U);

    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(ann, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(ann);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(Consensus::LLMQ_50_60), 2U);
    BOOST_CHECK_EQUAL(nodeState.GetAnnouncementSessionCount(Consensus::LLMQ_50_60), 1U);
}

BOOST_AUTO_TEST_CASE(limit_is_per_llmq_type)
{
    llmq::CSigSharesNodeState nodeState;

    constexpr size_t maxSessions{1};
    const auto ann1 = MakeAnn(1, 1);
    const auto ann2 = MakeAnn(2, 2);
    const auto otherTypeAnn = MakeAnn(3, 3, Consensus::LLMQ_400_60);

    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(ann1, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(ann1);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(), 1U);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(Consensus::LLMQ_50_60), 1U);

    BOOST_CHECK(!nodeState.CanCreateSessionFromAnn(ann2, maxSessions));
    BOOST_CHECK(nodeState.CanCreateSessionFromAnn(otherTypeAnn, maxSessions));
    nodeState.GetOrCreateSessionFromAnn(otherTypeAnn);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(), 2U);
    BOOST_CHECK_EQUAL(nodeState.GetSessionCount(Consensus::LLMQ_400_60), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
