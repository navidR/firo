// Copyright (c) 2020 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txrequest.h>
#include <uint256.h>

#include <test/test_bitcoin.h>
#include <test/test_random.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(txrequest_tests, BasicTestingSetup)

namespace
{

constexpr std::chrono::microseconds MIN_TIME = std::chrono::microseconds::min();
constexpr std::chrono::microseconds MAX_TIME = std::chrono::microseconds::max();
constexpr std::chrono::microseconds MICROSECOND{1};
constexpr std::chrono::microseconds NO_TIME{0};

uint64_t InsecureRandBits(int bits)
{
    return insecure_rand_ctx.randbits(bits);
}

uint64_t InsecureRandRange(uint64_t range)
{
    return insecure_rand_ctx.randrange(range);
}

bool InsecureRandBool()
{
    return insecure_rand_ctx.randbool();
}

uint256 InsecureRand256()
{
    uint256 result;
    for (auto& byte : result) {
        byte = static_cast<unsigned char>(InsecureRandBits(8));
    }
    return result;
}

using Action = std::pair<std::chrono::microseconds, std::function<void()> >;

struct Runner {
    TxRequestTracker txrequest;
    std::vector<Action> actions;
    std::set<NodeId> peerset;
    std::set<uint256> txhashset;
    std::multiset<std::pair<NodeId, GenTxid> > expired;
};

std::chrono::microseconds RandomTime8s() { return std::chrono::microseconds{1 + InsecureRandBits(23)}; }
std::chrono::microseconds RandomTime1y() { return std::chrono::microseconds{1 + InsecureRandBits(45)}; }

class Scenario
{
    Runner& m_runner;
    std::chrono::microseconds m_now;
    std::string m_testname;

public:
    Scenario(Runner& runner, std::chrono::microseconds starttime) : m_runner(runner), m_now(starttime) {}

    void SetTestName(std::string testname)
    {
        m_testname = std::move(testname);
    }

    void AdvanceTime(std::chrono::microseconds amount)
    {
        assert(amount.count() >= 0);
        m_now += amount;
    }

