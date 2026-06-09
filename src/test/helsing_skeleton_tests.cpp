// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/validation.h"
#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

namespace {

GroupElement DeterministicPoint(unsigned char tag)
{
    unsigned char seed[32] = {};
    seed[0] = tag;

    GroupElement point;
    point.generate(seed);
    return point;
}

uint256 DeterministicHash(unsigned char tag)
{
    uint256 hash;
    hash.begin()[0] = tag;
    return hash;
}

helsing::OutputId Output(unsigned char txidTag, uint32_t vout)
{
    return helsing::OutputId(DeterministicHash(txidTag), vout);
}

helsing::SparkOutputRecord EligibleOutput(const helsing::OutputId& output_id, unsigned char pointTag)
{
    helsing::SparkOutputRecord record;
    record.output_id = output_id;
    record.S = DeterministicPoint(pointTag);
    record.C = DeterministicPoint(pointTag + 1);
    record.K = DeterministicPoint(pointTag + 2);
    record.nHeight = 100;
    record.type = helsing::SparkOutputType::MINT;
    record.helsing_eligible = true;
    return record;
}

helsing::StakeTx ValidStakeTx()
{
    helsing::StakeTx tx;
    tx.inCoinIDs = {Output(1, 0), Output(2, 0)};
    tx.S_prime = DeterministicPoint(10);
    tx.C_prime = DeterministicPoint(11);
    tx.T = DeterministicPoint(12);
    tx.m.bytes = {0x6d};
    tx.pi_par.bytes = {0x01};
    tx.pi_val.bytes = {0x02};
    tx.pi_tag.bytes = {0x03};
    return tx;
}

helsing::ValidationStateView ValidView(const helsing::StakeTx& tx)
{
    helsing::ValidationStateView view;
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 20));
    view.sparkOutputs.emplace(tx.inCoinIDs[1], EligibleOutput(tx.inCoinIDs[1], 30));
    return view;
}

} // namespace

BOOST_AUTO_TEST_SUITE(helsing_skeleton_tests)

BOOST_AUTO_TEST_CASE(rejects_empty_incoinids)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    tx.inCoinIDs.clear();

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::EMPTY_INCOINIDS);
}

BOOST_AUTO_TEST_CASE(rejects_unsorted_or_duplicate_incoinids)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    tx.inCoinIDs = {Output(2, 0), Output(1, 0)};
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.inCoinIDs = {Output(1, 0), Output(1, 0)};
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
}

BOOST_AUTO_TEST_CASE(rejects_tag_conflicts)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    state.AddSpentTag(tx.T, 101);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);

    state.Reset();
    helsing::StakeRecord record;
    record.stake_id = DeterministicHash(99);
    record.T = tx.T;
    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);

    state.Reset();
    view.blockSpentTags.insert(tx.T);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(state_rejects_default_tag)
{
    helsing::CHelsingState state;
    helsing::StakeRecord record;
    record.stake_id = DeterministicHash(99);
    record.T = GroupElement();

    BOOST_CHECK(!state.AddActiveStake(record));
    BOOST_CHECK(!state.AddSpentTag(record.T, 101));
}

BOOST_AUTO_TEST_CASE(rejects_missing_or_ineligible_outputs)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs.erase(tx.inCoinIDs[0]);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);
}

BOOST_AUTO_TEST_CASE(accepts_structural_stub)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_SUITE_END()