    void ForgetTxHash(const uint256& txhash)
    {
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            runner.txrequest.ForgetTxHash(txhash);
            runner.txrequest.SanityCheck();
        });
    }

    void ReceivedInv(NodeId peer, const GenTxid& gtxid, bool pref, std::chrono::microseconds reqtime)
    {
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            runner.txrequest.ReceivedInv(peer, gtxid, pref, reqtime);
            runner.txrequest.SanityCheck();
        });
    }

    void DisconnectedPeer(NodeId peer)
    {
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            runner.txrequest.DisconnectedPeer(peer);
            runner.txrequest.SanityCheck();
        });
    }

    void RequestedTx(NodeId peer, const uint256& txhash, std::chrono::microseconds exptime)
    {
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            runner.txrequest.RequestedTx(peer, txhash, exptime);
            runner.txrequest.SanityCheck();
        });
    }

    void ReceivedResponse(NodeId peer, const uint256& txhash)
    {
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            runner.txrequest.ReceivedResponse(peer, txhash);
            runner.txrequest.SanityCheck();
        });
    }

    void Check(NodeId peer, const std::vector<GenTxid>& expected, size_t candidates, size_t inflight, size_t completed, const std::string& checkname, std::chrono::microseconds offset = std::chrono::microseconds{0})
    {
        const auto comment = m_testname + " " + checkname;
        auto& runner = m_runner;
        const auto now = m_now;
        assert(offset.count() <= 0);
        runner.actions.emplace_back(m_now, [=, &runner]() {
            std::vector<std::pair<NodeId, GenTxid> > expired_now;
            auto ret = runner.txrequest.GetRequestable(peer, now + offset, &expired_now);
            for (const auto& entry : expired_now)
                runner.expired.insert(entry);
            runner.txrequest.SanityCheck();
            runner.txrequest.PostGetRequestableSanityCheck(now + offset);
            size_t total = candidates + inflight + completed;
            size_t real_total = runner.txrequest.Count(peer);
            size_t real_candidates = runner.txrequest.CountCandidates(peer);
            size_t real_inflight = runner.txrequest.CountInFlight(peer);
            BOOST_CHECK_MESSAGE(real_total == total, strprintf("[%s] total %i (%i expected)", comment, real_total, total));
            BOOST_CHECK_MESSAGE(real_inflight == inflight, strprintf("[%s] inflight %i (%i expected)", comment, real_inflight, inflight));
            BOOST_CHECK_MESSAGE(real_candidates == candidates, strprintf("[%s] candidates %i (%i expected)", comment, real_candidates, candidates));
            BOOST_CHECK_MESSAGE(ret == expected, "[" + comment + "] mismatching requestables");
        });
    }

    void CheckExpired(NodeId peer, GenTxid gtxid)
    {
        const auto& testname = m_testname;
        auto& runner = m_runner;
        runner.actions.emplace_back(m_now, [=, &runner]() {
            auto it = runner.expired.find(std::pair<NodeId, GenTxid>{peer, gtxid});
            BOOST_CHECK_MESSAGE(it != runner.expired.end(), "[" + testname + "] missing expiration");
            if (it != runner.expired.end()) runner.expired.erase(it);
        });
    }

    uint256 NewTxHash(const std::vector<std::vector<NodeId> >& orders = {})
    {
        uint256 ret;
        bool ok;
        do {
            ret = InsecureRand256();
            ok = true;
            for (const auto& order : orders) {
                for (size_t pos = 1; pos < order.size(); ++pos) {
                    uint64_t prio_prev = m_runner.txrequest.ComputePriority(ret, order[pos - 1], true);
                    uint64_t prio_cur = m_runner.txrequest.ComputePriority(ret, order[pos], true);
                    if (prio_prev <= prio_cur) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) break;
            }
            if (ok) {
                ok = m_runner.txhashset.insert(ret).second;
            }
        } while (!ok);
        return ret;
    }

    GenTxid NewGTxid(const std::vector<std::vector<NodeId> >& orders = {})
    {
        return {InsecureRandBool(), NewTxHash(orders)};
    }

    NodeId NewPeer()
    {
        bool ok;
        NodeId ret;
        do {
            ret = InsecureRandBits(63);
            ok = m_runner.peerset.insert(ret).second;
        } while (!ok);
        return ret;
    }

    std::chrono::microseconds Now() const { return m_now; }
};

void BuildSingleTest(Scenario& scenario, int config)
{
    auto peer = scenario.NewPeer();
    auto gtxid = scenario.NewGTxid();
    bool immediate = config & 1;
    bool preferred = config & 2;
    auto delay = immediate ? NO_TIME : RandomTime8s();

    scenario.SetTestName(strprintf("Single(config=%i)", config));
    scenario.ReceivedInv(peer, gtxid, preferred, immediate ? MIN_TIME : scenario.Now() + delay);
    if (immediate) {
        scenario.Check(peer, {gtxid}, 1, 0, 0, "s1");
    } else {
        scenario.Check(peer, {}, 1, 0, 0, "s2");
        scenario.AdvanceTime(delay - MICROSECOND);
        scenario.Check(peer, {}, 1, 0, 0, "s3");
        scenario.AdvanceTime(MICROSECOND);
        scenario.Check(peer, {gtxid}, 1, 0, 0, "s4");
    }

    if (config >> 3) {
        scenario.AdvanceTime(RandomTime8s());
        auto expiry = RandomTime8s();
        scenario.Check(peer, {gtxid}, 1, 0, 0, "s5");
        scenario.RequestedTx(peer, gtxid.GetHash(), scenario.Now() + expiry);
        scenario.Check(peer, {}, 0, 1, 0, "s6");

        if ((config >> 3) == 1) {
            scenario.AdvanceTime(expiry - MICROSECOND);
            scenario.Check(peer, {}, 0, 1, 0, "s7");
            scenario.AdvanceTime(MICROSECOND);
            scenario.Check(peer, {}, 0, 0, 0, "s8");
            scenario.CheckExpired(peer, gtxid);
            return;
        } else {
            scenario.AdvanceTime(std::chrono::microseconds{InsecureRandRange(expiry.count())});
            scenario.Check(peer, {}, 0, 1, 0, "s9");
            if ((config >> 3) == 3) {
                scenario.ReceivedResponse(peer, gtxid.GetHash());
                scenario.Check(peer, {}, 0, 0, 0, "s10");
                return;
            }
        }
    }

    if (config & 4) {
        scenario.DisconnectedPeer(peer);
    } else {
        scenario.ForgetTxHash(gtxid.GetHash());
    }
    scenario.Check(peer, {}, 0, 0, 0, "s11");
}

void BuildPriorityTest(Scenario& scenario, int config)
{
    scenario.SetTestName(strprintf("Priority(config=%i)", config));

    auto peer1 = scenario.NewPeer();
    auto peer2 = scenario.NewPeer();
    bool prio1 = config & 1;
    auto gtxid = prio1 ? scenario.NewGTxid({{peer1, peer2}}) : scenario.NewGTxid({{peer2, peer1}});
    bool pref1 = config & 2;
    bool pref2 = config & 4;

    scenario.ReceivedInv(peer1, gtxid, pref1, MIN_TIME);
    scenario.Check(peer1, {gtxid}, 1, 0, 0, "p1");
    if (InsecureRandBool()) {
        scenario.AdvanceTime(RandomTime8s());
        scenario.Check(peer1, {gtxid}, 1, 0, 0, "p2");
    }

    scenario.ReceivedInv(peer2, gtxid, pref2, MIN_TIME);
    bool stage2_prio = (pref2 && !pref1) || (pref1 == pref2 && !prio1);
    NodeId priopeer = stage2_prio ? peer2 : peer1;
    NodeId otherpeer = stage2_prio ? peer1 : peer2;
    scenario.Check(otherpeer, {}, 1, 0, 0, "p3");
    scenario.Check(priopeer, {gtxid}, 1, 0, 0, "p4");
    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.Check(otherpeer, {}, 1, 0, 0, "p5");
    scenario.Check(priopeer, {gtxid}, 1, 0, 0, "p6");

    if (config & 8) {
        scenario.RequestedTx(priopeer, gtxid.GetHash(), MAX_TIME);
        scenario.Check(priopeer, {}, 0, 1, 0, "p7");
        scenario.Check(otherpeer, {}, 1, 0, 0, "p8");
        if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    }

    if (config & 16) {
        scenario.DisconnectedPeer(priopeer);
    } else {
        scenario.ReceivedResponse(priopeer, gtxid.GetHash());
    }
    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.Check(priopeer, {}, 0, 0, !(config & 16), "p8");
    scenario.Check(otherpeer, {gtxid}, 1, 0, 0, "p9");
    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());

    scenario.DisconnectedPeer(otherpeer);
    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.Check(peer1, {}, 0, 0, 0, "p10");
    scenario.Check(peer2, {}, 0, 0, 0, "p11");
}

void BuildBigPriorityTest(Scenario& scenario, int peers)
{
    scenario.SetTestName(strprintf("BigPriority(peers=%i)", peers));

    std::map<NodeId, bool> preferred;
    std::vector<NodeId> pref_peers;
    std::vector<NodeId> npref_peers;
    int num_pref = InsecureRandRange(peers + 1);
    int num_npref = peers - num_pref;
    for (int i = 0; i < num_pref; ++i) {
        pref_peers.push_back(scenario.NewPeer());
        preferred[pref_peers.back()] = true;
    }
    for (int i = 0; i < num_npref; ++i) {
        npref_peers.push_back(scenario.NewPeer());
        preferred[npref_peers.back()] = false;
    }

    std::vector<NodeId> request_order;
    for (int i = 0; i < num_pref; ++i)
        request_order.push_back(pref_peers[i]);
    for (int i = 0; i < num_npref; ++i)
        request_order.push_back(npref_peers[i]);

    std::vector<NodeId> announce_order = request_order;
    Shuffle(announce_order.begin(), announce_order.end(), insecure_rand_ctx);

    auto gtxid = scenario.NewGTxid({pref_peers, npref_peers});

    std::map<NodeId, std::chrono::microseconds> reqtimes;
    auto reqtime = scenario.Now();
    for (int i = peers - 1; i >= 0; --i) {
        reqtime += RandomTime8s();
        reqtimes[request_order[i]] = reqtime;
    }

    for (const auto peer : announce_order) {
        scenario.ReceivedInv(peer, gtxid, preferred[peer], reqtimes[peer]);
    }
    for (const auto peer : announce_order) {
        scenario.Check(peer, {}, 1, 0, 0, "b1");
    }

    for (int i = peers - 1; i >= 0; --i) {
        scenario.AdvanceTime(reqtimes[request_order[i]] - scenario.Now() - MICROSECOND);
        scenario.Check(request_order[i], {}, 1, 0, 0, "b2");
        scenario.AdvanceTime(MICROSECOND);
        scenario.Check(request_order[i], {gtxid}, 1, 0, 0, "b3");
    }

    for (int i = 0; i < peers; ++i) {
        if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
        const int pos = InsecureRandRange(request_order.size());
        const auto peer = request_order[pos];
        request_order.erase(request_order.begin() + pos);
        if (InsecureRandBool()) {
            scenario.DisconnectedPeer(peer);
            scenario.Check(peer, {}, 0, 0, 0, "b4");
        } else {
            scenario.ReceivedResponse(peer, gtxid.GetHash());
            scenario.Check(peer, {}, 0, 0, request_order.size() > 0, "b5");
        }
        if (!request_order.empty()) {
            scenario.Check(request_order[0], {gtxid}, 1, 0, 0, "b6");
        }
    }

    for (const auto peer : announce_order) {
        scenario.Check(peer, {}, 0, 0, 0, "b7");
    }
}

void BuildRequestOrderTest(Scenario& scenario, int config)
{
    scenario.SetTestName(strprintf("RequestOrder(config=%i)", config));

    auto peer = scenario.NewPeer();
    auto gtxid1 = scenario.NewGTxid();
    auto gtxid2 = scenario.NewGTxid();

    auto reqtime2 = scenario.Now() + RandomTime8s();
    auto reqtime1 = reqtime2 + RandomTime8s();

    scenario.ReceivedInv(peer, gtxid1, config & 1, reqtime1);
    scenario.ReceivedInv(peer, gtxid2, config & 2, reqtime2);

    scenario.AdvanceTime(reqtime2 - MICROSECOND - scenario.Now());
    scenario.Check(peer, {}, 2, 0, 0, "o1");
    scenario.AdvanceTime(MICROSECOND);
    scenario.Check(peer, {gtxid2}, 2, 0, 0, "o2");
    scenario.AdvanceTime(reqtime1 - MICROSECOND - scenario.Now());
    scenario.Check(peer, {gtxid2}, 2, 0, 0, "o3");
    scenario.AdvanceTime(MICROSECOND);
    scenario.Check(peer, {gtxid1, gtxid2}, 2, 0, 0, "o4");

    scenario.DisconnectedPeer(peer);
    scenario.Check(peer, {}, 0, 0, 0, "o5");
}

void BuildWtxidTest(Scenario& scenario, int config)
{
    scenario.SetTestName(strprintf("Wtxid(config=%i)", config));

    auto peerT = scenario.NewPeer();
    auto peerW = scenario.NewPeer();
    auto txhash = scenario.NewTxHash();
    GenTxid txid{false, txhash};
    GenTxid wtxid{true, txhash};

    auto reqtimeT = InsecureRandBool() ? MIN_TIME : scenario.Now() + RandomTime8s();
    auto reqtimeW = InsecureRandBool() ? MIN_TIME : scenario.Now() + RandomTime8s();

    if (config & 1) {
        scenario.ReceivedInv(peerT, txid, config & 2, reqtimeT);
        if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
        scenario.ReceivedInv(peerW, wtxid, !(config & 2), reqtimeW);
    } else {
        scenario.ReceivedInv(peerW, wtxid, !(config & 2), reqtimeW);
        if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
        scenario.ReceivedInv(peerT, txid, config & 2, reqtimeT);
    }

    auto max_reqtime = std::max(reqtimeT, reqtimeW);
    if (max_reqtime > scenario.Now()) scenario.AdvanceTime(max_reqtime - scenario.Now());
    if (config & 2) {
        scenario.Check(peerT, {txid}, 1, 0, 0, "w1");
        scenario.Check(peerW, {}, 1, 0, 0, "w2");
    } else {
        scenario.Check(peerT, {}, 1, 0, 0, "w3");
        scenario.Check(peerW, {wtxid}, 1, 0, 0, "w4");
    }

    auto expiry = RandomTime8s();
    if (config & 2) {
        scenario.RequestedTx(peerT, txid.GetHash(), scenario.Now() + expiry);
        scenario.Check(peerT, {}, 0, 1, 0, "w5");
        scenario.Check(peerW, {}, 1, 0, 0, "w6");
    } else {
        scenario.RequestedTx(peerW, wtxid.GetHash(), scenario.Now() + expiry);
        scenario.Check(peerT, {}, 1, 0, 0, "w7");
        scenario.Check(peerW, {}, 0, 1, 0, "w8");
    }

    scenario.AdvanceTime(expiry);
    if (config & 2) {
        scenario.Check(peerT, {}, 0, 0, 1, "w9");
        scenario.Check(peerW, {wtxid}, 1, 0, 0, "w10");
        scenario.CheckExpired(peerT, txid);
    } else {
        scenario.Check(peerT, {txid}, 1, 0, 0, "w11");
        scenario.Check(peerW, {}, 0, 0, 1, "w12");
        scenario.CheckExpired(peerW, wtxid);
    }

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.ForgetTxHash(txhash);
    scenario.Check(peerT, {}, 0, 0, 0, "w13");
    scenario.Check(peerW, {}, 0, 0, 0, "w14");
}

void BuildTimeBackwardsTest(Scenario& scenario)
{
    auto peer1 = scenario.NewPeer();
    auto peer2 = scenario.NewPeer();
    auto gtxid = scenario.NewGTxid({{peer1, peer2}});

    auto reqtime = scenario.Now() + RandomTime8s();
    scenario.ReceivedInv(peer2, gtxid, true, reqtime);
    scenario.Check(peer2, {}, 1, 0, 0, "r1");
    scenario.AdvanceTime(reqtime - scenario.Now());
    scenario.Check(peer2, {gtxid}, 1, 0, 0, "r2");
    scenario.Check(peer2, {}, 1, 0, 0, "r3", -MICROSECOND);
    scenario.Check(peer2, {gtxid}, 1, 0, 0, "r4");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.ReceivedInv(peer1, gtxid, true, MAX_TIME);
    scenario.Check(peer2, {gtxid}, 1, 0, 0, "r5");
    scenario.Check(peer1, {}, 1, 0, 0, "r6");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    auto expiry = scenario.Now() + RandomTime8s();
    scenario.RequestedTx(peer1, gtxid.GetHash(), expiry);
    scenario.Check(peer1, {}, 0, 1, 0, "r7");
    scenario.Check(peer2, {}, 1, 0, 0, "r8");

    scenario.AdvanceTime(expiry - scenario.Now());
    scenario.Check(peer1, {}, 0, 0, 1, "r9");
    scenario.Check(peer2, {gtxid}, 1, 0, 0, "r10");
    scenario.CheckExpired(peer1, gtxid);
    scenario.Check(peer1, {}, 0, 0, 1, "r11", -MICROSECOND);
    scenario.Check(peer2, {gtxid}, 1, 0, 0, "r12", -MICROSECOND);

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.DisconnectedPeer(peer2);
    scenario.Check(peer1, {}, 0, 0, 0, "r13");
    scenario.Check(peer2, {}, 0, 0, 0, "r14");
}

void BuildWeirdRequestsTest(Scenario& scenario)
{
    auto peer1 = scenario.NewPeer();
    auto peer2 = scenario.NewPeer();
    auto gtxid1 = scenario.NewGTxid({{peer1, peer2}});
    auto gtxid2 = scenario.NewGTxid({{peer2, peer1}});

    scenario.ReceivedInv(peer1, gtxid1, true, MIN_TIME);
    scenario.Check(peer1, {gtxid1}, 1, 0, 0, "q1");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.ReceivedInv(peer2, gtxid2, true, MIN_TIME);
    scenario.Check(peer1, {gtxid1}, 1, 0, 0, "q2");
    scenario.Check(peer2, {gtxid2}, 1, 0, 0, "q3");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.RequestedTx(peer1, gtxid2.GetHash(), MAX_TIME);
    scenario.Check(peer1, {gtxid1}, 1, 0, 0, "q4");
    scenario.Check(peer2, {gtxid2}, 1, 0, 0, "q5");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    auto expiryA = scenario.Now() + RandomTime8s();
    scenario.RequestedTx(peer1, gtxid1.GetHash(), expiryA);
    scenario.Check(peer1, {}, 0, 1, 0, "q6");
    scenario.Check(peer2, {gtxid2}, 1, 0, 0, "q7");

    auto expiryB = expiryA + RandomTime8s();
    scenario.RequestedTx(peer1, gtxid1.GetHash(), expiryB);
    scenario.Check(peer1, {}, 0, 1, 0, "q8");
    scenario.Check(peer2, {gtxid2}, 1, 0, 0, "q9");

    scenario.ReceivedInv(peer2, gtxid1, true, MIN_TIME);
    scenario.Check(peer1, {}, 0, 1, 0, "q10");
    scenario.Check(peer2, {gtxid2}, 2, 0, 0, "q11");

    scenario.AdvanceTime(expiryA - scenario.Now());
    scenario.Check(peer1, {}, 0, 0, 1, "q12");
    scenario.Check(peer2, {gtxid2, gtxid1}, 2, 0, 0, "q13");
    scenario.CheckExpired(peer1, gtxid1);

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.RequestedTx(peer1, gtxid1.GetHash(), MAX_TIME);
    scenario.Check(peer1, {}, 0, 0, 1, "q14");
    scenario.Check(peer2, {gtxid2, gtxid1}, 2, 0, 0, "q15");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.ReceivedInv(peer1, gtxid2, true, MIN_TIME);
    scenario.Check(peer1, {}, 1, 0, 1, "q16");
    scenario.Check(peer2, {gtxid2, gtxid1}, 2, 0, 0, "q17");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.RequestedTx(peer1, gtxid2.GetHash(), MAX_TIME);
    scenario.Check(peer1, {}, 0, 1, 1, "q18");
    scenario.Check(peer2, {gtxid1}, 2, 0, 0, "q19");

    if (InsecureRandBool()) scenario.AdvanceTime(RandomTime8s());
    scenario.RequestedTx(peer2, gtxid2.GetHash(), MAX_TIME);
    scenario.Check(peer1, {}, 0, 0, 2, "q20");
    scenario.Check(peer2, {gtxid1}, 1, 1, 0, "q21");

    scenario.DisconnectedPeer(peer2);
    scenario.Check(peer1, {}, 0, 0, 0, "q22");
    scenario.Check(peer2, {}, 0, 0, 0, "q23");
}

void TestInterleavedScenarios()
{
    std::vector<std::function<void(Scenario&)> > builders;
    for (int n = 0; n < 64; ++n) {
        builders.emplace_back([n](Scenario& scenario) { BuildWtxidTest(scenario, n); });
        builders.emplace_back([n](Scenario& scenario) { BuildRequestOrderTest(scenario, n & 3); });
        builders.emplace_back([n](Scenario& scenario) { BuildSingleTest(scenario, n & 31); });
        builders.emplace_back([n](Scenario& scenario) { BuildPriorityTest(scenario, n & 31); });
        builders.emplace_back([n](Scenario& scenario) { BuildBigPriorityTest(scenario, (n & 7) + 1); });
        builders.emplace_back([](Scenario& scenario) { BuildTimeBackwardsTest(scenario); });
        builders.emplace_back([](Scenario& scenario) { BuildWeirdRequestsTest(scenario); });
    }
    Shuffle(builders.begin(), builders.end(), insecure_rand_ctx);

    Runner runner;
    auto starttime = RandomTime1y();
    while (!builders.empty()) {
        auto scenario_start = starttime + RandomTime8s() + RandomTime8s() + RandomTime8s();
        Scenario scenario(runner, scenario_start);
        for (int j = 0; !builders.empty() && j < 10; ++j) {
            builders.back()(scenario);
            builders.pop_back();
        }
    }

    std::stable_sort(runner.actions.begin(), runner.actions.end(), [](const Action& a1, const Action& a2) {
        return a1.first < a2.first;
    });

    for (auto& action : runner.actions) {
        action.second();
    }

    BOOST_CHECK_EQUAL(runner.txrequest.Size(), 0U);
    BOOST_CHECK(runner.expired.empty());
}

} // namespace

BOOST_AUTO_TEST_CASE(interleaved_upstream_scenarios)
{
    for (int i = 0; i < 5; ++i) {
        TestInterleavedScenarios();
    }
}

BOOST_AUTO_TEST_CASE(rotating_peers_cannot_postpone_preferred_failover)
{
    TxRequestTracker tracker{/* deterministic=*/true};
    const uint256 txhash = InsecureRand256();
    const GenTxid txid{/* is_wtxid=*/false, txhash};
    constexpr NodeId attacker1 = 1;
    constexpr NodeId attacker2 = 2;
    constexpr NodeId honest = 3;
    constexpr std::chrono::microseconds now{1000000};
    constexpr auto expiry = now + std::chrono::seconds{60};

    tracker.ReceivedInv(attacker1, txid, /* preferred=*/false, now);
    BOOST_CHECK(tracker.GetRequestable(attacker1, now) == std::vector<GenTxid>{txid});
    tracker.RequestedTx(attacker1, txhash, expiry);

    // A fresh attacker identity cannot advance the honest peer's request time or outrank it.
    tracker.ReceivedInv(attacker2, txid, /* preferred=*/false, now);
    tracker.ReceivedInv(honest, txid, /* preferred=*/true, now);
    BOOST_CHECK(tracker.GetRequestable(honest, expiry - MICROSECOND).empty());

    std::vector<std::pair<NodeId, GenTxid> > expired;
    BOOST_CHECK(tracker.GetRequestable(honest, expiry, &expired) == std::vector<GenTxid>{txid});
    BOOST_REQUIRE_EQUAL(expired.size(), 1U);
    BOOST_CHECK_EQUAL(expired.front().first, attacker1);
    BOOST_CHECK(expired.front().second == txid);
}

BOOST_AUTO_TEST_SUITE_END()
