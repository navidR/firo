// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/validation.h"
#include "hash.h"
#include "libspark/mint_transaction.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "streams.h"
#include "test/test_bitcoin.h"
#include "utilstrencodings.h"
#include "version.h"

#include <boost/test/unit_test.hpp>

#include <limits>
#include <map>
#include <unordered_set>

namespace {

GroupElement DeterministicPoint(unsigned char tag)
{
    unsigned char seed[32] = {};
    seed[0] = tag;

    GroupElement point;
    point.generate(seed);
    return point;
}

GroupElement NonMemberPoint()
{
    GroupElement point("1", "1");
    BOOST_REQUIRE(!point.isInfinity());
    BOOST_REQUIRE(!point.isMember());
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

helsing::StakeRecord ActiveRecord(unsigned char stakeTag, const GroupElement& tag)
{
    helsing::StakeRecord record;
    record.stake_id = DeterministicHash(stakeTag);
    record.T = tag;
    record.m.bytes = {0x6d, stakeTag};
    record.nHeight = 100 + stakeTag;
    record.nLastUpdateHeight = record.nHeight;
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

void AddOutputs(helsing::ValidationStateView& view, const helsing::StakeTx& tx, unsigned char pointTag)
{
    for (const helsing::OutputId& output_id : tx.inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        pointTag += 10;
    }
}

void CheckOutputIdEqual(const helsing::OutputId& a, const helsing::OutputId& b)
{
    BOOST_CHECK(a == b);
    BOOST_CHECK(!(a != b));
    BOOST_CHECK(a.txid == b.txid);
    BOOST_CHECK_EQUAL(a.vout, b.vout);
}

void CheckStakeTxEqual(const helsing::StakeTx& a, const helsing::StakeTx& b)
{
    BOOST_CHECK_EQUAL(a.inCoinIDs.size(), b.inCoinIDs.size());
    for (size_t i = 0; i < a.inCoinIDs.size() && i < b.inCoinIDs.size(); ++i) {
        CheckOutputIdEqual(a.inCoinIDs[i], b.inCoinIDs[i]);
    }
    BOOST_CHECK(a.S_prime == b.S_prime);
    BOOST_CHECK(a.C_prime == b.C_prime);
    BOOST_CHECK(a.T == b.T);
    BOOST_CHECK(a.m.bytes == b.m.bytes);
    BOOST_CHECK(a.pi_par.bytes == b.pi_par.bytes);
    BOOST_CHECK(a.pi_val.bytes == b.pi_val.bytes);
    BOOST_CHECK(a.pi_tag.bytes == b.pi_tag.bytes);
}

void CheckStakeUpdateTxEqual(const helsing::StakeUpdateTx& a, const helsing::StakeUpdateTx& b)
{
    BOOST_CHECK(a.stake_id == b.stake_id);
    BOOST_CHECK(a.m_new.bytes == b.m_new.bytes);
    BOOST_CHECK(a.sig_update.bytes == b.sig_update.bytes);
}

void CheckPayoutTxSkeletonEqual(const helsing::PayoutTxSkeleton& a, const helsing::PayoutTxSkeleton& b)
{
    BOOST_CHECK(a.selected_stake_id == b.selected_stake_id);
    BOOST_CHECK_EQUAL(a.payout_index, b.payout_index);
    BOOST_CHECK(a.addr_pk.bytes == b.addr_pk.bytes);
    BOOST_CHECK_EQUAL(a.V_PAYOUT, b.V_PAYOUT);
    BOOST_CHECK(a.coin.bytes == b.coin.bytes);
}

void CheckPayoutBlockContextSkeletonEqual(const helsing::PayoutBlockContextSkeleton& a, const helsing::PayoutBlockContextSkeleton& b)
{
    BOOST_CHECK(a.chain_id == b.chain_id);
    BOOST_CHECK_EQUAL(a.block_height, b.block_height);
    BOOST_CHECK(a.prev_block_hash == b.prev_block_hash);
    BOOST_CHECK_EQUAL(a.payout_index, b.payout_index);
    BOOST_CHECK(a.selected_stake_id == b.selected_stake_id);
}

template <typename T>
std::string WireHex(const T& obj)
{
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << obj;
    return HexStr(stream);
}

template <typename T>
uint256 WireHash(const T& obj)
{
    return SerializeHash(obj, SER_NETWORK, PROTOCOL_VERSION);
}

std::vector<unsigned char> SparkSerialContext(unsigned char tag)
{
    return {0x68, tag};
}

struct SparkOutputFixture {
    CScript script;
    spark::Coin coin;
};

SparkOutputFixture SparkMintOutput(uint64_t value, unsigned char tag)
{
    auto params = spark::Params::get_default();
    const spark::SpendKey spend_key(params);
    const spark::FullViewKey full_view_key(spend_key);
    const spark::IncomingViewKey incoming_view_key(full_view_key);
    const spark::Address address(incoming_view_key, tag);

    spark::MintedCoinData mintedCoin;
    mintedCoin.address = address;
    mintedCoin.v = value;
    mintedCoin.memo = "helsing mint";

    spark::MintTransaction sparkMint(params, {mintedCoin}, SparkSerialContext(tag));
    std::vector<CDataStream> serializedCoins = sparkMint.getMintedCoinsSerialized();

    SparkOutputFixture fixture;
    fixture.script << OP_SPARKMINT;
    fixture.script.insert(fixture.script.end(), serializedCoins[0].begin(), serializedCoins[0].end());

    std::vector<spark::Coin> coins;
    sparkMint.getCoins(coins);
    fixture.coin = coins[0];
    return fixture;
}

SparkOutputFixture SparkSMintOutput(uint64_t value, unsigned char tag)
{
    auto params = spark::Params::get_default();
    const spark::SpendKey spend_key(params);
    const spark::FullViewKey full_view_key(spend_key);
    const spark::IncomingViewKey incoming_view_key(full_view_key);
    const spark::Address address(incoming_view_key, tag);

    SparkOutputFixture fixture;
    fixture.coin = spark::Coin(params, spark::COIN_TYPE_SPEND, Scalar(uint64_t(tag) + uint64_t(1)), address, value, "helsing smint", SparkSerialContext(tag));

    CDataStream serialized(SER_NETWORK, PROTOCOL_VERSION);
    serialized << fixture.coin;
    fixture.script << OP_SPARKSMINT;
    fixture.script.insert(fixture.script.end(), serialized.begin(), serialized.end());
    return fixture;
}

void CheckSparkRecordMatchesCoin(const helsing::SparkOutputRecord& record, const helsing::OutputId& output_id, const spark::Coin& coin, int nHeight, helsing::SparkOutputType type)
{
    CheckOutputIdEqual(record.output_id, output_id);
    BOOST_CHECK(record.S == coin.S);
    BOOST_CHECK(record.C == coin.C);
    BOOST_CHECK(record.K == coin.K);
    BOOST_CHECK_EQUAL(record.nHeight, nHeight);
    BOOST_CHECK(record.type == type);
    BOOST_CHECK(!record.helsing_eligible);
    BOOST_CHECK(helsing::IsValidSparkOutputRecord(record));
}

} // namespace

BOOST_AUTO_TEST_SUITE(helsing_skeleton_tests)

BOOST_AUTO_TEST_CASE(output_id_equality_ordering_and_serialization)
{
    const helsing::OutputId tx1v0 = Output(1, 0);
    const helsing::OutputId tx1v1 = Output(1, 1);
    const helsing::OutputId tx1vMax = Output(1, std::numeric_limits<uint32_t>::max());
    const helsing::OutputId tx2v0 = Output(2, 0);

    CheckOutputIdEqual(tx1v0, Output(1, 0));
    BOOST_CHECK(tx1v0 != tx1v1);
    BOOST_CHECK(tx1v0 != tx2v0);
    BOOST_CHECK(tx1v0 < tx1v1);
    BOOST_CHECK(tx1v1 < tx1vMax);
    BOOST_CHECK(tx1v1 < tx2v0);
    BOOST_CHECK(!(tx2v0 < tx1v0));

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx1v0 << tx1vMax;

    helsing::OutputId decodedZero;
    helsing::OutputId decodedMax;
    stream >> decodedZero >> decodedMax;

    CheckOutputIdEqual(decodedZero, tx1v0);
    CheckOutputIdEqual(decodedMax, tx1vMax);
}

BOOST_AUTO_TEST_CASE(output_id_map_key_uses_txid_and_vout)
{
    std::map<helsing::OutputId, int> index;

    BOOST_CHECK(index.emplace(Output(1, 0), 10).second);
    BOOST_CHECK(index.emplace(Output(1, 1), 11).second);
    BOOST_CHECK(index.emplace(Output(2, 0), 20).second);
    BOOST_CHECK(!index.emplace(Output(1, 0), 99).second);

    BOOST_CHECK_EQUAL(index.size(), 3U);
    BOOST_CHECK_EQUAL(index.at(Output(1, 0)), 10);
    BOOST_CHECK_EQUAL(index.at(Output(1, 1)), 11);
    BOOST_CHECK_EQUAL(index.at(Output(2, 0)), 20);
    BOOST_CHECK(index.find(helsing::OutputId(Output(1, 0).txid, 2)) == index.end());
}

BOOST_AUTO_TEST_CASE(sorted_distinct_helper_covers_boundaries)
{
    BOOST_CHECK(!helsing::IsStrictlySortedAndDistinct({}));
    BOOST_CHECK(helsing::IsStrictlySortedAndDistinct({Output(1, 0)}));
    BOOST_CHECK(helsing::IsStrictlySortedAndDistinct({Output(1, 0), Output(1, 1), Output(2, 0)}));

    BOOST_CHECK(!helsing::IsStrictlySortedAndDistinct({Output(1, 0), Output(1, 0)}));
    BOOST_CHECK(!helsing::IsStrictlySortedAndDistinct({Output(1, 1), Output(1, 0)}));
    BOOST_CHECK(!helsing::IsStrictlySortedAndDistinct({Output(2, 0), Output(1, 1)}));
}

BOOST_AUTO_TEST_CASE(cover_set_cardinality_requires_exact_public_power)
{
    BOOST_CHECK(helsing::IsValidCoverSetCardinality(8, 2, 3));
    BOOST_CHECK(helsing::IsValidCoverSetCardinality(9, 3, 2));
    BOOST_CHECK(helsing::IsValidCoverSetCardinality(16, 4, 2));

    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(0, 2, 3));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(7, 2, 3));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(9, 2, 3));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(8, 0, 3));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(8, 1, 3));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(8, 2, 0));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(8, 2, 1));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(1, 2, 0));
    BOOST_CHECK(!helsing::IsValidCoverSetCardinality(std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max(), 2));
}

BOOST_AUTO_TEST_CASE(payout_maturity_helper_checks_revised_spec_inequality)
{
    BOOST_CHECK(helsing::IsStakeMatureForPayout(100, 110, 10));
    BOOST_CHECK(helsing::IsStakeMatureForPayout(100, 100, 0));
    BOOST_CHECK(helsing::IsStakeMatureForPayout(0, 0, 0));
    BOOST_CHECK(helsing::IsStakeMatureForPayout(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 0));

    BOOST_CHECK(!helsing::IsStakeMatureForPayout(100, 109, 10));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(100, 99, 0));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(-1, 100, 0));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(100, -1, 0));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(100, 110, -1));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 1));
    BOOST_CHECK(!helsing::IsStakeMatureForPayout(std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max(), 2));
}

BOOST_AUTO_TEST_CASE(value_range_helper_checks_revised_spec_integer_domain)
{
    BOOST_CHECK(helsing::IsHelsingValueInRange(0, 1));
    BOOST_CHECK(helsing::IsHelsingValueInRange(1, 2));
    BOOST_CHECK(helsing::IsHelsingValueInRange(MAX_MONEY - 1, MAX_MONEY));
    BOOST_CHECK(helsing::IsHelsingValueInRange(std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max()));

    BOOST_CHECK(!helsing::IsHelsingValueInRange(-1, 1));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(1, 1));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(2, 1));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(0, 0));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(0, -1));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::max()));
    BOOST_CHECK(!helsing::IsHelsingValueInRange(std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max()));
}

BOOST_AUTO_TEST_CASE(expected_payout_amount_helper_checks_equality_and_range)
{
    BOOST_CHECK(helsing::IsExpectedPayoutAmountInRangeSkeleton(0, 0, 1));
    BOOST_CHECK(helsing::IsExpectedPayoutAmountInRangeSkeleton(1, 1, 2));
    BOOST_CHECK(helsing::IsExpectedPayoutAmountInRangeSkeleton(MAX_MONEY - 1, MAX_MONEY - 1, MAX_MONEY));
    BOOST_CHECK(helsing::IsExpectedPayoutAmountInRangeSkeleton(std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max()));

    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(0, 1, 2));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(1, 0, 2));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(-1, -1, 1));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(1, 1, 1));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(0, 0, 0));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(0, 0, -1));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max()));
    BOOST_CHECK(!helsing::IsExpectedPayoutAmountInRangeSkeleton(std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::max()));
}

BOOST_AUTO_TEST_CASE(payout_value_parameter_skeleton_checks_payoutverify_amount_domain)
{
    BOOST_CHECK(helsing::IsPayoutValueParameterInRangeSkeleton(0, 0, 1, true));
    BOOST_CHECK(helsing::IsPayoutValueParameterInRangeSkeleton(1, 1, 2, true));
    BOOST_CHECK(helsing::IsPayoutValueParameterInRangeSkeleton(MAX_MONEY - 1, MAX_MONEY - 1, MAX_MONEY, true));
    BOOST_CHECK(helsing::IsPayoutValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max(), true));

    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(0, 1, 2, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(1, 0, 2, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(0, 0, 1, false));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(-1, -1, 1, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(1, 1, 1, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(0, 0, 0, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(0, 0, -1, true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::max(), true));
    BOOST_CHECK(!helsing::IsPayoutValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), true));
}

BOOST_AUTO_TEST_CASE(value_parameter_skeleton_checks_stake_payout_and_scalar_order_bounds)
{
    BOOST_CHECK(helsing::AreHelsingValueParametersInRangeSkeleton(0, 0, 1, true));
    BOOST_CHECK(helsing::AreHelsingValueParametersInRangeSkeleton(1, 1, 2, true));
    BOOST_CHECK(helsing::AreHelsingValueParametersInRangeSkeleton(MAX_MONEY - 1, MAX_MONEY - 1, MAX_MONEY, true));
    BOOST_CHECK(helsing::AreHelsingValueParametersInRangeSkeleton(std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max(), true));

    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, 0, 1, false));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(-1, 0, 1, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, -1, 1, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(1, 0, 1, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, 1, 1, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, 0, 0, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, 0, -1, true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(std::numeric_limits<CAmount>::min(), 0, std::numeric_limits<CAmount>::max(), true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::max(), true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(std::numeric_limits<CAmount>::max(), 0, std::numeric_limits<CAmount>::max(), true));
    BOOST_CHECK(!helsing::AreHelsingValueParametersInRangeSkeleton(0, std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), true));
}

BOOST_AUTO_TEST_CASE(stake_value_parameter_skeleton_checks_stakeverify_value_domain)
{
    BOOST_CHECK(helsing::IsStakeValueParameterInRangeSkeleton(0, 1, true));
    BOOST_CHECK(helsing::IsStakeValueParameterInRangeSkeleton(1, 2, true));
    BOOST_CHECK(helsing::IsStakeValueParameterInRangeSkeleton(MAX_MONEY - 1, MAX_MONEY, true));
    BOOST_CHECK(helsing::IsStakeValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max(), true));

    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(0, 1, false));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(-1, 1, true));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(1, 1, true));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(0, 0, true));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(0, -1, true));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::min(), std::numeric_limits<CAmount>::max(), true));
    BOOST_CHECK(!helsing::IsStakeValueParameterInRangeSkeleton(std::numeric_limits<CAmount>::max(), std::numeric_limits<CAmount>::max(), true));
}

BOOST_AUTO_TEST_CASE(stake_verification_prefix_skeleton_accepts_steps_one_through_three)
{
    const helsing::StakeTx tx = ValidStakeTx();

    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, 0, 1, true, true) == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, MAX_MONEY - 1, MAX_MONEY, true, true) == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, std::numeric_limits<CAmount>::max() - 1, std::numeric_limits<CAmount>::max(), true, true) == helsing::StakeVerificationPrefixSkeletonResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_verification_prefix_skeleton_preserves_steps_one_through_three_precedence)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs.clear();
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, -1, 0, false, false) == helsing::StakeVerificationPrefixSkeletonResult::TX_INCOMPLETE);

    tx = ValidStakeTx();
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, -1, 0, false, false) == helsing::StakeVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL);

    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, 0, 1, false, true) == helsing::StakeVerificationPrefixSkeletonResult::INVALID_VALUE_PARAMETER);
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, -1, 1, true, true) == helsing::StakeVerificationPrefixSkeletonResult::INVALID_VALUE_PARAMETER);
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, 1, 1, true, true) == helsing::StakeVerificationPrefixSkeletonResult::INVALID_VALUE_PARAMETER);
    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, 0, 0, true, true) == helsing::StakeVerificationPrefixSkeletonResult::INVALID_VALUE_PARAMETER);
}

BOOST_AUTO_TEST_CASE(stake_verification_prefix_skeleton_does_not_define_canonical_or_proof_grammar)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {helsing::OutputId()};
    tx.S_prime = GroupElement();
    tx.C_prime = GroupElement();
    tx.T = GroupElement();
    tx.m.bytes = {0x00};
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0x00};
    tx.pi_tag.bytes = {0x00};

    BOOST_CHECK(helsing::CheckStakeVerificationPrefixSkeleton(tx, 0, 1, true, true) == helsing::StakeVerificationPrefixSkeletonResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_tag_state_skeleton_accepts_unused_parent_tag)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(60);
    const GroupElement activeOtherTag = DeterministicPoint(61);
    const GroupElement spentOtherTag = DeterministicPoint(62);

    BOOST_CHECK(state.AddActiveStake(ActiveRecord(63, activeOtherTag)));
    BOOST_CHECK(state.AddSpentTag(spentOtherTag, 200));

    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(tag, state) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_tag_state_skeleton_rejects_prior_spent_tag)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(64);
    const GroupElement activeOtherTag = DeterministicPoint(65);

    BOOST_CHECK(state.AddSpentTag(tag, 201));
    BOOST_CHECK(state.AddActiveStake(ActiveRecord(66, activeOtherTag)));

    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(tag, state) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
}

BOOST_AUTO_TEST_CASE(stake_tag_state_skeleton_rejects_active_tag)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(67);
    const GroupElement spentOtherTag = DeterministicPoint(68);

    BOOST_CHECK(state.AddActiveStake(ActiveRecord(69, tag)));
    BOOST_CHECK(state.AddSpentTag(spentOtherTag, 202));

    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(tag, state) == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);
}

BOOST_AUTO_TEST_CASE(stake_tag_state_skeleton_does_not_mutate_state)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(70);
    const GroupElement spentTag = DeterministicPoint(71);
    const GroupElement freshTag = DeterministicPoint(72);

    BOOST_CHECK(state.AddActiveStake(ActiveRecord(73, activeTag)));
    BOOST_CHECK(state.AddSpentTag(spentTag, 203));

    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(freshTag, state) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(activeTag, state) == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);
    BOOST_CHECK(helsing::CheckStakeTagStateSkeleton(spentTag, state) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);

    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentTag));
    BOOST_CHECK(!state.IsActiveTag(freshTag));
    BOOST_CHECK(!state.IsSpentTag(freshTag));
}

BOOST_AUTO_TEST_CASE(stake_context_validation_skeleton_accepts_caller_validated_context)
{
    helsing::StakeContext context;
    context.bytes = {0x6d, 0x61, 0x73, 0x74, 0x65, 0x72};

    BOOST_CHECK(helsing::IsStakeContextValidSkeleton(context, true, true, true, true));
}

BOOST_AUTO_TEST_CASE(stake_context_validation_skeleton_rejects_empty_or_missing_required_facts)
{
    helsing::StakeContext context;
    context.bytes = {0x6d};

    helsing::StakeContext emptyContext;
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(emptyContext, true, true, true, true));
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(context, false, true, true, true));
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(context, true, false, true, true));
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(context, true, true, false, true));
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(context, true, true, true, false));
    BOOST_CHECK(!helsing::IsStakeContextValidSkeleton(emptyContext, false, false, false, false));
}

BOOST_AUTO_TEST_CASE(stake_context_validation_skeleton_does_not_define_context_grammar)
{
    helsing::StakeContext context;
    context.bytes = {0x00};
    BOOST_CHECK(helsing::IsStakeContextValidSkeleton(context, true, true, true, true));

    context.bytes = {0xff, 0x00, 0x6d, 0x01};
    BOOST_CHECK(helsing::IsStakeContextValidSkeleton(context, true, true, true, true));
}

BOOST_AUTO_TEST_CASE(stake_verification_context_skeleton_accepts_steps_one_through_five)
{
    const helsing::StakeTx tx = ValidStakeTx();
    helsing::CHelsingState state;
    BOOST_CHECK(state.AddActiveStake(ActiveRecord(74, DeterministicPoint(75))));
    BOOST_CHECK(state.AddSpentTag(DeterministicPoint(76), 204));

    const helsing::StakeVerificationContextSkeletonResult result = helsing::CheckStakeVerificationContextSkeleton(tx, state, MAX_MONEY - 1, MAX_MONEY, true, true, true, true, true, true);

    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_valid);
}

BOOST_AUTO_TEST_CASE(stake_verification_context_skeleton_preserves_steps_one_through_five_precedence)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::CHelsingState state;
    BOOST_CHECK(state.AddSpentTag(tx.T, 205));

    tx.inCoinIDs.clear();
    helsing::StakeVerificationContextSkeletonResult result = helsing::CheckStakeVerificationContextSkeleton(tx, state, -1, 0, false, false, false, false, false, false);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::TX_INCOMPLETE);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_valid);

    tx = ValidStakeTx();
    result = helsing::CheckStakeVerificationContextSkeleton(tx, state, -1, 0, false, false, false, false, false, false);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_valid);

    result = helsing::CheckStakeVerificationContextSkeleton(tx, state, 0, 1, true, true, false, false, false, false);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
    BOOST_CHECK(result.context_valid);

    state.Reset();
    BOOST_CHECK(state.AddActiveStake(ActiveRecord(77, tx.T)));
    result = helsing::CheckStakeVerificationContextSkeleton(tx, state, 0, 1, true, true, false, false, false, false);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);
    BOOST_CHECK(result.context_valid);

    state.Reset();
    result = helsing::CheckStakeVerificationContextSkeleton(tx, state, 0, 1, true, true, true, true, false, true);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(!result.context_valid);
}

BOOST_AUTO_TEST_CASE(stake_verification_context_skeleton_stops_before_cover_sets_and_proofs)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {helsing::OutputId()};
    tx.S_prime = GroupElement();
    tx.C_prime = GroupElement();
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0xff};
    tx.pi_tag.bytes = {0x00, 0xff};

    helsing::CHelsingState state;
    const helsing::StakeVerificationContextSkeletonResult result = helsing::CheckStakeVerificationContextSkeleton(tx, state, 0, 1, true, true, true, true, true, true);

    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_valid);
}

BOOST_AUTO_TEST_CASE(stake_verification_context_skeleton_does_not_mutate_state)
{
    const helsing::StakeTx tx = ValidStakeTx();
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(78);
    const GroupElement spentTag = DeterministicPoint(79);

    BOOST_CHECK(state.AddActiveStake(ActiveRecord(80, activeTag)));
    BOOST_CHECK(state.AddSpentTag(spentTag, 206));

    const helsing::StakeVerificationContextSkeletonResult result = helsing::CheckStakeVerificationContextSkeleton(tx, state, 0, 1, true, true, true, true, true, true);
    BOOST_CHECK(result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_valid);

    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentTag));
    BOOST_CHECK(!state.IsActiveTag(tx.T));
    BOOST_CHECK(!state.IsSpentTag(tx.T));
}

BOOST_AUTO_TEST_CASE(stake_cover_set_identifier_skeleton_accepts_public_power_sorted_distinct_ids)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};

    BOOST_CHECK(helsing::CheckStakeCoverSetIdentifiersSkeleton(inCoinIDs, 2, 2) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_cover_set_identifier_skeleton_preserves_step_six_precedence)
{
    BOOST_CHECK(helsing::CheckStakeCoverSetIdentifiersSkeleton({Output(3, 0), Output(1, 0), Output(1, 0)}, 2, 2) == helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY);
    BOOST_CHECK(helsing::CheckStakeCoverSetIdentifiersSkeleton({Output(1, 0), Output(1, 0), Output(2, 0), Output(3, 0)}, 2, 2) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
    BOOST_CHECK(helsing::CheckStakeCoverSetIdentifiersSkeleton({Output(2, 0), Output(1, 1), Output(3, 0), Output(4, 0)}, 2, 2) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
}

BOOST_AUTO_TEST_CASE(stake_cover_set_identifier_skeleton_stops_before_output_lookup)
{
    const std::vector<helsing::OutputId> inCoinIDs = {helsing::OutputId(), Output(1, 0), Output(2, 0), Output(3, 0)};

    BOOST_CHECK(helsing::CheckStakeCoverSetIdentifiersSkeleton(inCoinIDs, 2, 2) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_verification_cover_set_skeleton_accepts_steps_one_through_six)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};

    helsing::CHelsingState state;
    const helsing::StakeVerificationCoverSetSkeletonResult result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, true, true, true, true, 2, 2);

    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_verification_cover_set_skeleton_preserves_steps_one_through_six_precedence)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(2, 0), Output(1, 0), Output(1, 0)};
    helsing::CHelsingState state;

    helsing::StakeVerificationCoverSetSkeletonResult result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, -1, 0, false, false, false, false, false, false, 2, 2);
    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::OK);

    BOOST_CHECK(state.AddSpentTag(tx.T, 207));
    result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, false, false, false, false, 2, 2);
    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::OK);

    state.Reset();
    result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, false, true, true, true, 2, 2);
    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(!result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::OK);

    result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, true, true, true, true, 2, 2);
    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY);

    tx.inCoinIDs = {Output(2, 0), Output(1, 0), Output(3, 0), Output(4, 0)};
    result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, true, true, true, true, 2, 2);
    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
}

BOOST_AUTO_TEST_CASE(stake_verification_cover_set_skeleton_stops_before_outputs_and_proofs)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {helsing::OutputId(), Output(1, 0), Output(2, 0), Output(3, 0)};
    tx.S_prime = GroupElement();
    tx.C_prime = GroupElement();
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0xff};
    tx.pi_tag.bytes = {0x00, 0xff};

    helsing::CHelsingState state;
    const helsing::StakeVerificationCoverSetSkeletonResult result = helsing::CheckStakeVerificationCoverSetSkeleton(tx, state, 0, 1, true, true, true, true, true, true, 2, 2);

    BOOST_CHECK(result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_cover_set_output_rules_skeleton_accepts_existing_eligible_outputs_with_spark_rules)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    unsigned char pointTag = 81;
    for (const helsing::OutputId& output_id : inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        sparkRules.emplace(output_id, true);
        pointTag += 3;
    }

    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_cover_set_output_rules_skeleton_preserves_step_seven_precedence)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    view.sparkOutputs.emplace(inCoinIDs[1], EligibleOutput(inCoinIDs[1], 84));
    view.sparkOutputs[inCoinIDs[1]].helsing_eligible = false;
    sparkRules.emplace(inCoinIDs[1], false);
    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    view.sparkOutputs.emplace(inCoinIDs[0], EligibleOutput(inCoinIDs[0], 87));
    sparkRules.emplace(inCoinIDs[0], false);
    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OUTPUT_SPARK_RULES_FAILED);

    sparkRules[inCoinIDs[0]] = true;
    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);

    view.sparkOutputs[inCoinIDs[1]].helsing_eligible = true;
    sparkRules[inCoinIDs[1]] = true;
    view.sparkOutputs.emplace(inCoinIDs[2], EligibleOutput(inCoinIDs[2], 90));
    view.sparkOutputs.emplace(inCoinIDs[3], EligibleOutput(inCoinIDs[3], 93));
    sparkRules[inCoinIDs[2]] = true;
    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OUTPUT_SPARK_RULES_FAILED);

    sparkRules[inCoinIDs[3]] = false;
    BOOST_CHECK(helsing::CheckStakeCoverSetOutputRulesSkeleton(inCoinIDs, view, sparkRules) == helsing::StakeValidationResult::OUTPUT_SPARK_RULES_FAILED);
}

BOOST_AUTO_TEST_CASE(stake_verification_outputs_skeleton_accepts_steps_one_through_seven)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};

    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    unsigned char pointTag = 96;
    for (const helsing::OutputId& output_id : tx.inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        sparkRules.emplace(output_id, true);
        pointTag += 3;
    }

    const helsing::StakeVerificationOutputSkeletonResult result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);

    BOOST_CHECK(result.cover_set_result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.cover_set_result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.cover_set_result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result.cover_set_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_verification_outputs_skeleton_preserves_steps_one_through_seven_precedence)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(2, 0), Output(1, 0), Output(1, 0)};
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    helsing::StakeVerificationOutputSkeletonResult result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, -1, 0, false, false, false, false, false, false, 2, 2, sparkRules);
    BOOST_CHECK(result.cover_set_result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL);
    BOOST_CHECK(result.cover_set_result.cover_set_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);

    BOOST_CHECK(state.AddSpentTag(tx.T, 208));
    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, false, false, false, false, 2, 2, sparkRules);
    BOOST_CHECK(result.cover_set_result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.cover_set_result.context_result.tag_result == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);

    state.Reset();
    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, false, true, true, true, 2, 2, sparkRules);
    BOOST_CHECK(result.cover_set_result.context_result.tag_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(!result.cover_set_result.context_result.context_valid);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);

    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);
    BOOST_CHECK(result.cover_set_result.context_result.context_valid);
    BOOST_CHECK(result.cover_set_result.cover_set_result == helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);

    tx.inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 99));
    sparkRules.emplace(tx.inCoinIDs[0], true);
    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);
    BOOST_CHECK(result.cover_set_result.cover_set_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    view.sparkOutputs.emplace(tx.inCoinIDs[1], EligibleOutput(tx.inCoinIDs[1], 102));
    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = false;
    sparkRules.emplace(tx.inCoinIDs[1], false);
    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);

    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = true;
    view.sparkOutputs.emplace(tx.inCoinIDs[2], EligibleOutput(tx.inCoinIDs[2], 105));
    view.sparkOutputs.emplace(tx.inCoinIDs[3], EligibleOutput(tx.inCoinIDs[3], 108));
    sparkRules[tx.inCoinIDs[1]] = true;
    sparkRules[tx.inCoinIDs[2]] = true;
    result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OUTPUT_SPARK_RULES_FAILED);
}

BOOST_AUTO_TEST_CASE(stake_verification_outputs_skeleton_stops_before_statement_hashes_and_proofs)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};
    tx.S_prime = GroupElement();
    tx.C_prime = GroupElement();
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0xff};
    tx.pi_tag.bytes = {0x00, 0xff};

    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    unsigned char pointTag = 111;
    for (const helsing::OutputId& output_id : tx.inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        sparkRules.emplace(output_id, true);
        pointTag += 3;
    }

    const helsing::StakeVerificationOutputSkeletonResult result = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);

    BOOST_CHECK(result.cover_set_result.context_result.prefix_result == helsing::StakeVerificationPrefixSkeletonResult::OK);
    BOOST_CHECK(result.cover_set_result.cover_set_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.output_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_proof_verification_skeleton_blocks_after_valid_prefix)
{
    helsing::StakeVerificationOutputSkeletonResult prefix;
    prefix.cover_set_result.context_result.prefix_result = helsing::StakeVerificationPrefixSkeletonResult::OK;
    prefix.cover_set_result.context_result.tag_result = helsing::StakeValidationResult::OK;
    prefix.cover_set_result.context_result.context_valid = true;
    prefix.cover_set_result.cover_set_result = helsing::StakeValidationResult::OK;
    prefix.output_result = helsing::StakeValidationResult::OK;

    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STATEMENT_HASHING_UNIMPLEMENTED);
}

BOOST_AUTO_TEST_CASE(stake_proof_verification_skeleton_preserves_prefix_failures)
{
    helsing::StakeVerificationOutputSkeletonResult prefix;
    prefix.cover_set_result.context_result.prefix_result = helsing::StakeVerificationPrefixSkeletonResult::TX_INCOMPLETE;
    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED);

    prefix.cover_set_result.context_result.prefix_result = helsing::StakeVerificationPrefixSkeletonResult::OK;
    prefix.cover_set_result.context_result.tag_result = helsing::StakeValidationResult::TAG_ALREADY_SPENT;
    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED);

    prefix.cover_set_result.context_result.tag_result = helsing::StakeValidationResult::OK;
    prefix.cover_set_result.context_result.context_valid = false;
    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED);

    prefix.cover_set_result.context_result.context_valid = true;
    prefix.cover_set_result.cover_set_result = helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY;
    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED);

    prefix.cover_set_result.cover_set_result = helsing::StakeValidationResult::OK;
    prefix.output_result = helsing::StakeValidationResult::OUTPUT_NOT_FOUND;
    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED);
}

BOOST_AUTO_TEST_CASE(stake_proof_verification_skeleton_does_not_fake_proof_acceptance)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(1, 0), Output(1, 1), Output(2, 0), Output(3, 0)};
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0x01};
    tx.pi_tag.bytes = {0x02};

    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    std::map<helsing::OutputId, bool> sparkRules;

    unsigned char pointTag = 114;
    for (const helsing::OutputId& output_id : tx.inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        sparkRules.emplace(output_id, true);
        pointTag += 3;
    }

    const helsing::StakeVerificationOutputSkeletonResult prefix = helsing::CheckStakeVerificationOutputsSkeleton(tx, state, view, 0, 1, true, true, true, true, true, true, 2, 2, sparkRules);
    BOOST_REQUIRE(prefix.output_result == helsing::StakeValidationResult::OK);

    BOOST_CHECK(helsing::CheckStakeProofVerificationSkeleton(prefix) == helsing::StakeProofVerificationSkeletonResult::STATEMENT_HASHING_UNIMPLEMENTED);
}

BOOST_AUTO_TEST_CASE(stake_tx_completeness_skeleton_accepts_populated_fields)
{
    const helsing::StakeTx tx = ValidStakeTx();

    BOOST_CHECK(helsing::IsCompleteStakeTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(stake_tx_completeness_skeleton_rejects_missing_fields)
{
    helsing::StakeTx tx;
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(tx));

    tx.inCoinIDs = {helsing::OutputId()};
    tx.m.bytes = {0x6d};
    tx.pi_par.bytes = {0x01};
    tx.pi_val.bytes = {0x02};
    tx.pi_tag.bytes = {0x03};
    BOOST_REQUIRE(helsing::IsCompleteStakeTxSkeleton(tx));

    helsing::StakeTx changed = tx;
    changed.inCoinIDs.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(changed));

    changed = tx;
    changed.m.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(changed));

    changed = tx;
    changed.pi_par.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(changed));

    changed = tx;
    changed.pi_val.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(changed));

    changed = tx;
    changed.pi_tag.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeTxSkeleton(changed));
}

BOOST_AUTO_TEST_CASE(stake_tx_completeness_skeleton_does_not_define_context_or_proof_grammar)
{
    helsing::StakeTx tx;
    tx.inCoinIDs = {helsing::OutputId()};
    tx.m.bytes = {0x00};
    tx.pi_par.bytes = {0x00};
    tx.pi_val.bytes = {0x00};
    tx.pi_tag.bytes = {0x00};
    BOOST_CHECK(helsing::IsCompleteStakeTxSkeleton(tx));

    tx.m.bytes = {0xff, 0x00, 0x6d};
    tx.pi_par.bytes = {0xff, 0x00, 0x01};
    tx.pi_val.bytes = {0xff, 0x00, 0x02};
    tx.pi_tag.bytes = {0xff, 0x00, 0x03};
    BOOST_CHECK(helsing::IsCompleteStakeTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(payout_tx_completeness_skeleton_accepts_populated_fields)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(232);
    tx.payout_index = std::numeric_limits<uint32_t>::max();
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    tx.V_PAYOUT = 0;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};

    BOOST_CHECK(helsing::IsCompletePayoutTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(payout_tx_completeness_skeleton_rejects_missing_fields)
{
    helsing::PayoutTxSkeleton tx;
    BOOST_CHECK(!helsing::IsCompletePayoutTxSkeleton(tx));

    tx.selected_stake_id = DeterministicHash(233);
    tx.addr_pk.bytes = {0x61};
    tx.coin.bytes = {0x63};
    BOOST_REQUIRE(helsing::IsCompletePayoutTxSkeleton(tx));

    helsing::PayoutTxSkeleton changed = tx;
    changed.selected_stake_id.SetNull();
    BOOST_CHECK(!helsing::IsCompletePayoutTxSkeleton(changed));

    changed = tx;
    changed.addr_pk.bytes.clear();
    BOOST_CHECK(!helsing::IsCompletePayoutTxSkeleton(changed));

    changed = tx;
    changed.coin.bytes.clear();
    BOOST_CHECK(!helsing::IsCompletePayoutTxSkeleton(changed));
}

BOOST_AUTO_TEST_CASE(payout_tx_completeness_skeleton_does_not_define_blob_grammar)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(234);
    tx.addr_pk.bytes = {0x00};
    tx.coin.bytes = {0x00};
    BOOST_CHECK(helsing::IsCompletePayoutTxSkeleton(tx));

    tx.addr_pk.bytes = {0xff, 0x00, 0x61};
    tx.coin.bytes = {0xff, 0x00, 0x63};
    BOOST_CHECK(helsing::IsCompletePayoutTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(stake_update_completeness_skeleton_accepts_populated_fields)
{
    helsing::StakeUpdateTx tx;
    tx.stake_id = DeterministicHash(229);
    tx.m_new.bytes = {0x6d, 0x5f, 0x6e, 0x65, 0x77};
    tx.sig_update.bytes = {0x73, 0x69, 0x67};

    BOOST_CHECK(helsing::IsCompleteStakeUpdateTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(stake_update_completeness_skeleton_rejects_missing_fields)
{
    helsing::StakeUpdateTx tx;
    BOOST_CHECK(!helsing::IsCompleteStakeUpdateTxSkeleton(tx));

    tx.stake_id = DeterministicHash(230);
    tx.m_new.bytes = {0x6d};
    tx.sig_update.bytes = {0x73};
    BOOST_REQUIRE(helsing::IsCompleteStakeUpdateTxSkeleton(tx));

    helsing::StakeUpdateTx changed = tx;
    changed.stake_id.SetNull();
    BOOST_CHECK(!helsing::IsCompleteStakeUpdateTxSkeleton(changed));

    changed = tx;
    changed.m_new.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeUpdateTxSkeleton(changed));

    changed = tx;
    changed.sig_update.bytes.clear();
    BOOST_CHECK(!helsing::IsCompleteStakeUpdateTxSkeleton(changed));
}

BOOST_AUTO_TEST_CASE(stake_update_completeness_skeleton_does_not_define_context_or_signature_grammar)
{
    helsing::StakeUpdateTx tx;
    tx.stake_id = DeterministicHash(231);
    tx.m_new.bytes = {0x00};
    tx.sig_update.bytes = {0x00};
    BOOST_CHECK(helsing::IsCompleteStakeUpdateTxSkeleton(tx));

    tx.m_new.bytes = {0xff, 0x00, 0x6d};
    tx.sig_update.bytes = {0xff, 0x00, 0x73};
    BOOST_CHECK(helsing::IsCompleteStakeUpdateTxSkeleton(tx));
}

BOOST_AUTO_TEST_CASE(payout_address_match_skeleton_compares_extracted_registered_address)
{
    helsing::PayoutTxSkeleton tx;
    helsing::PayoutAddressBlob registeredAddress;
    tx.selected_stake_id = DeterministicHash(225);
    tx.payout_index = 3;
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    tx.V_PAYOUT = 42;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};
    registeredAddress.bytes = tx.addr_pk.bytes;

    BOOST_CHECK(helsing::DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress));

    helsing::PayoutTxSkeleton changed = tx;
    changed.selected_stake_id = DeterministicHash(226);
    changed.payout_index = 4;
    changed.V_PAYOUT = 43;
    changed.coin.bytes = {0x64, 0x69, 0x66, 0x66};
    BOOST_CHECK(helsing::DoesPayoutAddressMatchRegisteredSkeleton(changed, registeredAddress));
}

BOOST_AUTO_TEST_CASE(payout_address_match_skeleton_rejects_mismatch_and_empty_blobs)
{
    helsing::PayoutTxSkeleton tx;
    helsing::PayoutAddressBlob registeredAddress;
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    registeredAddress.bytes = {0x64, 0x69, 0x66, 0x66};

    BOOST_CHECK(!helsing::DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress));

    registeredAddress.bytes = tx.addr_pk.bytes;
    tx.addr_pk.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress));

    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    registeredAddress.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress));

    tx.addr_pk.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress));
}

BOOST_AUTO_TEST_CASE(payout_stake_match_skeleton_compares_recomputed_selection)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(251);
    tx.payout_index = 19;
    tx.addr_pk.bytes = {0x61};
    tx.V_PAYOUT = 46;
    tx.coin.bytes = {0x63};

    BOOST_CHECK(helsing::DoesPayoutStakeMatchExpectedSkeleton(tx, tx.selected_stake_id));

    helsing::PayoutTxSkeleton changed = tx;
    changed.payout_index = 20;
    changed.addr_pk.bytes = {0x64};
    changed.V_PAYOUT = 47;
    changed.coin.bytes = {0x64};
    BOOST_CHECK(helsing::DoesPayoutStakeMatchExpectedSkeleton(changed, tx.selected_stake_id));
}

BOOST_AUTO_TEST_CASE(payout_stake_match_skeleton_rejects_mismatch_and_null_ids)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(252);
    const uint256 expectedStakeId = DeterministicHash(253);

    BOOST_CHECK(!helsing::DoesPayoutStakeMatchExpectedSkeleton(tx, expectedStakeId));

    BOOST_CHECK(helsing::DoesPayoutStakeMatchExpectedSkeleton(tx, tx.selected_stake_id));

    tx.selected_stake_id.SetNull();
    BOOST_CHECK(!helsing::DoesPayoutStakeMatchExpectedSkeleton(tx, expectedStakeId));

    tx.selected_stake_id = DeterministicHash(254);
    BOOST_CHECK(!helsing::DoesPayoutStakeMatchExpectedSkeleton(tx, uint256()));
}

BOOST_AUTO_TEST_CASE(payout_coin_match_skeleton_compares_recomputed_coin)
{
    helsing::PayoutTxSkeleton tx;
    helsing::PayoutCoinBlob expectedCoin;
    tx.selected_stake_id = DeterministicHash(227);
    tx.payout_index = 5;
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    tx.V_PAYOUT = 44;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};
    expectedCoin.bytes = tx.coin.bytes;

    BOOST_CHECK(helsing::DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin));

    helsing::PayoutTxSkeleton changed = tx;
    changed.selected_stake_id = DeterministicHash(228);
    changed.payout_index = 6;
    changed.addr_pk.bytes = {0x64, 0x69, 0x66, 0x66};
    changed.V_PAYOUT = 45;
    BOOST_CHECK(helsing::DoesPayoutCoinMatchExpectedSkeleton(changed, expectedCoin));
}

BOOST_AUTO_TEST_CASE(payout_coin_match_skeleton_rejects_mismatch_and_empty_blobs)
{
    helsing::PayoutTxSkeleton tx;
    helsing::PayoutCoinBlob expectedCoin;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};
    expectedCoin.bytes = {0x64, 0x69, 0x66, 0x66};

    BOOST_CHECK(!helsing::DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin));

    expectedCoin.bytes = tx.coin.bytes;
    tx.coin.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin));

    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};
    expectedCoin.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin));

    tx.coin.bytes.clear();
    BOOST_CHECK(!helsing::DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin));
}

BOOST_AUTO_TEST_CASE(payout_public_field_skeleton_accepts_caller_supplied_expected_values)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(40);
    tx.payout_index = 21;
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    tx.V_PAYOUT = 48;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 600;
    context.prev_block_hash = DeterministicHash(41);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    helsing::PayoutAddressBlob registeredAddress;
    registeredAddress.bytes = tx.addr_pk.bytes;
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = tx.coin.bytes;

    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, context, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT, tx.V_PAYOUT + 1, expectedCoin) == helsing::PayoutPublicValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(payout_public_field_skeleton_reports_each_ordered_failure)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(42);
    tx.payout_index = 22;
    tx.addr_pk.bytes = {0x61};
    tx.V_PAYOUT = 49;
    tx.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 601;
    context.prev_block_hash = DeterministicHash(43);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    helsing::PayoutAddressBlob registeredAddress;
    registeredAddress.bytes = tx.addr_pk.bytes;
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = tx.coin.bytes;

    helsing::PayoutAddressBlob wrongAddress = registeredAddress;
    wrongAddress.bytes = {0x62};
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, context, wrongAddress, tx.selected_stake_id, tx.V_PAYOUT, tx.V_PAYOUT + 1, expectedCoin) == helsing::PayoutPublicValidationResult::ADDRESS_MISMATCH);

    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, context, registeredAddress, DeterministicHash(44), tx.V_PAYOUT, tx.V_PAYOUT + 1, expectedCoin) == helsing::PayoutPublicValidationResult::SELECTED_STAKE_MISMATCH);
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, context, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT + 1, tx.V_PAYOUT + 2, expectedCoin) == helsing::PayoutPublicValidationResult::INVALID_PAYOUT_AMOUNT);

    helsing::PayoutBlockContextSkeleton wrongContext = context;
    ++wrongContext.payout_index;
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, wrongContext, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT, tx.V_PAYOUT + 1, expectedCoin) == helsing::PayoutPublicValidationResult::INVALID_PAYOUT_ID_INPUTS);

    helsing::PayoutCoinBlob wrongCoin = expectedCoin;
    wrongCoin.bytes = {0x64};
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, context, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT, tx.V_PAYOUT + 1, wrongCoin) == helsing::PayoutPublicValidationResult::PAYOUT_COIN_MISMATCH);
}

BOOST_AUTO_TEST_CASE(payout_public_field_skeleton_preserves_section_eighteen_precedence)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(45);
    tx.payout_index = 23;
    tx.addr_pk.bytes = {0x61};
    tx.V_PAYOUT = 50;
    tx.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 602;
    context.prev_block_hash = DeterministicHash(46);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    helsing::PayoutAddressBlob registeredAddress;
    registeredAddress.bytes = tx.addr_pk.bytes;
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = tx.coin.bytes;

    helsing::PayoutAddressBlob wrongAddress = registeredAddress;
    wrongAddress.bytes = {0x62};
    helsing::PayoutBlockContextSkeleton wrongContext = context;
    ++wrongContext.payout_index;
    helsing::PayoutCoinBlob wrongCoin = expectedCoin;
    wrongCoin.bytes = {0x64};

    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, wrongContext, wrongAddress, DeterministicHash(47), tx.V_PAYOUT + 1, tx.V_PAYOUT, wrongCoin) == helsing::PayoutPublicValidationResult::ADDRESS_MISMATCH);
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, wrongContext, registeredAddress, DeterministicHash(47), tx.V_PAYOUT + 1, tx.V_PAYOUT, wrongCoin) == helsing::PayoutPublicValidationResult::SELECTED_STAKE_MISMATCH);
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, wrongContext, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT + 1, tx.V_PAYOUT, wrongCoin) == helsing::PayoutPublicValidationResult::INVALID_PAYOUT_AMOUNT);
    BOOST_CHECK(helsing::CheckPayoutPublicFieldsSkeleton(tx, wrongContext, registeredAddress, tx.selected_stake_id, tx.V_PAYOUT, tx.V_PAYOUT + 1, wrongCoin) == helsing::PayoutPublicValidationResult::INVALID_PAYOUT_ID_INPUTS);
}

BOOST_AUTO_TEST_CASE(stake_margin_helper_checks_integer_sum_before_scalar_conversion)
{
    BOOST_CHECK(helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(0, 0, 1));
    BOOST_CHECK(helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(1, 1, 3));
    BOOST_CHECK(helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(MAX_MONEY - 2, 1, MAX_MONEY));
    BOOST_CHECK(helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(std::numeric_limits<CAmount>::max() - 2, 1, std::numeric_limits<CAmount>::max()));

    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(-1, 0, 1));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(0, -1, 1));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(0, 0, 0));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(0, 0, -1));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(1, 0, 1));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(1, 1, 2));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(1, 2, 2));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(std::numeric_limits<CAmount>::max() - 1, 2, std::numeric_limits<CAmount>::max()));
    BOOST_CHECK(!helsing::IsHelsingStakeValueWithMarginInRangeSkeleton(std::numeric_limits<CAmount>::min(), 0, std::numeric_limits<CAmount>::max()));
}

BOOST_AUTO_TEST_CASE(helsing_eligible_output_candidate_requires_tag_revealing_spend_paths)
{
    helsing::SparkOutputRecord output = EligibleOutput(Output(52, 0), 60);
    output.helsing_eligible = false;

    BOOST_CHECK(helsing::IsHelsingEligibleOutputCandidateSkeleton(output, true));
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(output, false));

    output.helsing_eligible = true;
    BOOST_CHECK(helsing::IsHelsingEligibleOutputCandidateSkeleton(output, true));
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(output, false));
}

BOOST_AUTO_TEST_CASE(helsing_eligible_output_candidate_requires_valid_output_record)
{
    const helsing::SparkOutputRecord output = EligibleOutput(Output(53, 0), 70);

    helsing::SparkOutputRecord missingOutputId = output;
    missingOutputId.output_id = helsing::OutputId();
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(missingOutputId, true));

    helsing::SparkOutputRecord invalidPoint = output;
    invalidPoint.S = GroupElement();
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(invalidPoint, true));

    helsing::SparkOutputRecord negativeHeight = output;
    negativeHeight.nHeight = -1;
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(negativeHeight, true));

    helsing::SparkOutputRecord unknownType = output;
    unknownType.type = helsing::SparkOutputType::UNKNOWN;
    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(unknownType, true));
}

BOOST_AUTO_TEST_CASE(helsing_eligible_output_candidate_does_not_mutate_record)
{
    const helsing::SparkOutputRecord original = EligibleOutput(Output(54, 0), 80);
    helsing::SparkOutputRecord output = original;

    BOOST_CHECK(!helsing::IsHelsingEligibleOutputCandidateSkeleton(output, false));

    BOOST_CHECK(output.output_id == original.output_id);
    BOOST_CHECK(output.S == original.S);
    BOOST_CHECK(output.C == original.C);
    BOOST_CHECK(output.K == original.K);
    BOOST_CHECK_EQUAL(output.nHeight, original.nHeight);
    BOOST_CHECK(output.type == original.type);
    BOOST_CHECK_EQUAL(output.helsing_eligible, original.helsing_eligible);
}

BOOST_AUTO_TEST_CASE(mark_helsing_eligible_candidates_sets_requested_outputs_atomically)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId firstId = Output(55, 0);
    const helsing::OutputId secondId = Output(55, 1);
    const helsing::OutputId untouchedId = Output(55, 2);
    helsing::SparkOutputRecord first = EligibleOutput(firstId, 90);
    helsing::SparkOutputRecord second = EligibleOutput(secondId, 91);
    helsing::SparkOutputRecord untouched = EligibleOutput(untouchedId, 92);
    first.helsing_eligible = false;
    second.helsing_eligible = false;
    untouched.helsing_eligible = false;

    BOOST_CHECK(outputs.emplace(firstId, first).second);
    BOOST_CHECK(outputs.emplace(secondId, second).second);
    BOOST_CHECK(outputs.emplace(untouchedId, untouched).second);
    BOOST_CHECK(helsing::MarkHelsingEligibleOutputCandidatesSkeleton({{firstId, true}, {secondId, true}}, outputs));

    BOOST_CHECK(outputs.at(firstId).helsing_eligible);
    BOOST_CHECK(outputs.at(secondId).helsing_eligible);
    BOOST_CHECK(!outputs.at(untouchedId).helsing_eligible);
    BOOST_CHECK(outputs.at(firstId).S == first.S);
    BOOST_CHECK(outputs.at(secondId).C == second.C);
    BOOST_CHECK(outputs.at(untouchedId).K == untouched.K);
}

BOOST_AUTO_TEST_CASE(mark_helsing_eligible_candidates_accepts_empty_batch)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existingId = Output(56, 0);
    const helsing::SparkOutputRecord existing = EligibleOutput(existingId, 93);

    BOOST_CHECK(outputs.emplace(existingId, existing).second);
    BOOST_CHECK(helsing::MarkHelsingEligibleOutputCandidatesSkeleton({}, outputs));

    BOOST_REQUIRE(outputs.count(existingId) == 1);
    BOOST_CHECK(outputs.at(existingId).S == existing.S);
    BOOST_CHECK_EQUAL(outputs.at(existingId).helsing_eligible, existing.helsing_eligible);
}

BOOST_AUTO_TEST_CASE(mark_helsing_eligible_candidates_rejects_non_tag_revealing_paths_without_mutation)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId firstId = Output(57, 0);
    const helsing::OutputId secondId = Output(57, 1);
    helsing::SparkOutputRecord first = EligibleOutput(firstId, 94);
    helsing::SparkOutputRecord second = EligibleOutput(secondId, 95);
    first.helsing_eligible = false;
    second.helsing_eligible = false;

    BOOST_CHECK(outputs.emplace(firstId, first).second);
    BOOST_CHECK(outputs.emplace(secondId, second).second);
    BOOST_CHECK(!helsing::MarkHelsingEligibleOutputCandidatesSkeleton({{firstId, true}, {secondId, false}}, outputs));

    BOOST_CHECK(!outputs.at(firstId).helsing_eligible);
    BOOST_CHECK(!outputs.at(secondId).helsing_eligible);
}

BOOST_AUTO_TEST_CASE(mark_helsing_eligible_candidates_rejects_missing_or_invalid_records_without_mutation)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId validId = Output(58, 0);
    const helsing::OutputId invalidId = Output(58, 1);
    const helsing::OutputId missingId = Output(58, 2);
    helsing::SparkOutputRecord valid = EligibleOutput(validId, 96);
    helsing::SparkOutputRecord invalid = EligibleOutput(invalidId, 97);
    valid.helsing_eligible = false;
    invalid.helsing_eligible = false;
    invalid.S = GroupElement();

    BOOST_CHECK(outputs.emplace(validId, valid).second);
    BOOST_CHECK(outputs.emplace(invalidId, invalid).second);
    BOOST_CHECK(!helsing::MarkHelsingEligibleOutputCandidatesSkeleton({{validId, true}, {missingId, true}}, outputs));
    BOOST_CHECK(!outputs.at(validId).helsing_eligible);
    BOOST_CHECK(!outputs.at(invalidId).helsing_eligible);

    BOOST_CHECK(!helsing::MarkHelsingEligibleOutputCandidatesSkeleton({{validId, true}, {invalidId, true}}, outputs));
    BOOST_CHECK(!outputs.at(validId).helsing_eligible);
    BOOST_CHECK(!outputs.at(invalidId).helsing_eligible);
}

BOOST_AUTO_TEST_CASE(mark_helsing_eligible_candidates_rejects_output_id_mismatch_without_mutation)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId mapId = Output(59, 0);
    helsing::SparkOutputRecord mismatched = EligibleOutput(Output(59, 1), 98);
    mismatched.helsing_eligible = false;

    BOOST_CHECK(outputs.emplace(mapId, mismatched).second);
    BOOST_CHECK(!helsing::MarkHelsingEligibleOutputCandidatesSkeleton({{mapId, true}}, outputs));

    BOOST_REQUIRE(outputs.count(mapId) == 1);
    BOOST_CHECK(!outputs.at(mapId).helsing_eligible);
    BOOST_CHECK(outputs.at(mapId).output_id == mismatched.output_id);
}

BOOST_AUTO_TEST_CASE(cover_set_output_skeleton_accepts_valid_public_power)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(1, 0), Output(2, 0), Output(3, 0), Output(4, 0)};
    helsing::ValidationStateView view;
    unsigned char pointTag = 20;
    for (const helsing::OutputId& output_id : inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        pointTag += 10;
    }

    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(cover_set_output_skeleton_cardinality_precedes_other_failures)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(2, 0), Output(1, 0), helsing::OutputId()};
    helsing::ValidationStateView view;

    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY);
}

BOOST_AUTO_TEST_CASE(cover_set_output_skeleton_order_precedes_id_and_lookup_failures)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(2, 0), Output(1, 0), helsing::OutputId(), Output(3, 0)};
    helsing::ValidationStateView view;

    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
}

BOOST_AUTO_TEST_CASE(cover_set_output_skeleton_id_precedes_lookup_failures)
{
    const std::vector<helsing::OutputId> inCoinIDs = {helsing::OutputId(), Output(1, 0), Output(2, 0), Output(3, 0)};
    helsing::ValidationStateView view;

    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::INVALID_OUTPUT_ID);
}

BOOST_AUTO_TEST_CASE(cover_set_output_skeleton_checks_output_records_in_spec_order)
{
    const std::vector<helsing::OutputId> inCoinIDs = {Output(1, 0), Output(2, 0), Output(3, 0), Output(4, 0)};
    helsing::ValidationStateView view;
    unsigned char pointTag = 20;
    for (const helsing::OutputId& output_id : inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        pointTag += 10;
    }

    view.sparkOutputs.erase(inCoinIDs[0]);
    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    view.sparkOutputs.clear();
    pointTag = 20;
    for (const helsing::OutputId& output_id : inCoinIDs) {
        view.sparkOutputs.emplace(output_id, EligibleOutput(output_id, pointTag));
        pointTag += 10;
    }
    view.sparkOutputs[inCoinIDs[0]].output_id = Output(99, 0);
    view.sparkOutputs[inCoinIDs[0]].S = GroupElement();
    view.sparkOutputs[inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);

    view.sparkOutputs[inCoinIDs[0]] = EligibleOutput(inCoinIDs[0], 20);
    view.sparkOutputs[inCoinIDs[0]].S = GroupElement();
    view.sparkOutputs[inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view.sparkOutputs[inCoinIDs[0]] = EligibleOutput(inCoinIDs[0], 20);
    view.sparkOutputs[inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckCoverSetOutputsSkeleton(inCoinIDs, view, 2, 2) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);
}

BOOST_AUTO_TEST_CASE(output_id_helper_rejects_null_txid)
{
    BOOST_CHECK(!helsing::IsValidOutputId(helsing::OutputId()));
    BOOST_CHECK(!helsing::IsValidOutputId(helsing::OutputId(uint256(), 7)));
    BOOST_CHECK(helsing::IsValidOutputId(Output(1, 0)));
}

BOOST_AUTO_TEST_CASE(public_point_helper_rejects_infinity)
{
    BOOST_CHECK(helsing::IsValidPublicPoint(DeterministicPoint(1)));
    BOOST_CHECK(!helsing::IsValidPublicPoint(GroupElement()));
    BOOST_CHECK(!helsing::IsValidPublicPoint(NonMemberPoint()));
}

BOOST_AUTO_TEST_CASE(spark_output_record_helper_rejects_malformed_records)
{
    helsing::SparkOutputRecord record = EligibleOutput(Output(1, 0), 20);
    BOOST_CHECK(helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.output_id = helsing::OutputId();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.type = helsing::SparkOutputType::SPEND;
    BOOST_CHECK(helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.S = GroupElement();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.S = NonMemberPoint();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.C = GroupElement();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.C = NonMemberPoint();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.K = GroupElement();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.K = NonMemberPoint();
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.nHeight = -1;
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.type = helsing::SparkOutputType::UNKNOWN;
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.type = static_cast<helsing::SparkOutputType>(255);
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.type = static_cast<helsing::SparkOutputType>(3);
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(record));

    record = EligibleOutput(Output(1, 0), 20);
    record.nHeight = 0;
    BOOST_CHECK(helsing::IsValidSparkOutputRecord(record));
}

BOOST_AUTO_TEST_CASE(spark_output_record_helper_allows_ineligible_valid_records)
{
    helsing::SparkOutputRecord record = EligibleOutput(Output(1, 0), 20);
    record.helsing_eligible = false;

    BOOST_CHECK(helsing::IsValidSparkOutputRecord(record));
}

BOOST_AUTO_TEST_CASE(extracts_spark_mint_outputs_by_txid_vout)
{
    const SparkOutputFixture first = SparkMintOutput(1, 41);
    const SparkOutputFixture second = SparkMintOutput(2, 42);

    CMutableTransaction mtx;
    mtx.vout.emplace_back(0, CScript() << OP_RETURN);
    mtx.vout.emplace_back(1, first.script);
    mtx.vout.emplace_back(2, second.script);
    const CTransaction tx(mtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(tx, 200, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 2U);

    const helsing::OutputId firstId(tx.GetHash(), 1);
    const helsing::OutputId secondId(tx.GetHash(), 2);
    BOOST_REQUIRE(outputs.count(firstId) == 1);
    BOOST_REQUIRE(outputs.count(secondId) == 1);
    CheckSparkRecordMatchesCoin(outputs.at(firstId), firstId, first.coin, 200, helsing::SparkOutputType::MINT);
    CheckSparkRecordMatchesCoin(outputs.at(secondId), secondId, second.coin, 200, helsing::SparkOutputType::MINT);
}

BOOST_AUTO_TEST_CASE(extracts_spark_spend_created_outputs_by_txid_vout)
{
    const SparkOutputFixture smint = SparkSMintOutput(3, 43);

    CMutableTransaction mtx;
    mtx.nVersion = 3;
    mtx.nType = TRANSACTION_SPARK;
    mtx.vout.emplace_back(0, smint.script);
    const CTransaction tx(mtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(tx, 201, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);

    const helsing::OutputId outputId(tx.GetHash(), 0);
    BOOST_REQUIRE(outputs.count(outputId) == 1);
    CheckSparkRecordMatchesCoin(outputs.at(outputId), outputId, smint.coin, 201, helsing::SparkOutputType::SPEND);
}

BOOST_AUTO_TEST_CASE(extractor_preserves_duplicate_serial_commitments_under_distinct_output_ids)
{
    const SparkOutputFixture duplicate = SparkMintOutput(4, 44);

    CMutableTransaction mtx;
    mtx.vout.emplace_back(1, duplicate.script);
    mtx.vout.emplace_back(1, duplicate.script);
    const CTransaction tx(mtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(tx, 202, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 2U);

    const helsing::OutputId firstId(tx.GetHash(), 0);
    const helsing::OutputId secondId(tx.GetHash(), 1);
    BOOST_REQUIRE(outputs.count(firstId) == 1);
    BOOST_REQUIRE(outputs.count(secondId) == 1);
    BOOST_CHECK(outputs.at(firstId).S == outputs.at(secondId).S);
    BOOST_CHECK(outputs.at(firstId).output_id != outputs.at(secondId).output_id);
}

BOOST_AUTO_TEST_CASE(extractor_appends_records_for_block_view_accumulation)
{
    const SparkOutputFixture first = SparkMintOutput(5, 45);
    const SparkOutputFixture second = SparkSMintOutput(6, 46);

    CMutableTransaction firstMtx;
    firstMtx.vout.emplace_back(1, first.script);
    const CTransaction firstTx(firstMtx);

    CMutableTransaction secondMtx;
    secondMtx.nVersion = 3;
    secondMtx.nType = TRANSACTION_SPARK;
    secondMtx.vout.emplace_back(0, second.script);
    const CTransaction secondTx(secondMtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(firstTx, 203, outputs));
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(secondTx, 204, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 2U);

    const helsing::OutputId firstId(firstTx.GetHash(), 0);
    const helsing::OutputId secondId(secondTx.GetHash(), 0);
    BOOST_REQUIRE(outputs.count(firstId) == 1);
    BOOST_REQUIRE(outputs.count(secondId) == 1);
    CheckSparkRecordMatchesCoin(outputs.at(firstId), firstId, first.coin, 203, helsing::SparkOutputType::MINT);
    CheckSparkRecordMatchesCoin(outputs.at(secondId), secondId, second.coin, 204, helsing::SparkOutputType::SPEND);
}

BOOST_AUTO_TEST_CASE(extractor_rejects_malformed_spark_outputs_without_partial_records)
{
    const SparkOutputFixture valid = SparkMintOutput(7, 47);
    CScript malformed;
    malformed << OP_SPARKMINT;

    CMutableTransaction mtx;
    mtx.vout.emplace_back(1, valid.script);
    mtx.vout.emplace_back(1, malformed);
    const CTransaction tx(mtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existing = Output(9, 0);
    outputs.emplace(existing, EligibleOutput(existing, 90));
    BOOST_CHECK(!helsing::ExtractSparkOutputRecords(tx, 205, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existing) == 1);
}

BOOST_AUTO_TEST_CASE(extractor_rejects_duplicate_output_id_without_partial_records)
{
    const SparkOutputFixture first = SparkMintOutput(8, 48);
    const SparkOutputFixture second = SparkMintOutput(9, 49);

    CMutableTransaction mtx;
    mtx.vout.emplace_back(1, first.script);
    mtx.vout.emplace_back(1, second.script);
    const CTransaction tx(mtx);

    const helsing::OutputId existing(tx.GetHash(), 1);
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    outputs.emplace(existing, EligibleOutput(existing, 91));

    BOOST_CHECK(!helsing::ExtractSparkOutputRecords(tx, 206, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existing) == 1);
}

BOOST_AUTO_TEST_CASE(extractor_ignores_non_spark_transactions_and_rejects_negative_height)
{
    const SparkOutputFixture smint = SparkSMintOutput(10, 50);

    CMutableTransaction mtx;
    mtx.vout.emplace_back(1, smint.script);
    const CTransaction tx(mtx);

    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    BOOST_CHECK(helsing::ExtractSparkOutputRecords(tx, 204, outputs));
    BOOST_CHECK(outputs.empty());

    const SparkOutputFixture mint = SparkMintOutput(11, 51);
    CMutableTransaction sparkMintTx;
    sparkMintTx.vout.emplace_back(1, mint.script);
    const CTransaction sparkTx(sparkMintTx);
    const helsing::OutputId existing = Output(10, 0);
    outputs.emplace(existing, EligibleOutput(existing, 91));
    BOOST_CHECK(!helsing::ExtractSparkOutputRecords(sparkTx, -1, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existing) == 1);
}

BOOST_AUTO_TEST_CASE(apply_accepted_payout_output_records_skeleton_inserts_records_atomically)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existing = Output(11, 0);
    const helsing::OutputId firstId = Output(12, 0);
    const helsing::OutputId secondId = Output(12, 1);
    const helsing::SparkOutputRecord existingRecord = EligibleOutput(existing, 92);
    helsing::SparkOutputRecord first = EligibleOutput(firstId, 93);
    helsing::SparkOutputRecord second = EligibleOutput(secondId, 94);
    first.helsing_eligible = false;
    second.helsing_eligible = false;

    BOOST_CHECK(outputs.emplace(existing, existingRecord).second);
    BOOST_CHECK(helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({first, second}, outputs));

    BOOST_CHECK_EQUAL(outputs.size(), 3U);
    BOOST_REQUIRE(outputs.count(existing) == 1);
    BOOST_REQUIRE(outputs.count(firstId) == 1);
    BOOST_REQUIRE(outputs.count(secondId) == 1);
    CheckOutputIdEqual(outputs.at(firstId).output_id, firstId);
    CheckOutputIdEqual(outputs.at(secondId).output_id, secondId);
    BOOST_CHECK(outputs.at(firstId).S == first.S);
    BOOST_CHECK(outputs.at(firstId).C == first.C);
    BOOST_CHECK(outputs.at(firstId).K == first.K);
    BOOST_CHECK(outputs.at(firstId).type == first.type);
    BOOST_CHECK(!outputs.at(firstId).helsing_eligible);
    BOOST_CHECK(outputs.at(secondId).S == second.S);
    BOOST_CHECK(outputs.at(secondId).C == second.C);
    BOOST_CHECK(outputs.at(secondId).K == second.K);
    BOOST_CHECK(outputs.at(secondId).type == second.type);
    BOOST_CHECK(!outputs.at(secondId).helsing_eligible);
    BOOST_CHECK(outputs.at(existing).S == existingRecord.S);
}

BOOST_AUTO_TEST_CASE(apply_accepted_payout_output_records_skeleton_accepts_empty_batch)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existing = Output(13, 0);
    const helsing::SparkOutputRecord existingRecord = EligibleOutput(existing, 95);

    BOOST_CHECK(outputs.emplace(existing, existingRecord).second);
    BOOST_CHECK(helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({}, outputs));

    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_REQUIRE(outputs.count(existing) == 1);
    BOOST_CHECK(outputs.at(existing).S == existingRecord.S);
    BOOST_CHECK(outputs.at(existing).C == existingRecord.C);
    BOOST_CHECK(outputs.at(existing).K == existingRecord.K);
}

BOOST_AUTO_TEST_CASE(apply_accepted_payout_output_records_skeleton_preserves_duplicate_serial_commitments)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId firstId = Output(14, 0);
    const helsing::OutputId secondId = Output(14, 1);
    helsing::SparkOutputRecord first = EligibleOutput(firstId, 96);
    helsing::SparkOutputRecord second = EligibleOutput(secondId, 97);
    second.S = first.S;

    BOOST_CHECK(helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({first, second}, outputs));

    BOOST_CHECK_EQUAL(outputs.size(), 2U);
    BOOST_REQUIRE(outputs.count(firstId) == 1);
    BOOST_REQUIRE(outputs.count(secondId) == 1);
    BOOST_CHECK(outputs.at(firstId).output_id != outputs.at(secondId).output_id);
    BOOST_CHECK(outputs.at(firstId).S == outputs.at(secondId).S);
}

BOOST_AUTO_TEST_CASE(apply_accepted_payout_output_records_skeleton_rejects_invalid_without_mutation)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existing = Output(15, 0);
    const helsing::OutputId validId = Output(16, 0);
    const helsing::SparkOutputRecord existingRecord = EligibleOutput(existing, 98);
    helsing::SparkOutputRecord valid = EligibleOutput(validId, 99);
    helsing::SparkOutputRecord invalid = EligibleOutput(Output(17, 0), 100);
    invalid.S = GroupElement();

    BOOST_CHECK(outputs.emplace(existing, existingRecord).second);
    BOOST_CHECK(!helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({valid, invalid}, outputs));

    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_REQUIRE(outputs.count(existing) == 1);
    BOOST_CHECK(outputs.at(existing).S == existingRecord.S);
    BOOST_CHECK(outputs.count(validId) == 0);
    BOOST_CHECK(outputs.count(invalid.output_id) == 0);
}

BOOST_AUTO_TEST_CASE(apply_accepted_payout_output_records_skeleton_rejects_duplicate_output_ids_without_mutation)
{
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::OutputId existing = Output(18, 0);
    const helsing::OutputId firstId = Output(19, 0);
    const helsing::OutputId duplicateId = Output(20, 0);
    const helsing::SparkOutputRecord existingRecord = EligibleOutput(existing, 101);
    helsing::SparkOutputRecord first = EligibleOutput(firstId, 102);
    helsing::SparkOutputRecord duplicate = EligibleOutput(duplicateId, 103);
    helsing::SparkOutputRecord duplicateAgain = EligibleOutput(duplicateId, 104);

    BOOST_CHECK(outputs.emplace(existing, existingRecord).second);
    BOOST_CHECK(!helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({first, duplicate, duplicateAgain}, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existing) == 1);
    BOOST_CHECK(outputs.count(firstId) == 0);
    BOOST_CHECK(outputs.count(duplicateId) == 0);

    BOOST_CHECK(!helsing::ApplyAcceptedPayoutOutputRecordsSkeleton({existingRecord}, outputs));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existing) == 1);
}

BOOST_AUTO_TEST_CASE(apply_accepted_block_with_payout_outputs_skeleton_applies_steps_six_through_nine_atomically)
{
    helsing::CHelsingState state;
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::StakeRecord updateRecord = ActiveRecord(210, DeterministicPoint(223));
    const helsing::StakeRecord spentRecord = ActiveRecord(211, DeterministicPoint(224));
    const helsing::OutputId existingOutputId = Output(21, 0);
    const helsing::OutputId payoutOutputId = Output(22, 0);
    const helsing::SparkOutputRecord existingOutput = EligibleOutput(existingOutputId, 105);
    helsing::SparkOutputRecord payoutOutput = EligibleOutput(payoutOutputId, 106);
    helsing::StakeTx newStake = ValidStakeTx();
    newStake.T = DeterministicPoint(225);
    newStake.m.bytes = {0x62, 0x6c, 0x6f, 0x63, 0x6b};
    helsing::StakeContext updatedContext;
    updatedContext.bytes = {0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x5f, 0x75, 0x70, 0x64};
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    const uint256 newStakeId = DeterministicHash(212);
    blockSpentTags.insert(spentRecord.T);
    payoutOutput.helsing_eligible = false;

    BOOST_CHECK(state.AddActiveStake(updateRecord));
    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(outputs.emplace(existingOutputId, existingOutput).second);

    BOOST_CHECK(helsing::ApplyAcceptedBlockWithPayoutOutputsSkeleton(state, outputs, blockSpentTags, {{newStakeId, newStake}}, {{updateRecord.stake_id, updatedContext}}, {payoutOutput}, 430));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsSpentTag(spentRecord.T));
    BOOST_CHECK(!state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(state.IsActiveTag(updateRecord.T));
    BOOST_CHECK(state.IsActiveTag(newStake.T));
    BOOST_CHECK_EQUAL(outputs.size(), 2U);
    BOOST_REQUIRE(outputs.count(existingOutputId) == 1);
    BOOST_REQUIRE(outputs.count(payoutOutputId) == 1);
    BOOST_CHECK(outputs.at(existingOutputId).S == existingOutput.S);
    BOOST_CHECK(outputs.at(payoutOutputId).S == payoutOutput.S);
    BOOST_CHECK(!outputs.at(payoutOutputId).helsing_eligible);

    const helsing::StakeRecord* updatedStored = state.GetStakeRecord(updateRecord.stake_id);
    const helsing::StakeRecord* spentStored = state.GetStakeRecord(spentRecord.stake_id);
    const helsing::StakeRecord* newStored = state.GetStakeRecord(newStakeId);
    BOOST_REQUIRE(updatedStored != nullptr);
    BOOST_REQUIRE(spentStored != nullptr);
    BOOST_REQUIRE(newStored != nullptr);
    BOOST_CHECK(updatedStored->m.bytes == updatedContext.bytes);
    BOOST_CHECK_EQUAL(updatedStored->nLastUpdateHeight, 430);
    BOOST_CHECK(updatedStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(spentStored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(spentStored->nSpentHeight, 430);
    BOOST_CHECK(newStored->T == newStake.T);
    BOOST_CHECK_EQUAL(newStored->nHeight, 430);
    BOOST_CHECK(newStored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(apply_accepted_block_with_payout_outputs_skeleton_accepts_empty_block)
{
    helsing::CHelsingState state;
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::StakeRecord activeRecord = ActiveRecord(213, DeterministicPoint(226));
    const helsing::OutputId existingOutputId = Output(23, 0);
    const helsing::SparkOutputRecord existingOutput = EligibleOutput(existingOutputId, 107);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(outputs.emplace(existingOutputId, existingOutput).second);

    BOOST_CHECK(helsing::ApplyAcceptedBlockWithPayoutOutputsSkeleton(state, outputs, {}, {}, {}, {}, 431));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.at(existingOutputId).S == existingOutput.S);

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == activeRecord.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, activeRecord.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(apply_accepted_block_with_payout_outputs_skeleton_rejects_state_failure_without_output_mutation)
{
    helsing::CHelsingState state;
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::StakeRecord activeRecord = ActiveRecord(214, DeterministicPoint(227));
    const helsing::OutputId existingOutputId = Output(24, 0);
    const helsing::OutputId payoutOutputId = Output(25, 0);
    const helsing::SparkOutputRecord existingOutput = EligibleOutput(existingOutputId, 108);
    helsing::SparkOutputRecord payoutOutput = EligibleOutput(payoutOutputId, 109);
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(activeRecord.T);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(outputs.emplace(existingOutputId, existingOutput).second);
    BOOST_CHECK(!helsing::ApplyAcceptedBlockWithPayoutOutputsSkeleton(state, outputs, blockSpentTags, {}, {{activeRecord.stake_id, helsing::StakeContext()}}, {payoutOutput}, 432));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(!state.IsSpentTag(activeRecord.T));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existingOutputId) == 1);
    BOOST_CHECK(outputs.count(payoutOutputId) == 0);

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == activeRecord.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, activeRecord.nLastUpdateHeight);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(apply_accepted_block_with_payout_outputs_skeleton_rejects_output_failure_without_state_mutation)
{
    helsing::CHelsingState state;
    std::map<helsing::OutputId, helsing::SparkOutputRecord> outputs;
    const helsing::StakeRecord spentRecord = ActiveRecord(215, DeterministicPoint(228));
    const helsing::OutputId existingOutputId = Output(26, 0);
    const helsing::OutputId payoutOutputId = Output(27, 0);
    const helsing::SparkOutputRecord existingOutput = EligibleOutput(existingOutputId, 110);
    helsing::SparkOutputRecord payoutOutput = EligibleOutput(payoutOutputId, 111);
    payoutOutput.S = GroupElement();
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(spentRecord.T);

    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(outputs.emplace(existingOutputId, existingOutput).second);
    BOOST_CHECK(!helsing::ApplyAcceptedBlockWithPayoutOutputsSkeleton(state, outputs, blockSpentTags, {}, {}, {payoutOutput}, 433));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(!state.IsSpentTag(spentRecord.T));
    BOOST_CHECK_EQUAL(outputs.size(), 1U);
    BOOST_CHECK(outputs.count(existingOutputId) == 1);
    BOOST_CHECK(outputs.count(payoutOutputId) == 0);

    const helsing::StakeRecord* stored = state.GetStakeRecord(spentRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(default_objects_are_structurally_invalid)
{
    helsing::SparkOutputRecord output;
    BOOST_CHECK(!helsing::IsValidSparkOutputRecord(output));

    helsing::StakeTx tx;
    helsing::ValidationStateView view;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::EMPTY_INCOINIDS);

    tx.inCoinIDs = {Output(1, 0)};
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 20));
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);
}

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

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.inCoinIDs = {Output(1, 1), Output(1, 0)};
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);
}

BOOST_AUTO_TEST_CASE(rejects_null_output_id)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {helsing::OutputId()};
    helsing::ValidationStateView view;
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 20));

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_ID);
}

BOOST_AUTO_TEST_CASE(accepts_same_txid_with_increasing_vout)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(7, 0), Output(7, 1)};
    helsing::ValidationStateView view = ValidView(tx);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_public_points)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    tx.S_prime = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.S_prime = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.C_prime = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.C_prime = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.T = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.T = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);
}

BOOST_AUTO_TEST_CASE(validation_invalid_tag_precedes_block_spent_tag)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    tx.T = GroupElement();
    view.blockSpentTags.insert(tx.T);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);
}

BOOST_AUTO_TEST_CASE(rejects_each_missing_proof_blob)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    tx.pi_par.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.pi_val.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);

    tx = ValidStakeTx();
    view = ValidView(tx);
    tx.pi_tag.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);
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
    helsing::StakeRecord record = ActiveRecord(99, tx.T);
    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);

    state.Reset();
    view.blockSpentTags.insert(tx.T);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(tag_conflict_precedence_is_stable)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    view.blockSpentTags.insert(tx.T);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
}

BOOST_AUTO_TEST_CASE(validation_precedence_structural_before_state)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;
    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();

    tx.inCoinIDs = {Output(2, 0), Output(1, 0)};
    tx.S_prime = GroupElement();
    tx.pi_par.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    tx.S_prime = GroupElement();
    tx.pi_par.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);

    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    tx.pi_par.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);
}

BOOST_AUTO_TEST_CASE(validation_empty_incoinids_precedes_all_other_failures)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    tx.inCoinIDs.clear();
    tx.S_prime = GroupElement();
    tx.pi_par.bytes.clear();

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::EMPTY_INCOINIDS);
}

BOOST_AUTO_TEST_CASE(validation_invalid_output_id_precedes_group_and_state_failures)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {helsing::OutputId()};
    helsing::ValidationStateView view;
    helsing::CHelsingState state;
    view.helsingState = &state;
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 20));

    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    view.blockSpentTags.insert(tx.T);
    tx.S_prime = GroupElement();
    tx.pi_par.bytes.clear();

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_ID);
}

BOOST_AUTO_TEST_CASE(validation_precedence_state_before_block_and_outputs)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);

    state.Reset();
    view = ValidView(tx);
    view.helsingState = &state;
    BOOST_CHECK(state.AddActiveStake(ActiveRecord(88, tx.T)));
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_ALREADY_ACTIVE);

    state.Reset();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(validation_missing_proofs_precede_tag_conflicts)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    BOOST_CHECK(state.AddSpentTag(tx.T, 101));
    tx.pi_val.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);

    state.Reset();
    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    BOOST_CHECK(state.AddActiveStake(ActiveRecord(121, tx.T)));
    tx.pi_tag.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);

    state.Reset();
    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    tx.pi_par.bytes.clear();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::MISSING_PROOF);
}

BOOST_AUTO_TEST_CASE(validation_block_spent_precedes_output_record_failures)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);

    view.sparkOutputs[tx.inCoinIDs[0]].output_id = Output(99, 0);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);

    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs[tx.inCoinIDs[0]].S = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);

    tx = ValidStakeTx();
    view = ValidView(tx);
    view.helsingState = &state;
    view.blockSpentTags.insert(tx.T);
    view.sparkOutputs[tx.inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_spent_tags_work_without_persistent_state)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.blockSpentTags.insert(tx.T);

    BOOST_CHECK(view.helsingState == nullptr);
    BOOST_CHECK(view.HasBlockSpentTag(tx.T));
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_spent_tag_builder_accepts_distinct_tags)
{
    const GroupElement tagA = DeterministicPoint(132);
    const GroupElement tagB = DeterministicPoint(133);
    const GroupElement sentinel = DeterministicPoint(134);

    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(sentinel);

    BOOST_CHECK(helsing::BuildBlockSpentTagsSkeleton({tagA, tagB}, nullptr, blockSpentTags) == helsing::StakeValidationResult::OK);
    BOOST_CHECK_EQUAL(blockSpentTags.size(), 2U);
    BOOST_CHECK(blockSpentTags.count(tagA) == 1);
    BOOST_CHECK(blockSpentTags.count(tagB) == 1);
    BOOST_CHECK(blockSpentTags.count(sentinel) == 0);
}

BOOST_AUTO_TEST_CASE(block_spent_tag_builder_rejects_duplicates_without_mutation)
{
    const GroupElement tag = DeterministicPoint(135);
    const GroupElement sentinel = DeterministicPoint(136);

    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(sentinel);

    BOOST_CHECK(helsing::BuildBlockSpentTagsSkeleton({tag, tag}, nullptr, blockSpentTags) == helsing::StakeValidationResult::DUPLICATE_SPENT_TAG_IN_BLOCK);
    BOOST_CHECK_EQUAL(blockSpentTags.size(), 1U);
    BOOST_CHECK(blockSpentTags.count(sentinel) == 1);
    BOOST_CHECK(blockSpentTags.count(tag) == 0);
}

BOOST_AUTO_TEST_CASE(block_spent_tag_builder_rejects_prior_spent_tags_without_mutation)
{
    const GroupElement spentTag = DeterministicPoint(137);
    const GroupElement freshTag = DeterministicPoint(138);
    const GroupElement sentinel = DeterministicPoint(139);
    helsing::CHelsingState state;
    BOOST_CHECK(state.AddSpentTag(spentTag, 300));

    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(sentinel);

    BOOST_CHECK(helsing::BuildBlockSpentTagsSkeleton({freshTag, spentTag}, &state, blockSpentTags) == helsing::StakeValidationResult::TAG_ALREADY_SPENT);
    BOOST_CHECK_EQUAL(blockSpentTags.size(), 1U);
    BOOST_CHECK(blockSpentTags.count(sentinel) == 1);
    BOOST_CHECK(blockSpentTags.count(freshTag) == 0);
    BOOST_CHECK(blockSpentTags.count(spentTag) == 0);
}

BOOST_AUTO_TEST_CASE(block_spent_tag_builder_duplicate_precedes_prior_spent)
{
    const GroupElement spentTag = DeterministicPoint(140);
    const GroupElement sentinel = DeterministicPoint(141);
    helsing::CHelsingState state;
    BOOST_CHECK(state.AddSpentTag(spentTag, 301));

    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(sentinel);

    BOOST_CHECK(helsing::BuildBlockSpentTagsSkeleton({spentTag, spentTag}, &state, blockSpentTags) == helsing::StakeValidationResult::DUPLICATE_SPENT_TAG_IN_BLOCK);
    BOOST_CHECK_EQUAL(blockSpentTags.size(), 1U);
    BOOST_CHECK(blockSpentTags.count(sentinel) == 1);
}

BOOST_AUTO_TEST_CASE(block_skeleton_accepts_empty_and_distinct_new_stake_tags)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};
    txB.T = DeterministicPoint(13);

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({}, view) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB}, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(block_skeleton_rejects_duplicate_new_stake_tags)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};
    txB.T = txA.T;

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB}, view) == helsing::StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_skeleton_rejects_non_adjacent_duplicate_new_stake_tags)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    helsing::StakeTx txC = ValidStakeTx();
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};
    txB.T = DeterministicPoint(13);
    txC.inCoinIDs = {Output(5, 0), Output(6, 0)};
    txC.T = txA.T;

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);
    AddOutputs(view, txC, 60);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB, txC}, view) == helsing::StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_skeleton_duplicate_new_stake_tags_precede_individual_failures)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};
    txB.T = txA.T;
    txB.S_prime = GroupElement();

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB}, view) == helsing::StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_skeleton_invalid_tags_fall_through_to_individual_validation)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    txA.T = GroupElement();
    txB.T = txA.T;
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);
    view.blockSpentTags.insert(txA.T);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB}, view) == helsing::StakeValidationResult::INVALID_GROUP_ELEMENT);
}

BOOST_AUTO_TEST_CASE(block_skeleton_block_spent_tags_precede_duplicate_new_stake_tags)
{
    helsing::StakeTx txA = ValidStakeTx();
    helsing::StakeTx txB = ValidStakeTx();
    txB.inCoinIDs = {Output(3, 0), Output(4, 0)};
    txB.T = txA.T;

    helsing::ValidationStateView view;
    AddOutputs(view, txA, 20);
    AddOutputs(view, txB, 40);
    view.blockSpentTags.insert(txA.T);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({txA, txB}, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_skeleton_block_spent_tag_precedes_active_tag)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view;
    helsing::CHelsingState state;
    view.helsingState = &state;
    AddOutputs(view, tx, 20);

    BOOST_CHECK(state.AddActiveStake(ActiveRecord(131, tx.T)));
    view.blockSpentTags.insert(tx.T);

    BOOST_CHECK(helsing::CheckStakeBlockSkeleton({tx}, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(payout_eligibility_accepts_active_mature_stake)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(142, DeterministicPoint(142));

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(record.stake_id, view, record.nHeight + 10, 10) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(record.stake_id, view, record.nHeight, 0) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(payout_eligibility_rejects_missing_stake_record)
{
    helsing::ValidationStateView view;
    const uint256 stakeId = DeterministicHash(143);

    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(stakeId, view, 200, 0) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);

    helsing::CHelsingState state;
    view.helsingState = &state;
    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(stakeId, view, 200, 0) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(payout_eligibility_rejects_inactive_stakes_before_tag_and_maturity_checks)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord spentRecord = ActiveRecord(144, DeterministicPoint(144));
    const helsing::StakeRecord revokedRecord = ActiveRecord(145, DeterministicPoint(145));

    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.AddSpentTag(spentRecord.T, spentRecord.nHeight + 1));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));
    view.blockSpentTags.insert(spentRecord.T);
    view.blockSpentTags.insert(revokedRecord.T);

    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(spentRecord.stake_id, view, spentRecord.nHeight - 1, 10) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(revokedRecord.stake_id, view, revokedRecord.nHeight - 1, 10) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
}

BOOST_AUTO_TEST_CASE(payout_eligibility_rejects_block_spent_tag_before_maturity)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(146, DeterministicPoint(146));

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(record.stake_id, view, record.nHeight - 1, 10) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(payout_eligibility_rejects_immature_stake)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(147, DeterministicPoint(147));

    BOOST_CHECK(state.AddActiveStake(record));

    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(record.stake_id, view, record.nHeight + 9, 10) == helsing::StakeValidationResult::STAKE_NOT_MATURE);
    BOOST_CHECK(helsing::CheckPayoutEligibilitySkeleton(record.stake_id, view, record.nHeight, -1) == helsing::StakeValidationResult::STAKE_NOT_MATURE);
}

BOOST_AUTO_TEST_CASE(payout_block_eligibility_skeleton_accepts_empty_and_active_mature_payouts)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord firstRecord = ActiveRecord(158, DeterministicPoint(158));
    const helsing::StakeRecord secondRecord = ActiveRecord(159, DeterministicPoint(159));
    helsing::PayoutTxSkeleton firstPayout;
    helsing::PayoutTxSkeleton secondPayout;
    firstPayout.selected_stake_id = firstRecord.stake_id;
    firstPayout.payout_index = 0;
    firstPayout.V_PAYOUT = 1;
    secondPayout.selected_stake_id = secondRecord.stake_id;
    secondPayout.payout_index = 1;
    secondPayout.V_PAYOUT = 1;

    BOOST_CHECK(state.AddActiveStake(firstRecord));
    BOOST_CHECK(state.AddActiveStake(secondRecord));

    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({}, view, secondRecord.nHeight + 10, 10) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({firstPayout, secondPayout}, view, secondRecord.nHeight + 10, 10) == helsing::StakeValidationResult::OK);

    const helsing::StakeRecord* storedFirst = state.GetStakeRecord(firstRecord.stake_id);
    const helsing::StakeRecord* storedSecond = state.GetStakeRecord(secondRecord.stake_id);
    BOOST_REQUIRE(storedFirst != nullptr);
    BOOST_REQUIRE(storedSecond != nullptr);
    BOOST_CHECK(storedFirst->m.bytes == firstRecord.m.bytes);
    BOOST_CHECK(storedSecond->m.bytes == secondRecord.m.bytes);
}

BOOST_AUTO_TEST_CASE(payout_block_eligibility_skeleton_leaves_index_policy_to_separate_helper)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord firstRecord = ActiveRecord(160, DeterministicPoint(160));
    const helsing::StakeRecord secondRecord = ActiveRecord(161, DeterministicPoint(161));
    helsing::PayoutTxSkeleton firstPayout;
    helsing::PayoutTxSkeleton secondPayout;
    firstPayout.selected_stake_id = firstRecord.stake_id;
    firstPayout.payout_index = 7;
    secondPayout.selected_stake_id = secondRecord.stake_id;
    secondPayout.payout_index = 7;

    BOOST_CHECK(state.AddActiveStake(firstRecord));
    BOOST_CHECK(state.AddActiveStake(secondRecord));

    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({firstPayout, secondPayout}, view, secondRecord.nHeight + 10, 10) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(!helsing::ArePayoutIndexesDistinctSkeleton({firstPayout, secondPayout}));
}

BOOST_AUTO_TEST_CASE(payout_block_eligibility_skeleton_rejects_missing_state_or_record)
{
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = DeterministicHash(162);

    helsing::ValidationStateView view;
    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({payout}, view, 300, 0) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);

    helsing::CHelsingState state;
    view.helsingState = &state;
    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({payout}, view, 300, 0) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(payout_block_eligibility_skeleton_checks_spec_prefix_before_unimplemented_fields)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(163, DeterministicPoint(163));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({payout}, view, record.nHeight - 1, 10) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(payout_block_eligibility_skeleton_preserves_single_payout_precedence)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(164, DeterministicPoint(164));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(record.T, record.nHeight + 1));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckPayoutBlockEligibilitySkeleton({payout}, view, record.nHeight - 1, 10) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
}

BOOST_AUTO_TEST_CASE(payout_verification_skeleton_accepts_eligible_payout_with_expected_public_fields)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(48, DeterministicPoint(48));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;
    payout.payout_index = 24;
    payout.addr_pk.bytes = {0x61};
    payout.V_PAYOUT = 51;
    payout.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 603;
    context.prev_block_hash = DeterministicHash(49);
    context.payout_index = payout.payout_index;
    context.selected_stake_id = payout.selected_stake_id;

    helsing::PayoutAddressBlob registeredAddress;
    registeredAddress.bytes = payout.addr_pk.bytes;
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = payout.coin.bytes;

    BOOST_CHECK(state.AddActiveStake(record));
    const helsing::PayoutVerificationSkeletonResult result = helsing::CheckPayoutVerificationSkeleton(payout, view, record.nHeight + 10, 10, context, registeredAddress, record.stake_id, payout.V_PAYOUT, payout.V_PAYOUT + 1, expectedCoin);

    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.public_result == helsing::PayoutPublicValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(payout_verification_skeleton_checks_eligibility_before_public_fields)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(49, DeterministicPoint(49));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;
    payout.payout_index = 25;
    payout.addr_pk.bytes = {0x61};
    payout.V_PAYOUT = 52;
    payout.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton wrongContext;
    wrongContext.chain_id = {0x66};
    wrongContext.block_height = 604;
    wrongContext.prev_block_hash = DeterministicHash(50);
    wrongContext.payout_index = payout.payout_index + 1;
    wrongContext.selected_stake_id = payout.selected_stake_id;

    helsing::PayoutAddressBlob wrongAddress;
    wrongAddress.bytes = {0x62};
    helsing::PayoutCoinBlob wrongCoin;
    wrongCoin.bytes = {0x64};

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);
    const helsing::PayoutVerificationSkeletonResult result = helsing::CheckPayoutVerificationSkeleton(payout, view, record.nHeight - 1, 10, wrongContext, wrongAddress, DeterministicHash(51), payout.V_PAYOUT + 1, payout.V_PAYOUT, wrongCoin);

    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
    BOOST_CHECK(result.public_result == helsing::PayoutPublicValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(payout_verification_skeleton_reports_public_failure_after_eligibility_passes)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(50, DeterministicPoint(50));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;
    payout.payout_index = 26;
    payout.addr_pk.bytes = {0x61};
    payout.V_PAYOUT = 53;
    payout.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 605;
    context.prev_block_hash = DeterministicHash(52);
    context.payout_index = payout.payout_index;
    context.selected_stake_id = payout.selected_stake_id;

    helsing::PayoutAddressBlob wrongAddress;
    wrongAddress.bytes = {0x62};
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = payout.coin.bytes;

    BOOST_CHECK(state.AddActiveStake(record));
    const helsing::PayoutVerificationSkeletonResult result = helsing::CheckPayoutVerificationSkeleton(payout, view, record.nHeight + 10, 10, context, wrongAddress, record.stake_id, payout.V_PAYOUT, payout.V_PAYOUT + 1, expectedCoin);

    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.public_result == helsing::PayoutPublicValidationResult::ADDRESS_MISMATCH);
}

BOOST_AUTO_TEST_CASE(payout_verification_skeleton_does_not_mutate_state)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(51, DeterministicPoint(51));
    helsing::PayoutTxSkeleton payout;
    payout.selected_stake_id = record.stake_id;
    payout.payout_index = 27;
    payout.addr_pk.bytes = {0x61};
    payout.V_PAYOUT = 54;
    payout.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 606;
    context.prev_block_hash = DeterministicHash(53);
    context.payout_index = payout.payout_index;
    context.selected_stake_id = payout.selected_stake_id;

    helsing::PayoutAddressBlob registeredAddress;
    registeredAddress.bytes = payout.addr_pk.bytes;
    helsing::PayoutCoinBlob expectedCoin;
    expectedCoin.bytes = payout.coin.bytes;

    BOOST_CHECK(state.AddActiveStake(record));
    const helsing::PayoutVerificationSkeletonResult result = helsing::CheckPayoutVerificationSkeleton(payout, view, record.nHeight + 10, 10, context, registeredAddress, record.stake_id, payout.V_PAYOUT, payout.V_PAYOUT + 1, expectedCoin);

    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK(result.public_result == helsing::PayoutPublicValidationResult::OK);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK(!state.IsSpentTag(record.T));
    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(stake_update_verification_skeleton_accepts_complete_active_update)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(52, DeterministicPoint(52));
    helsing::StakeUpdateTx tx;
    tx.stake_id = record.stake_id;
    tx.m_new.bytes = {0x6d, 0x32};
    tx.sig_update.bytes = {0x73, 0x32};

    BOOST_CHECK(state.AddActiveStake(record));
    const helsing::StakeUpdateVerificationSkeletonResult result = helsing::CheckStakeUpdateVerificationSkeleton(tx, view);

    BOOST_CHECK(result.tx_complete);
    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_update_verification_skeleton_checks_completeness_before_state)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(53, DeterministicPoint(53));
    helsing::StakeUpdateTx tx;
    tx.stake_id = record.stake_id;
    tx.m_new.bytes = {0x6d, 0x33};

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);
    const helsing::StakeUpdateVerificationSkeletonResult result = helsing::CheckStakeUpdateVerificationSkeleton(tx, view);

    BOOST_CHECK(!result.tx_complete);
    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_update_verification_skeleton_reports_eligibility_after_completeness)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(54, DeterministicPoint(54));
    helsing::StakeUpdateTx tx;
    tx.stake_id = record.stake_id;
    tx.m_new.bytes = {0x6d, 0x34};
    tx.sig_update.bytes = {0x73, 0x34};

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);
    const helsing::StakeUpdateVerificationSkeletonResult result = helsing::CheckStakeUpdateVerificationSkeleton(tx, view);

    BOOST_CHECK(result.tx_complete);
    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(stake_update_verification_skeleton_does_not_mutate_state)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(55, DeterministicPoint(55));
    helsing::StakeUpdateTx tx;
    tx.stake_id = record.stake_id;
    tx.m_new.bytes = {0x6d, 0x35};
    tx.sig_update.bytes = {0x73, 0x35};

    BOOST_CHECK(state.AddActiveStake(record));
    const helsing::StakeUpdateVerificationSkeletonResult result = helsing::CheckStakeUpdateVerificationSkeleton(tx, view);

    BOOST_CHECK(result.tx_complete);
    BOOST_CHECK(result.stake_result == helsing::StakeValidationResult::OK);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK(!state.IsSpentTag(record.T));
    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nLastUpdateHeight);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(stake_update_eligibility_accepts_active_stake)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(148, DeterministicPoint(148));

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(record.stake_id, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(stake_update_eligibility_rejects_missing_stake_record)
{
    helsing::ValidationStateView view;
    const uint256 stakeId = DeterministicHash(149);

    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(stakeId, view) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);

    helsing::CHelsingState state;
    view.helsingState = &state;
    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(stakeId, view) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(stake_update_eligibility_rejects_inactive_stakes_before_tag_checks)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord spentRecord = ActiveRecord(150, DeterministicPoint(150));
    const helsing::StakeRecord revokedRecord = ActiveRecord(151, DeterministicPoint(151));

    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.AddSpentTag(spentRecord.T, spentRecord.nHeight + 1));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));
    view.blockSpentTags.insert(spentRecord.T);
    view.blockSpentTags.insert(revokedRecord.T);

    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(spentRecord.stake_id, view) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(revokedRecord.stake_id, view) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
}

BOOST_AUTO_TEST_CASE(stake_update_eligibility_rejects_block_spent_tag)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(152, DeterministicPoint(152));

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckStakeUpdateEligibilitySkeleton(record.stake_id, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(stake_update_block_skeleton_accepts_empty_and_active_updates)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord firstRecord = ActiveRecord(153, DeterministicPoint(153));
    const helsing::StakeRecord secondRecord = ActiveRecord(154, DeterministicPoint(154));
    helsing::StakeUpdateTx firstUpdate;
    helsing::StakeUpdateTx secondUpdate;
    firstUpdate.stake_id = firstRecord.stake_id;
    firstUpdate.m_new.bytes = {0x6d, 0x31};
    firstUpdate.sig_update.bytes = {0x73, 0x31};
    secondUpdate.stake_id = secondRecord.stake_id;
    secondUpdate.m_new.bytes = {0x6d, 0x32};
    secondUpdate.sig_update.bytes = {0x73, 0x32};

    BOOST_CHECK(state.AddActiveStake(firstRecord));
    BOOST_CHECK(state.AddActiveStake(secondRecord));

    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({}, view) == helsing::StakeValidationResult::OK);
    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({firstUpdate, secondUpdate}, view) == helsing::StakeValidationResult::OK);

    const helsing::StakeRecord* storedFirst = state.GetStakeRecord(firstRecord.stake_id);
    const helsing::StakeRecord* storedSecond = state.GetStakeRecord(secondRecord.stake_id);
    BOOST_REQUIRE(storedFirst != nullptr);
    BOOST_REQUIRE(storedSecond != nullptr);
    BOOST_CHECK(storedFirst->m.bytes == firstRecord.m.bytes);
    BOOST_CHECK(storedSecond->m.bytes == secondRecord.m.bytes);
}

BOOST_AUTO_TEST_CASE(stake_update_block_skeleton_rejects_missing_state_or_record)
{
    helsing::StakeUpdateTx update;
    update.stake_id = DeterministicHash(155);
    update.m_new.bytes = {0x6d};
    update.sig_update.bytes = {0x73};

    helsing::ValidationStateView view;
    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({update}, view) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);

    helsing::CHelsingState state;
    view.helsingState = &state;
    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({update}, view) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(stake_update_block_skeleton_checks_spec_prefix_before_unimplemented_fields)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(156, DeterministicPoint(156));
    helsing::StakeUpdateTx update;
    update.stake_id = record.stake_id;

    BOOST_CHECK(state.AddActiveStake(record));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({update}, view) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(stake_update_block_skeleton_preserves_single_update_precedence)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord record = ActiveRecord(157, DeterministicPoint(157));
    helsing::StakeUpdateTx update;
    update.stake_id = record.stake_id;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(record.T, record.nHeight + 1));
    view.blockSpentTags.insert(record.T);

    BOOST_CHECK(helsing::CheckStakeUpdateBlockSkeleton({update}, view) == helsing::StakeValidationResult::STAKE_NOT_ACTIVE);
}

BOOST_AUTO_TEST_CASE(block_validation_prefix_skeleton_accepts_valid_phase_inputs)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    helsing::StakeTx stake = ValidStakeTx();
    const helsing::StakeRecord updateRecord = ActiveRecord(171, DeterministicPoint(171));
    const helsing::StakeRecord payoutRecord = ActiveRecord(172, DeterministicPoint(172));
    helsing::StakeUpdateTx update;
    helsing::PayoutTxSkeleton payout;
    update.stake_id = updateRecord.stake_id;
    update.m_new.bytes = {0x75, 0x70, 0x64};
    update.sig_update.bytes = {0x73, 0x69, 0x67};
    payout.selected_stake_id = payoutRecord.stake_id;
    payout.payout_index = 0;
    payout.V_PAYOUT = 1;

    AddOutputs(view, stake, 30);
    BOOST_CHECK(state.AddActiveStake(updateRecord));
    BOOST_CHECK(state.AddActiveStake(payoutRecord));

    BOOST_CHECK(helsing::CheckBlockValidationPrefixSkeleton({stake}, {update}, {payout}, view, payoutRecord.nHeight + 10, 10) == helsing::StakeValidationResult::OK);

    const helsing::StakeRecord* storedUpdate = state.GetStakeRecord(updateRecord.stake_id);
    const helsing::StakeRecord* storedPayout = state.GetStakeRecord(payoutRecord.stake_id);
    BOOST_REQUIRE(storedUpdate != nullptr);
    BOOST_REQUIRE(storedPayout != nullptr);
    BOOST_CHECK(storedUpdate->m.bytes == updateRecord.m.bytes);
    BOOST_CHECK(storedPayout->m.bytes == payoutRecord.m.bytes);
}

BOOST_AUTO_TEST_CASE(block_validation_prefix_skeleton_checks_stakes_before_updates_and_payouts)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    helsing::StakeTx stake = ValidStakeTx();
    helsing::StakeUpdateTx update;
    helsing::PayoutTxSkeleton payout;
    update.stake_id = DeterministicHash(173);
    payout.selected_stake_id = DeterministicHash(174);

    AddOutputs(view, stake, 40);
    view.blockSpentTags.insert(stake.T);

    BOOST_CHECK(helsing::CheckBlockValidationPrefixSkeleton({stake}, {update}, {payout}, view, 300, 0) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_validation_prefix_skeleton_checks_updates_before_payouts)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord updateRecord = ActiveRecord(175, DeterministicPoint(175));
    helsing::StakeUpdateTx update;
    helsing::PayoutTxSkeleton payout;
    update.stake_id = updateRecord.stake_id;
    payout.selected_stake_id = DeterministicHash(176);

    BOOST_CHECK(state.AddActiveStake(updateRecord));
    view.blockSpentTags.insert(updateRecord.T);

    BOOST_CHECK(helsing::CheckBlockValidationPrefixSkeleton({}, {update}, {payout}, view, 300, 0) == helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK);
}

BOOST_AUTO_TEST_CASE(block_validation_prefix_skeleton_reports_payout_after_prior_phases_pass)
{
    helsing::CHelsingState state;
    helsing::ValidationStateView view;
    view.helsingState = &state;
    const helsing::StakeRecord updateRecord = ActiveRecord(177, DeterministicPoint(177));
    helsing::StakeUpdateTx update;
    helsing::PayoutTxSkeleton payout;
    update.stake_id = updateRecord.stake_id;
    payout.selected_stake_id = DeterministicHash(178);

    BOOST_CHECK(state.AddActiveStake(updateRecord));

    BOOST_CHECK(helsing::CheckBlockValidationPrefixSkeleton({}, {update}, {payout}, view, 300, 0) == helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND);
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

BOOST_AUTO_TEST_CASE(state_initially_empty_indexes)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(51);
    const uint256 stakeId = DeterministicHash(110);
    uint256 activeStakeId = DeterministicHash(250);
    const uint256 sentinel = activeStakeId;

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK(!state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK(activeStakeId == sentinel);
    BOOST_CHECK(state.GetStakeRecord(stakeId) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_add_spent_tag_without_active_record_indexes_spent_only)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(52);
    uint256 activeStakeId;

    BOOST_CHECK(state.AddSpentTag(tag, 120));
    BOOST_CHECK(state.IsSpentTag(tag));
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK(!state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
}

BOOST_AUTO_TEST_CASE(state_rejects_negative_activation_height_without_mutation)
{
    helsing::CHelsingState state;
    helsing::StakeRecord record = ActiveRecord(123, DeterministicPoint(63));
    record.nHeight = -1;

    BOOST_CHECK(!state.AddActiveStake(record));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(record.T));
}

BOOST_AUTO_TEST_CASE(state_rejects_negative_activation_height_without_mutating_populated_state)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(69);
    const GroupElement spentOnlyTag = DeterministicPoint(70);
    const helsing::StakeRecord activeRecord = ActiveRecord(108, activeTag);
    helsing::StakeRecord rejectedRecord = ActiveRecord(109, DeterministicPoint(71));
    rejectedRecord.nHeight = -1;

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 172));
    BOOST_CHECK(!state.AddActiveStake(rejectedRecord));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsActiveTag(rejectedRecord.T));
    BOOST_CHECK(state.GetStakeRecord(activeRecord.stake_id) != nullptr);
    BOOST_CHECK(state.GetStakeRecord(rejectedRecord.stake_id) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_rejects_negative_spent_height_without_mutation)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(62);
    const helsing::StakeRecord record = ActiveRecord(122, tag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.AddSpentTag(tag, -1));

    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK(state.IsActiveTag(tag));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_add_spent_tag_accepts_zero_height)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(72);
    const helsing::StakeRecord record = ActiveRecord(110, tag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(tag, 0));

    BOOST_CHECK(state.IsSpentTag(tag));
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, 0);
}

BOOST_AUTO_TEST_CASE(state_apply_block_spent_tags_skeleton_updates_spent_and_active_indexes)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord spentRecord = ActiveRecord(171, DeterministicPoint(181));
    const helsing::StakeRecord activeRecord = ActiveRecord(172, DeterministicPoint(182));
    const GroupElement spentOnlyTag = DeterministicPoint(183);
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(spentRecord.T);
    blockSpentTags.insert(spentOnlyTag);

    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(activeRecord));

    BOOST_CHECK(state.ApplyBlockSpentTagsSkeleton(blockSpentTags, 400));

    BOOST_CHECK(state.IsSpentTag(spentRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 2U);

    const helsing::StakeRecord* spentStored = state.GetStakeRecord(spentRecord.stake_id);
    const helsing::StakeRecord* activeStored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(spentStored != nullptr);
    BOOST_REQUIRE(activeStored != nullptr);
    BOOST_CHECK(spentStored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(spentStored->nSpentHeight, 400);
    BOOST_CHECK(activeStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(activeStored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_apply_block_spent_tags_skeleton_accepts_empty_set)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord record = ActiveRecord(173, DeterministicPoint(184));
    const GroupElement spentOnlyTag = DeterministicPoint(185);
    const std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 401));

    BOOST_CHECK(state.ApplyBlockSpentTagsSkeleton(blockSpentTags, 402));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
}

BOOST_AUTO_TEST_CASE(state_apply_block_spent_tags_skeleton_rejects_negative_height_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord record = ActiveRecord(174, DeterministicPoint(186));
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(record.T);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.ApplyBlockSpentTagsSkeleton(blockSpentTags, -1));

    BOOST_CHECK(!state.IsSpentTag(record.T));
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_apply_block_spent_tags_skeleton_rejects_invalid_or_prior_spent_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(175, DeterministicPoint(187));
    const GroupElement priorSpentTag = DeterministicPoint(188);
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(activeRecord.T);
    blockSpentTags.insert(priorSpentTag);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(priorSpentTag, 403));
    BOOST_CHECK(!state.ApplyBlockSpentTagsSkeleton(blockSpentTags, 404));

    BOOST_CHECK(!state.IsSpentTag(activeRecord.T));
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(state.IsSpentTag(priorSpentTag));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);

    blockSpentTags.clear();
    blockSpentTags.insert(activeRecord.T);
    blockSpentTags.insert(GroupElement());
    BOOST_CHECK(!state.ApplyBlockSpentTagsSkeleton(blockSpentTags, 405));

    BOOST_CHECK(!state.IsSpentTag(activeRecord.T));
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
}

BOOST_AUTO_TEST_CASE(state_add_accepted_stake_populates_consensus_record)
{
    helsing::CHelsingState state;
    helsing::StakeTx tx = ValidStakeTx();
    tx.m.bytes = {0x01, 0x02, 0x03};
    const uint256 stakeId = DeterministicHash(131);

    BOOST_CHECK(state.AddAcceptedStake(stakeId, tx, 250));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(tx.T, activeStakeId));
    BOOST_CHECK(activeStakeId == stakeId);
    BOOST_CHECK(state.IsActiveTag(tx.T));
    BOOST_CHECK(!state.IsSpentTag(tx.T));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(stakeId);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->stake_id == stakeId);
    BOOST_CHECK(stored->T == tx.T);
    BOOST_CHECK(stored->m.bytes == tx.m.bytes);
    BOOST_CHECK_EQUAL(stored->nHeight, 250);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, 250);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_add_accepted_stake_rejects_invalid_inputs_without_mutation)
{
    helsing::CHelsingState state;
    helsing::StakeTx tx = ValidStakeTx();
    const GroupElement validTag = tx.T;
    const helsing::StakeRecord activeRecord = ActiveRecord(132, DeterministicPoint(84));
    const GroupElement spentOnlyTag = DeterministicPoint(85);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 260));

    uint256 nullStakeId;
    BOOST_CHECK(!state.AddAcceptedStake(nullStakeId, tx, 251));

    BOOST_CHECK(!state.AddAcceptedStake(DeterministicHash(133), tx, -1));

    tx.T = GroupElement();
    BOOST_CHECK(!state.AddAcceptedStake(DeterministicHash(134), tx, 252));

    tx = ValidStakeTx();
    tx.T = activeRecord.T;
    BOOST_CHECK(!state.AddAcceptedStake(DeterministicHash(135), tx, 253));

    tx = ValidStakeTx();
    tx.T = spentOnlyTag;
    BOOST_CHECK(!state.AddAcceptedStake(DeterministicHash(136), tx, 254));

    tx = ValidStakeTx();
    BOOST_CHECK(!state.AddAcceptedStake(activeRecord.stake_id, tx, 255));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsActiveTag(validTag));
    BOOST_CHECK(state.GetStakeRecord(activeRecord.stake_id) != nullptr);
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(133)) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(134)) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(135)) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(136)) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stakes_skeleton_adds_all_records_atomically)
{
    helsing::CHelsingState state;
    helsing::StakeTx first = ValidStakeTx();
    helsing::StakeTx second = ValidStakeTx();
    second.T = DeterministicPoint(189);
    second.m.bytes = {0x73, 0x65, 0x63};
    const GroupElement spentOnlyTag = DeterministicPoint(190);
    const uint256 firstStakeId = DeterministicHash(177);
    const uint256 secondStakeId = DeterministicHash(178);

    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 406));
    BOOST_CHECK(state.ApplyAcceptedStakesSkeleton({{firstStakeId, first}, {secondStakeId, second}}, 407));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(first.T));
    BOOST_CHECK(state.IsActiveTag(second.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(first.T, activeStakeId));
    BOOST_CHECK(activeStakeId == firstStakeId);
    BOOST_CHECK(state.GetActiveStakeId(second.T, activeStakeId));
    BOOST_CHECK(activeStakeId == secondStakeId);

    const helsing::StakeRecord* firstStored = state.GetStakeRecord(firstStakeId);
    const helsing::StakeRecord* secondStored = state.GetStakeRecord(secondStakeId);
    BOOST_REQUIRE(firstStored != nullptr);
    BOOST_REQUIRE(secondStored != nullptr);
    BOOST_CHECK(firstStored->T == first.T);
    BOOST_CHECK(firstStored->m.bytes == first.m.bytes);
    BOOST_CHECK_EQUAL(firstStored->nHeight, 407);
    BOOST_CHECK_EQUAL(firstStored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(firstStored->nLastUpdateHeight, 407);
    BOOST_CHECK(firstStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(secondStored->T == second.T);
    BOOST_CHECK(secondStored->m.bytes == second.m.bytes);
    BOOST_CHECK_EQUAL(secondStored->nHeight, 407);
    BOOST_CHECK_EQUAL(secondStored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(secondStored->nLastUpdateHeight, 407);
    BOOST_CHECK(secondStored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stakes_skeleton_accepts_empty_set)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(179, DeterministicPoint(191));
    const GroupElement spentOnlyTag = DeterministicPoint(192);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 408));

    BOOST_CHECK(state.ApplyAcceptedStakesSkeleton({}, 409));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stakes_skeleton_rejects_negative_height_without_mutation)
{
    helsing::CHelsingState state;
    helsing::StakeTx tx = ValidStakeTx();
    const uint256 stakeId = DeterministicHash(180);

    BOOST_CHECK(!state.ApplyAcceptedStakesSkeleton({{stakeId, tx}}, -1));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(tx.T));
    BOOST_CHECK(state.GetStakeRecord(stakeId) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stakes_skeleton_rejects_invalid_batch_without_partial_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord existingRecord = ActiveRecord(181, DeterministicPoint(193));
    helsing::StakeTx first = ValidStakeTx();
    helsing::StakeTx duplicateTag = ValidStakeTx();
    helsing::StakeTx validAfterFailure = ValidStakeTx();
    duplicateTag.T = first.T;
    validAfterFailure.T = DeterministicPoint(194);
    const uint256 firstStakeId = DeterministicHash(182);
    const uint256 duplicateStakeId = DeterministicHash(183);
    const uint256 validAfterFailureStakeId = DeterministicHash(184);

    BOOST_CHECK(state.AddActiveStake(existingRecord));
    BOOST_CHECK(!state.ApplyAcceptedStakesSkeleton({{firstStakeId, first}, {duplicateStakeId, duplicateTag}, {validAfterFailureStakeId, validAfterFailure}}, 410));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(existingRecord.T));
    BOOST_CHECK(!state.IsActiveTag(first.T));
    BOOST_CHECK(!state.IsActiveTag(validAfterFailure.T));
    BOOST_CHECK(state.GetStakeRecord(firstStakeId) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(duplicateStakeId) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(validAfterFailureStakeId) == nullptr);

    uint256 nullStakeId;
    BOOST_CHECK(!state.ApplyAcceptedStakesSkeleton({{nullStakeId, first}}, 411));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);

    helsing::StakeTx spentTagTx = ValidStakeTx();
    spentTagTx.T = DeterministicPoint(195);
    BOOST_CHECK(state.AddSpentTag(spentTagTx.T, 412));
    BOOST_CHECK(!state.ApplyAcceptedStakesSkeleton({{DeterministicHash(185), spentTagTx}}, 413));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_update_skeleton_updates_only_metadata)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord record = ActiveRecord(137, DeterministicPoint(86));
    const helsing::StakeRecord otherRecord = ActiveRecord(138, DeterministicPoint(87));
    const GroupElement spentOnlyTag = DeterministicPoint(88);
    helsing::StakeContext newContext;
    newContext.bytes = {0x6d, 0x5f, 0x6e, 0x65, 0x77};

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddActiveStake(otherRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 261));

    BOOST_CHECK(state.ApplyAcceptedStakeUpdateSkeleton(record.stake_id, newContext, 300));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK(state.IsActiveTag(otherRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsSpentTag(record.T));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(record.T, activeStakeId));
    BOOST_CHECK(activeStakeId == record.stake_id);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->stake_id == record.stake_id);
    BOOST_CHECK(stored->T == record.T);
    BOOST_CHECK(stored->m.bytes == newContext.bytes);
    BOOST_CHECK_EQUAL(stored->nHeight, record.nHeight);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, 300);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);

    const helsing::StakeRecord* otherStored = state.GetStakeRecord(otherRecord.stake_id);
    BOOST_REQUIRE(otherStored != nullptr);
    BOOST_CHECK(otherStored->m.bytes == otherRecord.m.bytes);
    BOOST_CHECK_EQUAL(otherStored->nLastUpdateHeight, otherRecord.nLastUpdateHeight);
    BOOST_CHECK(otherStored->T == otherRecord.T);
    BOOST_CHECK(otherStored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_update_skeleton_rejects_missing_and_inactive)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(139, DeterministicPoint(89));
    const helsing::StakeRecord spentRecord = ActiveRecord(140, DeterministicPoint(90));
    const helsing::StakeRecord revokedRecord = ActiveRecord(141, DeterministicPoint(91));
    helsing::StakeContext newContext;
    newContext.bytes = {0x75, 0x70, 0x64, 0x61, 0x74, 0x65};

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.AddSpentTag(spentRecord.T, spentRecord.nHeight + 1));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));

    BOOST_CHECK(!state.ApplyAcceptedStakeUpdateSkeleton(DeterministicHash(142), newContext, 301));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdateSkeleton(spentRecord.stake_id, newContext, 302));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdateSkeleton(revokedRecord.stake_id, newContext, 303));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(!state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(!state.IsActiveTag(revokedRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentRecord.T));

    const helsing::StakeRecord* activeStored = state.GetStakeRecord(activeRecord.stake_id);
    const helsing::StakeRecord* spentStored = state.GetStakeRecord(spentRecord.stake_id);
    const helsing::StakeRecord* revokedStored = state.GetStakeRecord(revokedRecord.stake_id);
    BOOST_REQUIRE(activeStored != nullptr);
    BOOST_REQUIRE(spentStored != nullptr);
    BOOST_REQUIRE(revokedStored != nullptr);
    BOOST_CHECK(activeStored->m.bytes == activeRecord.m.bytes);
    BOOST_CHECK_EQUAL(activeStored->nLastUpdateHeight, activeRecord.nLastUpdateHeight);
    BOOST_CHECK(spentStored->m.bytes == spentRecord.m.bytes);
    BOOST_CHECK_EQUAL(spentStored->nLastUpdateHeight, spentRecord.nLastUpdateHeight);
    BOOST_CHECK(spentStored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK(revokedStored->m.bytes == revokedRecord.m.bytes);
    BOOST_CHECK_EQUAL(revokedStored->nLastUpdateHeight, revokedRecord.nLastUpdateHeight);
    BOOST_CHECK(revokedStored->status == helsing::StakeStatus::REVOKED);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_update_skeleton_rejects_negative_height)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord record = ActiveRecord(142, DeterministicPoint(92));
    const GroupElement spentOnlyTag = DeterministicPoint(93);
    helsing::StakeContext newContext;
    newContext.bytes = {0x6e, 0x65, 0x67};

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 262));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdateSkeleton(record.stake_id, newContext, -1));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(record.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nLastUpdateHeight);
    BOOST_CHECK(stored->T == record.T);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_updates_skeleton_updates_all_records_atomically)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord firstRecord = ActiveRecord(186, DeterministicPoint(196));
    const helsing::StakeRecord secondRecord = ActiveRecord(187, DeterministicPoint(197));
    const helsing::StakeRecord untouchedRecord = ActiveRecord(188, DeterministicPoint(198));
    const GroupElement spentOnlyTag = DeterministicPoint(199);
    helsing::StakeContext firstContext;
    helsing::StakeContext secondContext;
    firstContext.bytes = {0x62, 0x61, 0x74, 0x63, 0x68, 0x31};
    secondContext.bytes = {0x62, 0x61, 0x74, 0x63, 0x68, 0x32};

    BOOST_CHECK(state.AddActiveStake(firstRecord));
    BOOST_CHECK(state.AddActiveStake(secondRecord));
    BOOST_CHECK(state.AddActiveStake(untouchedRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 414));

    BOOST_CHECK(state.ApplyAcceptedStakeUpdatesSkeleton({{firstRecord.stake_id, firstContext}, {secondRecord.stake_id, secondContext}}, 415));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(firstRecord.T));
    BOOST_CHECK(state.IsActiveTag(secondRecord.T));
    BOOST_CHECK(state.IsActiveTag(untouchedRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    const helsing::StakeRecord* firstStored = state.GetStakeRecord(firstRecord.stake_id);
    const helsing::StakeRecord* secondStored = state.GetStakeRecord(secondRecord.stake_id);
    const helsing::StakeRecord* untouchedStored = state.GetStakeRecord(untouchedRecord.stake_id);
    BOOST_REQUIRE(firstStored != nullptr);
    BOOST_REQUIRE(secondStored != nullptr);
    BOOST_REQUIRE(untouchedStored != nullptr);
    BOOST_CHECK(firstStored->m.bytes == firstContext.bytes);
    BOOST_CHECK(secondStored->m.bytes == secondContext.bytes);
    BOOST_CHECK(untouchedStored->m.bytes == untouchedRecord.m.bytes);
    BOOST_CHECK_EQUAL(firstStored->nLastUpdateHeight, 415);
    BOOST_CHECK_EQUAL(secondStored->nLastUpdateHeight, 415);
    BOOST_CHECK_EQUAL(untouchedStored->nLastUpdateHeight, untouchedRecord.nLastUpdateHeight);
    BOOST_CHECK(firstStored->T == firstRecord.T);
    BOOST_CHECK(secondStored->T == secondRecord.T);
    BOOST_CHECK(untouchedStored->T == untouchedRecord.T);
    BOOST_CHECK(firstStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(secondStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(untouchedStored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_updates_skeleton_accepts_empty_set)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(189, DeterministicPoint(200));
    const GroupElement spentOnlyTag = DeterministicPoint(201);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 416));

    BOOST_CHECK(state.ApplyAcceptedStakeUpdatesSkeleton({}, 417));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == activeRecord.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, activeRecord.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_updates_skeleton_rejects_negative_height_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord record = ActiveRecord(190, DeterministicPoint(202));
    helsing::StakeContext newContext;
    newContext.bytes = {0x6e, 0x65, 0x67, 0x5f, 0x62, 0x61, 0x74, 0x63, 0x68};

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{record.stake_id, newContext}}, -1));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(record.T));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nLastUpdateHeight);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_stake_updates_skeleton_rejects_invalid_batch_without_partial_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord firstRecord = ActiveRecord(191, DeterministicPoint(203));
    const helsing::StakeRecord secondRecord = ActiveRecord(192, DeterministicPoint(204));
    const helsing::StakeRecord spentRecord = ActiveRecord(193, DeterministicPoint(205));
    const helsing::StakeRecord revokedRecord = ActiveRecord(194, DeterministicPoint(206));
    helsing::StakeContext firstContext;
    helsing::StakeContext secondContext;
    helsing::StakeContext thirdContext;
    firstContext.bytes = {0x66, 0x69, 0x72, 0x73, 0x74};
    secondContext.bytes = {0x73, 0x65, 0x63, 0x6f, 0x6e, 0x64};
    thirdContext.bytes = {0x74, 0x68, 0x69, 0x72, 0x64};

    BOOST_CHECK(state.AddActiveStake(firstRecord));
    BOOST_CHECK(state.AddActiveStake(secondRecord));
    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.AddSpentTag(spentRecord.T, 418));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));

    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{firstRecord.stake_id, firstContext}, {DeterministicHash(195), secondContext}, {secondRecord.stake_id, thirdContext}}, 419));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{firstRecord.stake_id, firstContext}, {firstRecord.stake_id, secondContext}}, 420));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{spentRecord.stake_id, firstContext}}, 421));
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{revokedRecord.stake_id, firstContext}}, 422));

    uint256 nullStakeId;
    BOOST_CHECK(!state.ApplyAcceptedStakeUpdatesSkeleton({{nullStakeId, firstContext}}, 423));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 4U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(firstRecord.T));
    BOOST_CHECK(state.IsActiveTag(secondRecord.T));
    BOOST_CHECK(!state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(!state.IsActiveTag(revokedRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentRecord.T));

    const helsing::StakeRecord* firstStored = state.GetStakeRecord(firstRecord.stake_id);
    const helsing::StakeRecord* secondStored = state.GetStakeRecord(secondRecord.stake_id);
    const helsing::StakeRecord* spentStored = state.GetStakeRecord(spentRecord.stake_id);
    const helsing::StakeRecord* revokedStored = state.GetStakeRecord(revokedRecord.stake_id);
    BOOST_REQUIRE(firstStored != nullptr);
    BOOST_REQUIRE(secondStored != nullptr);
    BOOST_REQUIRE(spentStored != nullptr);
    BOOST_REQUIRE(revokedStored != nullptr);
    BOOST_CHECK(firstStored->m.bytes == firstRecord.m.bytes);
    BOOST_CHECK(secondStored->m.bytes == secondRecord.m.bytes);
    BOOST_CHECK(spentStored->m.bytes == spentRecord.m.bytes);
    BOOST_CHECK(revokedStored->m.bytes == revokedRecord.m.bytes);
    BOOST_CHECK_EQUAL(firstStored->nLastUpdateHeight, firstRecord.nLastUpdateHeight);
    BOOST_CHECK_EQUAL(secondStored->nLastUpdateHeight, secondRecord.nLastUpdateHeight);
    BOOST_CHECK_EQUAL(spentStored->nLastUpdateHeight, spentRecord.nLastUpdateHeight);
    BOOST_CHECK_EQUAL(revokedStored->nLastUpdateHeight, revokedRecord.nLastUpdateHeight);
    BOOST_CHECK(firstStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(secondStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(spentStored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK(revokedStored->status == helsing::StakeStatus::REVOKED);
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(195)) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_applies_spec_steps_atomically)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord updateRecord = ActiveRecord(196, DeterministicPoint(207));
    const helsing::StakeRecord spentRecord = ActiveRecord(197, DeterministicPoint(208));
    const helsing::StakeRecord untouchedRecord = ActiveRecord(198, DeterministicPoint(209));
    const GroupElement spentOnlyTag = DeterministicPoint(210);
    helsing::StakeTx newStake = ValidStakeTx();
    newStake.T = DeterministicPoint(211);
    newStake.m.bytes = {0x6e, 0x65, 0x77};
    helsing::StakeContext updatedContext;
    updatedContext.bytes = {0x75, 0x70, 0x64, 0x61, 0x74, 0x65, 0x64};
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(spentRecord.T);
    blockSpentTags.insert(spentOnlyTag);
    const uint256 newStakeId = DeterministicHash(199);

    BOOST_CHECK(state.AddActiveStake(updateRecord));
    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(untouchedRecord));

    BOOST_CHECK(state.ApplyAcceptedBlockSkeleton(blockSpentTags, {{newStakeId, newStake}}, {{updateRecord.stake_id, updatedContext}}, 424));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 4U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 2U);
    BOOST_CHECK(state.IsSpentTag(spentRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsActiveTag(spentRecord.T));
    BOOST_CHECK(state.IsActiveTag(updateRecord.T));
    BOOST_CHECK(state.IsActiveTag(untouchedRecord.T));
    BOOST_CHECK(state.IsActiveTag(newStake.T));

    const helsing::StakeRecord* updatedStored = state.GetStakeRecord(updateRecord.stake_id);
    const helsing::StakeRecord* spentStored = state.GetStakeRecord(spentRecord.stake_id);
    const helsing::StakeRecord* untouchedStored = state.GetStakeRecord(untouchedRecord.stake_id);
    const helsing::StakeRecord* newStored = state.GetStakeRecord(newStakeId);
    BOOST_REQUIRE(updatedStored != nullptr);
    BOOST_REQUIRE(spentStored != nullptr);
    BOOST_REQUIRE(untouchedStored != nullptr);
    BOOST_REQUIRE(newStored != nullptr);
    BOOST_CHECK(updatedStored->m.bytes == updatedContext.bytes);
    BOOST_CHECK_EQUAL(updatedStored->nLastUpdateHeight, 424);
    BOOST_CHECK(updatedStored->T == updateRecord.T);
    BOOST_CHECK(updatedStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(spentStored->T == spentRecord.T);
    BOOST_CHECK(spentStored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(spentStored->nSpentHeight, 424);
    BOOST_CHECK(untouchedStored->m.bytes == untouchedRecord.m.bytes);
    BOOST_CHECK_EQUAL(untouchedStored->nLastUpdateHeight, untouchedRecord.nLastUpdateHeight);
    BOOST_CHECK(untouchedStored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(newStored->T == newStake.T);
    BOOST_CHECK(newStored->m.bytes == newStake.m.bytes);
    BOOST_CHECK_EQUAL(newStored->nHeight, 424);
    BOOST_CHECK_EQUAL(newStored->nLastUpdateHeight, 424);
    BOOST_CHECK(newStored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_accepts_empty_block)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(200, DeterministicPoint(212));
    const GroupElement spentOnlyTag = DeterministicPoint(213);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 425));

    BOOST_CHECK(state.ApplyAcceptedBlockSkeleton({}, {}, {}, 426));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == activeRecord.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, activeRecord.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_rejects_negative_height_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(201, DeterministicPoint(214));
    helsing::StakeTx newStake = ValidStakeTx();
    newStake.T = DeterministicPoint(215);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(!state.ApplyAcceptedBlockSkeleton({}, {{DeterministicHash(202), newStake}}, {}, -1));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(!state.IsActiveTag(newStake.T));
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(202)) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_rejects_update_spent_in_same_block_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord updateRecord = ActiveRecord(203, DeterministicPoint(216));
    const helsing::StakeRecord untouchedRecord = ActiveRecord(204, DeterministicPoint(217));
    helsing::StakeTx newStake = ValidStakeTx();
    newStake.T = DeterministicPoint(218);
    helsing::StakeContext updatedContext;
    updatedContext.bytes = {0x73, 0x70, 0x65, 0x6e, 0x74, 0x5f, 0x75, 0x70, 0x64};
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(updateRecord.T);
    const uint256 newStakeId = DeterministicHash(205);

    BOOST_CHECK(state.AddActiveStake(updateRecord));
    BOOST_CHECK(state.AddActiveStake(untouchedRecord));

    BOOST_CHECK(!state.ApplyAcceptedBlockSkeleton(blockSpentTags, {{newStakeId, newStake}}, {{updateRecord.stake_id, updatedContext}}, 427));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(updateRecord.T));
    BOOST_CHECK(state.IsActiveTag(untouchedRecord.T));
    BOOST_CHECK(!state.IsSpentTag(updateRecord.T));
    BOOST_CHECK(!state.IsActiveTag(newStake.T));
    BOOST_CHECK(state.GetStakeRecord(newStakeId) == nullptr);

    const helsing::StakeRecord* stored = state.GetStakeRecord(updateRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->m.bytes == updateRecord.m.bytes);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, updateRecord.nLastUpdateHeight);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_rejects_same_block_new_stake_update_without_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(206, DeterministicPoint(219));
    helsing::StakeTx newStake = ValidStakeTx();
    newStake.T = DeterministicPoint(220);
    helsing::StakeContext updatedContext;
    updatedContext.bytes = {0x6e, 0x65, 0x77, 0x5f, 0x75, 0x70, 0x64};
    const uint256 newStakeId = DeterministicHash(207);

    BOOST_CHECK(state.AddActiveStake(activeRecord));

    BOOST_CHECK(!state.ApplyAcceptedBlockSkeleton({}, {{newStakeId, newStake}}, {{newStakeId, updatedContext}}, 428));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(!state.IsActiveTag(newStake.T));
    BOOST_CHECK(state.GetStakeRecord(newStakeId) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_apply_accepted_block_skeleton_rejects_invalid_accepted_stake_without_partial_mutation)
{
    helsing::CHelsingState state;
    const helsing::StakeRecord activeRecord = ActiveRecord(208, DeterministicPoint(221));
    const GroupElement spentOnlyTag = DeterministicPoint(222);
    helsing::StakeTx spentTagStake = ValidStakeTx();
    spentTagStake.T = spentOnlyTag;
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    blockSpentTags.insert(spentOnlyTag);

    BOOST_CHECK(state.AddActiveStake(activeRecord));

    BOOST_CHECK(!state.ApplyAcceptedBlockSkeleton(blockSpentTags, {{DeterministicHash(209), spentTagStake}}, {}, 429));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(activeRecord.T));
    BOOST_CHECK(!state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(state.GetStakeRecord(DeterministicHash(209)) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_active_stake_lifecycle)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(40);
    const helsing::StakeRecord record = ActiveRecord(90, tag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK(activeStakeId == record.stake_id);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->stake_id == record.stake_id);
    BOOST_CHECK(stored->T == tag);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nHeight, record.nHeight);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nHeight);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_add_active_normalizes_record_fields)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(46);
    helsing::StakeRecord record = ActiveRecord(98, tag);
    record.status = helsing::StakeStatus::SPENT;
    record.nSpentHeight = 123;
    record.nLastUpdateHeight = -1;

    BOOST_CHECK(state.AddActiveStake(record));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK(activeStakeId == record.stake_id);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, record.nHeight);

    helsing::StakeRecord revokedRecord = ActiveRecord(102, DeterministicPoint(64));
    revokedRecord.status = helsing::StakeStatus::REVOKED;
    revokedRecord.nSpentHeight = 456;
    BOOST_CHECK(state.AddActiveStake(revokedRecord));

    stored = state.GetStakeRecord(revokedRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, revokedRecord.nLastUpdateHeight);

    helsing::StakeRecord invalidStatusRecord = ActiveRecord(103, DeterministicPoint(65));
    invalidStatusRecord.status = static_cast<helsing::StakeStatus>(255);
    invalidStatusRecord.nSpentHeight = 789;
    BOOST_CHECK(state.AddActiveStake(invalidStatusRecord));

    stored = state.GetStakeRecord(invalidStatusRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, invalidStatusRecord.nLastUpdateHeight);

    helsing::StakeRecord boundaryRecord = ActiveRecord(124, DeterministicPoint(73));
    boundaryRecord.nHeight = 0;
    boundaryRecord.nSpentHeight = std::numeric_limits<int>::max();
    boundaryRecord.status = static_cast<helsing::StakeStatus>(255);
    BOOST_CHECK(state.AddActiveStake(boundaryRecord));

    stored = state.GetStakeRecord(boundaryRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK_EQUAL(stored->nHeight, 0);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK_EQUAL(stored->nLastUpdateHeight, boundaryRecord.nLastUpdateHeight);
}

BOOST_AUTO_TEST_CASE(state_rejects_duplicate_active_stakes_and_spent_tags)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(41);
    const helsing::StakeRecord record = ActiveRecord(91, tag);
    helsing::StakeRecord duplicateStakeId = ActiveRecord(91, DeterministicPoint(47));

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.AddActiveStake(record));
    BOOST_CHECK(!state.AddActiveStake(ActiveRecord(92, tag)));
    BOOST_CHECK(!state.AddActiveStake(duplicateStakeId));
    BOOST_CHECK(!state.IsActiveTag(duplicateStakeId.T));

    BOOST_CHECK(state.AddSpentTag(tag, 120));
    BOOST_CHECK(!state.AddActiveStake(ActiveRecord(93, tag)));
    BOOST_CHECK(!state.AddSpentTag(tag, 121));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
}

BOOST_AUTO_TEST_CASE(state_failed_add_active_stake_is_noop_for_all_indexes)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(53);
    const GroupElement duplicateStakeIdTag = DeterministicPoint(54);
    const GroupElement spentTag = DeterministicPoint(55);
    const helsing::StakeRecord record = ActiveRecord(111, tag);
    const helsing::StakeRecord duplicateStakeId = ActiveRecord(111, duplicateStakeIdTag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(spentTag, 121));

    BOOST_CHECK(!state.AddActiveStake(record));
    BOOST_CHECK(!state.AddActiveStake(ActiveRecord(112, tag)));
    BOOST_CHECK(!state.AddActiveStake(duplicateStakeId));
    BOOST_CHECK(!state.AddActiveStake(ActiveRecord(113, spentTag)));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(tag));
    BOOST_CHECK(state.IsSpentTag(spentTag));
    BOOST_CHECK(!state.IsActiveTag(duplicateStakeIdTag));

    uint256 activeStakeId;
    BOOST_CHECK(state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK(activeStakeId == record.stake_id);
    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->T == tag);
}

BOOST_AUTO_TEST_CASE(state_failed_duplicate_add_active_does_not_overwrite_existing_record)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(74);
    const helsing::StakeRecord record = ActiveRecord(125, tag);
    helsing::StakeRecord duplicateStakeId = ActiveRecord(125, DeterministicPoint(75));
    duplicateStakeId.m.bytes = {0xde, 0xad};
    duplicateStakeId.nHeight = 999;
    duplicateStakeId.nSpentHeight = 888;
    duplicateStakeId.status = helsing::StakeStatus::SPENT;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.AddActiveStake(duplicateStakeId));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->stake_id == record.stake_id);
    BOOST_CHECK(stored->T == record.T);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nHeight, record.nHeight);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(!state.IsActiveTag(duplicateStakeId.T));

    helsing::StakeRecord duplicateTag = ActiveRecord(126, tag);
    duplicateTag.m.bytes = {0xbe, 0xef};
    duplicateTag.nHeight = 1000;
    BOOST_CHECK(!state.AddActiveStake(duplicateTag));

    stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->stake_id == record.stake_id);
    BOOST_CHECK(stored->T == record.T);
    BOOST_CHECK(stored->m.bytes == record.m.bytes);
    BOOST_CHECK_EQUAL(stored->nHeight, record.nHeight);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK(state.GetStakeRecord(duplicateTag.stake_id) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_rejects_null_stake_id)
{
    helsing::CHelsingState state;
    helsing::StakeRecord record = ActiveRecord(94, DeterministicPoint(42));
    record.stake_id.SetNull();

    BOOST_CHECK(!state.AddActiveStake(record));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
}

BOOST_AUTO_TEST_CASE(state_rejects_null_stake_id_without_mutating_populated_state)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(76);
    const GroupElement spentOnlyTag = DeterministicPoint(77);
    const helsing::StakeRecord activeRecord = ActiveRecord(127, activeTag);
    helsing::StakeRecord rejectedRecord = ActiveRecord(128, DeterministicPoint(78));
    rejectedRecord.stake_id.SetNull();

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 173));
    BOOST_CHECK(!state.AddActiveStake(rejectedRecord));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));
    BOOST_CHECK(!state.IsActiveTag(rejectedRecord.T));

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->T == activeTag);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_rejects_nonmember_tags_without_mutation)
{
    helsing::CHelsingState state;
    const GroupElement validTag = DeterministicPoint(66);
    const GroupElement invalidTag = NonMemberPoint();
    const helsing::StakeRecord validRecord = ActiveRecord(104, validTag);
    helsing::StakeRecord invalidRecord = ActiveRecord(105, invalidTag);

    BOOST_CHECK(state.AddActiveStake(validRecord));
    BOOST_CHECK(!state.AddActiveStake(invalidRecord));
    BOOST_CHECK(!state.AddSpentTag(invalidTag, 170));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(validTag));
    BOOST_CHECK(!state.IsActiveTag(invalidTag));
    BOOST_CHECK(!state.IsSpentTag(invalidTag));
    BOOST_CHECK(state.GetStakeRecord(validRecord.stake_id) != nullptr);
    BOOST_CHECK(state.GetStakeRecord(invalidRecord.stake_id) == nullptr);

    uint256 activeStakeId = DeterministicHash(252);
    const uint256 sentinel = activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(invalidTag, activeStakeId));
    BOOST_CHECK(activeStakeId == sentinel);
}

BOOST_AUTO_TEST_CASE(state_spent_tag_deactivates_matching_stake)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(43);
    const helsing::StakeRecord record = ActiveRecord(95, tag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(tag, 130));

    BOOST_CHECK(state.IsSpentTag(tag));
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);

    uint256 activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(tag, activeStakeId));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, 130);

    BOOST_CHECK(!state.AddSpentTag(tag, 131));
    stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, 130);
}

BOOST_AUTO_TEST_CASE(state_get_active_stake_id_failure_preserves_output_after_deactivation)
{
    helsing::CHelsingState state;
    const GroupElement spentTag = DeterministicPoint(67);
    const GroupElement revokedTag = DeterministicPoint(68);
    const helsing::StakeRecord spentRecord = ActiveRecord(106, spentTag);
    const helsing::StakeRecord revokedRecord = ActiveRecord(107, revokedTag);

    BOOST_CHECK(state.AddActiveStake(spentRecord));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.AddSpentTag(spentTag, 171));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));

    uint256 activeStakeId = DeterministicHash(250);
    const uint256 spentSentinel = activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(spentTag, activeStakeId));
    BOOST_CHECK(activeStakeId == spentSentinel);

    activeStakeId = DeterministicHash(251);
    const uint256 revokedSentinel = activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(revokedTag, activeStakeId));
    BOOST_CHECK(activeStakeId == revokedSentinel);

    const GroupElement unknownTag = DeterministicPoint(79);
    activeStakeId = DeterministicHash(253);
    const uint256 unknownSentinel = activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(unknownTag, activeStakeId));
    BOOST_CHECK(activeStakeId == unknownSentinel);

    activeStakeId = DeterministicHash(254);
    const uint256 invalidSentinel = activeStakeId;
    BOOST_CHECK(!state.GetActiveStakeId(NonMemberPoint(), activeStakeId));
    BOOST_CHECK(activeStakeId == invalidSentinel);
}

BOOST_AUTO_TEST_CASE(state_spending_one_of_multiple_active_stakes_updates_only_matching_indexes)
{
    helsing::CHelsingState state;
    const GroupElement tagA = DeterministicPoint(56);
    const GroupElement tagB = DeterministicPoint(57);
    const helsing::StakeRecord recordA = ActiveRecord(114, tagA);
    const helsing::StakeRecord recordB = ActiveRecord(115, tagB);

    BOOST_CHECK(state.AddActiveStake(recordA));
    BOOST_CHECK(state.AddActiveStake(recordB));
    BOOST_CHECK(state.AddSpentTag(tagA, 140));

    BOOST_CHECK(state.IsSpentTag(tagA));
    BOOST_CHECK(!state.IsActiveTag(tagA));
    BOOST_CHECK(!state.IsSpentTag(tagB));
    BOOST_CHECK(state.IsActiveTag(tagB));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 2U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);

    const helsing::StakeRecord* storedA = state.GetStakeRecord(recordA.stake_id);
    const helsing::StakeRecord* storedB = state.GetStakeRecord(recordB.stake_id);
    BOOST_REQUIRE(storedA != nullptr);
    BOOST_REQUIRE(storedB != nullptr);
    BOOST_CHECK(storedA->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(storedA->nSpentHeight, 140);
    BOOST_CHECK(storedB->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(storedB->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_duplicate_spent_only_tag_does_not_touch_unrelated_active_record)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(80);
    const GroupElement spentOnlyTag = DeterministicPoint(81);
    const helsing::StakeRecord activeRecord = ActiveRecord(129, activeTag);

    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK(state.AddSpentTag(spentOnlyTag, 174));
    BOOST_CHECK(!state.AddSpentTag(spentOnlyTag, 175));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentOnlyTag));

    const helsing::StakeRecord* stored = state.GetStakeRecord(activeRecord.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->T == activeTag);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_revoke_active_stake)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(44);
    const helsing::StakeRecord record = ActiveRecord(96, tag);

    BOOST_CHECK(!state.RevokeStake(record.stake_id));
    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.RevokeStake(record.stake_id));
    BOOST_CHECK(!state.RevokeStake(record.stake_id));

    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::REVOKED);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_revoke_unknown_stake_is_noop_on_populated_state)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(58);
    const GroupElement spentTag = DeterministicPoint(59);
    const helsing::StakeRecord record = ActiveRecord(116, activeTag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(spentTag, 150));
    BOOST_CHECK(!state.RevokeStake(DeterministicHash(117)));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentTag));
    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
}

BOOST_AUTO_TEST_CASE(state_revoke_null_stake_id_is_noop_on_populated_state)
{
    helsing::CHelsingState state;
    const GroupElement activeTag = DeterministicPoint(82);
    const GroupElement spentTag = DeterministicPoint(83);
    const helsing::StakeRecord record = ActiveRecord(130, activeTag);
    uint256 nullStakeId;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(spentTag, 176));
    BOOST_CHECK(!state.RevokeStake(nullStakeId));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);
    BOOST_CHECK(state.IsActiveTag(activeTag));
    BOOST_CHECK(state.IsSpentTag(spentTag));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::ACTIVE);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, -1);
}

BOOST_AUTO_TEST_CASE(state_revoke_keeps_record_and_frees_active_tag_index)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(60);
    const helsing::StakeRecord record = ActiveRecord(118, tag);
    uint256 activeStakeId;

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.RevokeStake(record.stake_id));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK(!state.GetActiveStakeId(tag, activeStakeId));
    BOOST_CHECK(!state.AddActiveStake(record));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::REVOKED);
}

BOOST_AUTO_TEST_CASE(state_revoke_rejects_spent_stake)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(48);
    const helsing::StakeRecord record = ActiveRecord(99, tag);

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(tag, 150));
    BOOST_CHECK(!state.RevokeStake(record.stake_id));

    const helsing::StakeRecord* stored = state.GetStakeRecord(record.stake_id);
    BOOST_REQUIRE(stored != nullptr);
    BOOST_CHECK(stored->status == helsing::StakeStatus::SPENT);
    BOOST_CHECK_EQUAL(stored->nSpentHeight, 150);
}

BOOST_AUTO_TEST_CASE(state_invalid_tag_operations_are_noop_on_populated_state)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(61);
    const helsing::StakeRecord record = ActiveRecord(119, tag);
    helsing::StakeRecord invalidRecord = ActiveRecord(120, GroupElement());

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(!state.AddActiveStake(invalidRecord));
    BOOST_CHECK(!state.AddSpentTag(GroupElement(), 160));

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK(state.GetStakeRecord(record.stake_id) != nullptr);
    BOOST_CHECK(state.GetStakeRecord(invalidRecord.stake_id) == nullptr);
}

BOOST_AUTO_TEST_CASE(state_reset_clears_all_indexes)
{
    helsing::CHelsingState state;
    const GroupElement tag = DeterministicPoint(45);
    const helsing::StakeRecord record = ActiveRecord(97, tag);
    const helsing::StakeRecord revokedRecord = ActiveRecord(100, DeterministicPoint(49));
    const helsing::StakeRecord activeRecord = ActiveRecord(101, DeterministicPoint(50));

    BOOST_CHECK(state.AddActiveStake(record));
    BOOST_CHECK(state.AddSpentTag(tag, 140));
    BOOST_CHECK(state.AddActiveStake(revokedRecord));
    BOOST_CHECK(state.RevokeStake(revokedRecord.stake_id));
    BOOST_CHECK(state.AddActiveStake(activeRecord));
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 3U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 1U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 1U);

    state.Reset();

    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(tag));
    BOOST_CHECK(!state.IsSpentTag(tag));
    BOOST_CHECK(state.GetStakeRecord(record.stake_id) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(revokedRecord.stake_id) == nullptr);
    BOOST_CHECK(state.GetStakeRecord(activeRecord.stake_id) == nullptr);
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

BOOST_AUTO_TEST_CASE(accepts_mint_and_spend_output_types)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[0]].type = helsing::SparkOutputType::SPEND;
    view.sparkOutputs[tx.inCoinIDs[1]].type = helsing::SparkOutputType::SPEND;

    BOOST_CHECK(helsing::IsValidSparkOutputRecord(view.sparkOutputs[tx.inCoinIDs[0]]));
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(validation_accepts_single_input_current_skeleton)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(8, 0)};

    helsing::ValidationStateView view;
    view.sparkOutputs.emplace(tx.inCoinIDs[0], EligibleOutput(tx.inCoinIDs[0], 90));

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(rejects_mismatched_output_record_id)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[0]].output_id = Output(99, 0);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);
}

BOOST_AUTO_TEST_CASE(rejects_malformed_output_records)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[0]].S = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].C = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].C = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].K = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].K = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].nHeight = -1;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].type = helsing::SparkOutputType::UNKNOWN;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].type = static_cast<helsing::SparkOutputType>(255);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].type = static_cast<helsing::SparkOutputType>(3);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);
}

BOOST_AUTO_TEST_CASE(rejects_missing_second_output)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs.erase(tx.inCoinIDs[1]);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(rejects_bad_second_output_records)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[1]].output_id = Output(99, 0);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].S = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].C = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].K = NonMemberPoint();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].nHeight = -1;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].type = static_cast<helsing::SparkOutputType>(255);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);
}

BOOST_AUTO_TEST_CASE(validation_checks_all_outputs_beyond_second)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(1, 0), Output(2, 0), Output(3, 0)};
    helsing::ValidationStateView view = ValidView(tx);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    view = ValidView(tx);
    view.sparkOutputs.emplace(tx.inCoinIDs[2], EligibleOutput(tx.inCoinIDs[2], 70));
    view.sparkOutputs[tx.inCoinIDs[2]].output_id = Output(99, 0);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);

    view = ValidView(tx);
    view.sparkOutputs.emplace(tx.inCoinIDs[2], EligibleOutput(tx.inCoinIDs[2], 70));
    view.sparkOutputs[tx.inCoinIDs[2]].S = GroupElement();
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);

    view = ValidView(tx);
    view.sparkOutputs.emplace(tx.inCoinIDs[2], EligibleOutput(tx.inCoinIDs[2], 70));
    view.sparkOutputs[tx.inCoinIDs[2]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);

    view.sparkOutputs[tx.inCoinIDs[2]].helsing_eligible = true;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(validation_uses_full_output_id_not_txid_only)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(9, 1)};

    helsing::ValidationStateView view;
    view.sparkOutputs.emplace(Output(9, 0), EligibleOutput(Output(9, 0), 60));

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);
}

BOOST_AUTO_TEST_CASE(validation_output_identity_allows_duplicate_serial_commitments)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(9, 0), Output(10, 0)};
    helsing::ValidationStateView view = ValidView(tx);

    const GroupElement sharedS = DeterministicPoint(70);
    view.sparkOutputs[tx.inCoinIDs[0]].S = sharedS;
    view.sparkOutputs[tx.inCoinIDs[1]].S = sharedS;

    BOOST_CHECK(view.sparkOutputs[tx.inCoinIDs[0]].output_id != view.sparkOutputs[tx.inCoinIDs[1]].output_id);
    BOOST_CHECK(view.sparkOutputs[tx.inCoinIDs[0]].S == view.sparkOutputs[tx.inCoinIDs[1]].S);
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_CASE(validation_output_loop_returns_first_failure)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    view.sparkOutputs.erase(tx.inCoinIDs[0]);
    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = false;

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_FOUND);

    tx = ValidStakeTx();
    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].helsing_eligible = false;
    view.sparkOutputs.erase(tx.inCoinIDs[1]);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE);
}

BOOST_AUTO_TEST_CASE(validation_output_record_precedence_is_stable)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[0]].output_id = Output(99, 0);
    view.sparkOutputs[tx.inCoinIDs[0]].S = GroupElement();
    view.sparkOutputs[tx.inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[0]].S = GroupElement();
    view.sparkOutputs[tx.inCoinIDs[0]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);
}

BOOST_AUTO_TEST_CASE(validation_output_record_precedence_is_stable_on_second_output)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    view.sparkOutputs[tx.inCoinIDs[1]].output_id = Output(99, 0);
    view.sparkOutputs[tx.inCoinIDs[1]].S = GroupElement();
    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OUTPUT_ID_MISMATCH);

    view = ValidView(tx);
    view.sparkOutputs[tx.inCoinIDs[1]].S = GroupElement();
    view.sparkOutputs[tx.inCoinIDs[1]].helsing_eligible = false;
    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::INVALID_OUTPUT_RECORD);
}

BOOST_AUTO_TEST_CASE(validation_state_view_lookup_helpers)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    const helsing::SparkOutputRecord* found = view.FindSparkOutput(tx.inCoinIDs[0]);
    BOOST_REQUIRE(found != nullptr);
    CheckOutputIdEqual(found->output_id, tx.inCoinIDs[0]);
    BOOST_CHECK(found->S == DeterministicPoint(20));
    BOOST_CHECK(found->C == DeterministicPoint(21));
    BOOST_CHECK(found->K == DeterministicPoint(22));
    BOOST_CHECK_EQUAL(found->nHeight, 100);
    BOOST_CHECK(found->type == helsing::SparkOutputType::MINT);
    BOOST_CHECK(found->helsing_eligible);

    BOOST_CHECK(view.FindSparkOutput(Output(99, 0)) == nullptr);
    BOOST_CHECK(view.FindSparkOutput(helsing::OutputId(tx.inCoinIDs[0].txid, tx.inCoinIDs[0].vout + 1)) == nullptr);
    BOOST_CHECK(!view.HasBlockSpentTag(tx.T));
    view.blockSpentTags.insert(tx.T);
    BOOST_CHECK(view.HasBlockSpentTag(tx.T));
    BOOST_CHECK(!view.HasBlockSpentTag(DeterministicPoint(91)));
}

BOOST_AUTO_TEST_CASE(validation_result_strings_cover_all_values)
{
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::OK), "OK");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::EMPTY_INCOINIDS), "EMPTY_INCOINIDS");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::INVALID_COVER_SET_CARDINALITY), "INVALID_COVER_SET_CARDINALITY");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT), "INCOINIDS_NOT_SORTED_DISTINCT");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::INVALID_OUTPUT_ID), "INVALID_OUTPUT_ID");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::INVALID_GROUP_ELEMENT), "INVALID_GROUP_ELEMENT");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::MISSING_PROOF), "MISSING_PROOF");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::TAG_ALREADY_SPENT), "TAG_ALREADY_SPENT");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::TAG_SPENT_IN_BLOCK), "TAG_SPENT_IN_BLOCK");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::DUPLICATE_SPENT_TAG_IN_BLOCK), "DUPLICATE_SPENT_TAG_IN_BLOCK");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::TAG_ALREADY_ACTIVE), "TAG_ALREADY_ACTIVE");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK), "DUPLICATE_STAKE_TAG_IN_BLOCK");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::STAKE_RECORD_NOT_FOUND), "STAKE_RECORD_NOT_FOUND");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::STAKE_NOT_ACTIVE), "STAKE_NOT_ACTIVE");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::STAKE_NOT_MATURE), "STAKE_NOT_MATURE");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::OUTPUT_NOT_FOUND), "OUTPUT_NOT_FOUND");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::OUTPUT_ID_MISMATCH), "OUTPUT_ID_MISMATCH");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::INVALID_OUTPUT_RECORD), "INVALID_OUTPUT_RECORD");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(helsing::StakeValidationResult::OUTPUT_NOT_ELIGIBLE), "OUTPUT_NOT_ELIGIBLE");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(static_cast<helsing::StakeValidationResult>(255)), "UNKNOWN");
    BOOST_CHECK_EQUAL(helsing::StakeValidationResultToString(static_cast<helsing::StakeValidationResult>(-1)), "UNKNOWN");
}

BOOST_AUTO_TEST_CASE(stake_tx_serialization_roundtrip)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs.push_back(Output(3, 7));
    tx.m.bytes = {0x01, 0x02, 0x03, 0x04};
    tx.pi_par.bytes = {0x10, 0x11};
    tx.pi_val.bytes = {0x20, 0x21, 0x22};
    tx.pi_tag.bytes = {0x30, 0x31, 0x32, 0x33};

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::StakeTx decoded;
    stream >> decoded;

    CheckStakeTxEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(stake_tx_wire_regression_vectors)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs.push_back(Output(3, 7));
    tx.m.bytes = {0x01, 0x02, 0x03, 0x04};
    tx.pi_par.bytes = {0x10, 0x11};
    tx.pi_val.bytes = {0x20, 0x21, 0x22};
    tx.pi_tag.bytes = {0x30, 0x31, 0x32, 0x33};

    const std::string output1 = WireHex(Output(1, 0));
    const std::string output2 = WireHex(Output(2, 0));
    const std::string output3 = WireHex(Output(3, 7));
    const std::string sPrime = WireHex(tx.S_prime);
    const std::string cPrime = WireHex(tx.C_prime);
    const std::string tag = WireHex(tx.T);
    const std::string context = WireHex(tx.m);
    const std::string parProof = WireHex(tx.pi_par);
    const std::string valProof = WireHex(tx.pi_val);
    const std::string tagProof = WireHex(tx.pi_tag);

    BOOST_CHECK_EQUAL(output1, "010000000000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(output2, "020000000000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(output3, "030000000000000000000000000000000000000000000000000000000000000007000000");
    BOOST_CHECK_EQUAL(WireHex(helsing::StakeContext()), "00");
    BOOST_CHECK_EQUAL(context, "0401020304");
    BOOST_CHECK_EQUAL(parProof, "021011");
    BOOST_CHECK_EQUAL(valProof, "03202122");
    BOOST_CHECK_EQUAL(tagProof, "0430313233");
    BOOST_CHECK_EQUAL(sPrime, "f142f9527f87078bb7ab339db4642cb9a27d402ffcaa7619951e938555cf3dbe0000");
    BOOST_CHECK_EQUAL(cPrime, "7fd78938d3d576859f86f568ad342cd05357543aede3a6a48cedda6ff2c9b2350100");
    BOOST_CHECK_EQUAL(tag, "06c3ae6d399a9cf1a3445af93452102d0eceae6918c731e20294c324bf4419a20000");
    BOOST_CHECK_EQUAL(WireHex(tx), std::string("03") + output1 + output2 + output3 + sPrime + cPrime + tag + context + parProof + valProof + tagProof);
    BOOST_CHECK_EQUAL(WireHash(tx).ToString(), "1a3000cbdb67ca49f60d467cb72e9332566a5e97ddf17f52890dcec89ab6ead5");
}

BOOST_AUTO_TEST_CASE(stake_tx_wire_hash_is_field_sensitive)
{
    const helsing::StakeTx tx = ValidStakeTx();
    const uint256 baseHash = WireHash(tx);

    helsing::StakeTx changed = tx;
    changed.inCoinIDs[0] = Output(9, 0);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.S_prime = DeterministicPoint(21);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.C_prime = DeterministicPoint(22);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.T = DeterministicPoint(23);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.m.bytes.push_back(0x04);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.pi_par.bytes.push_back(0x10);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.pi_val.bytes.push_back(0x20);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.pi_tag.bytes.push_back(0x30);
    BOOST_CHECK(WireHash(changed) != baseHash);
}

BOOST_AUTO_TEST_CASE(stake_update_tx_serialization_roundtrip)
{
    helsing::StakeUpdateTx tx;
    tx.stake_id = DeterministicHash(151);
    tx.m_new.bytes = {0x6d, 0x5f, 0x6e, 0x65, 0x77};
    tx.sig_update.bytes = {0x30, 0x44, 0x02, 0x20};

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::StakeUpdateTx decoded;
    stream >> decoded;

    CheckStakeUpdateTxEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(stake_update_tx_wire_regression_vectors)
{
    helsing::StakeUpdateTx tx;
    tx.stake_id = DeterministicHash(152);
    tx.m_new.bytes = {0x6d, 0x5f, 0x6e, 0x65, 0x77};
    tx.sig_update.bytes = {0x30, 0x44, 0x02, 0x20};

    const std::string stakeId = WireHex(tx.stake_id);
    const std::string newContext = WireHex(tx.m_new);
    const std::string updateSig = WireHex(tx.sig_update);

    BOOST_CHECK_EQUAL(stakeId, "9800000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(newContext, "056d5f6e6577");
    BOOST_CHECK_EQUAL(updateSig, "0430440220");
    BOOST_CHECK_EQUAL(WireHex(tx), stakeId + newContext + updateSig);
}

BOOST_AUTO_TEST_CASE(stake_update_tx_wire_hash_is_field_sensitive)
{
    helsing::StakeUpdateTx tx;
    tx.stake_id = DeterministicHash(153);
    tx.m_new.bytes = {0x6d, 0x5f, 0x6e, 0x65, 0x77};
    tx.sig_update.bytes = {0x30, 0x44, 0x02, 0x20};
    const uint256 baseHash = WireHash(tx);

    helsing::StakeUpdateTx changed = tx;
    changed.stake_id = DeterministicHash(154);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.m_new.bytes.push_back(0x01);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.sig_update.bytes.push_back(0x01);
    BOOST_CHECK(WireHash(changed) != baseHash);
}

BOOST_AUTO_TEST_CASE(payout_tx_skeleton_serialization_roundtrip)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(160);
    tx.payout_index = 7;
    tx.addr_pk.bytes = {0x64, 0x51, 0x31};
    tx.V_PAYOUT = 42;
    tx.coin.bytes = {0x53, 0x4b, 0x43};

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::PayoutTxSkeleton decoded;
    stream >> decoded;

    CheckPayoutTxSkeletonEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(payout_tx_skeleton_wire_regression_vectors)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(160);
    tx.payout_index = 7;
    tx.addr_pk.bytes = {0x64, 0x51, 0x31};
    tx.V_PAYOUT = 42;
    tx.coin.bytes = {0x53, 0x4b, 0x43};

    const std::string stakeId = WireHex(tx.selected_stake_id);
    const std::string payoutIndex = "07000000";
    const std::string payoutAddress = WireHex(tx.addr_pk);
    const std::string payoutValue = "2a00000000000000";
    const std::string payoutCoin = WireHex(tx.coin);

    BOOST_CHECK_EQUAL(stakeId, "a000000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(payoutAddress, "03645131");
    BOOST_CHECK_EQUAL(payoutCoin, "03534b43");
    BOOST_CHECK_EQUAL(WireHex(tx), stakeId + payoutIndex + payoutAddress + payoutValue + payoutCoin);
}

BOOST_AUTO_TEST_CASE(payout_tx_skeleton_wire_hash_is_field_sensitive)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(161);
    tx.payout_index = 8;
    tx.addr_pk.bytes = {0x64, 0x51, 0x32};
    tx.V_PAYOUT = 100;
    tx.coin.bytes = {0x53, 0x4b, 0x43, 0x32};
    const uint256 baseHash = WireHash(tx);

    helsing::PayoutTxSkeleton changed = tx;
    changed.selected_stake_id = DeterministicHash(162);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    ++changed.payout_index;
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.addr_pk.bytes.push_back(0x01);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    ++changed.V_PAYOUT;
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = tx;
    changed.coin.bytes.push_back(0x01);
    BOOST_CHECK(WireHash(changed) != baseHash);
}

BOOST_AUTO_TEST_CASE(payout_block_context_skeleton_serialization_roundtrip)
{
    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 123456;
    context.prev_block_hash = DeterministicHash(171);
    context.payout_index = 9;
    context.selected_stake_id = DeterministicHash(172);

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << context;

    helsing::PayoutBlockContextSkeleton decoded;
    stream >> decoded;

    CheckPayoutBlockContextSkeletonEqual(decoded, context);
}

BOOST_AUTO_TEST_CASE(payout_block_context_skeleton_wire_regression_vectors)
{
    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 123456;
    context.prev_block_hash = DeterministicHash(171);
    context.payout_index = 9;
    context.selected_stake_id = DeterministicHash(172);

    const std::string chainId = "046669726f";
    const std::string blockHeight = "40e20100";
    const std::string prevBlockHash = WireHex(context.prev_block_hash);
    const std::string payoutIndex = "09000000";
    const std::string selectedStakeId = WireHex(context.selected_stake_id);

    BOOST_CHECK_EQUAL(prevBlockHash, "ab00000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(selectedStakeId, "ac00000000000000000000000000000000000000000000000000000000000000");
    BOOST_CHECK_EQUAL(WireHex(context), chainId + blockHeight + prevBlockHash + payoutIndex + selectedStakeId);
}

BOOST_AUTO_TEST_CASE(payout_block_context_skeleton_wire_hash_is_field_sensitive)
{
    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 123456;
    context.prev_block_hash = DeterministicHash(173);
    context.payout_index = 10;
    context.selected_stake_id = DeterministicHash(174);
    const uint256 baseHash = WireHash(context);

    helsing::PayoutBlockContextSkeleton changed = context;
    changed.chain_id.push_back(0x00);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = context;
    ++changed.block_height;
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = context;
    changed.prev_block_hash = DeterministicHash(175);
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = context;
    ++changed.payout_index;
    BOOST_CHECK(WireHash(changed) != baseHash);

    changed = context;
    changed.selected_stake_id = DeterministicHash(176);
    BOOST_CHECK(WireHash(changed) != baseHash);
}

BOOST_AUTO_TEST_CASE(payout_block_context_completeness_skeleton_accepts_populated_fields)
{
    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 0;
    context.prev_block_hash = DeterministicHash(177);
    context.payout_index = 0;
    context.selected_stake_id = DeterministicHash(178);

    BOOST_CHECK(helsing::IsCompletePayoutBlockContextSkeleton(context));

    context.block_height = std::numeric_limits<int32_t>::max();
    context.payout_index = std::numeric_limits<uint32_t>::max();
    BOOST_CHECK(helsing::IsCompletePayoutBlockContextSkeleton(context));
}

BOOST_AUTO_TEST_CASE(payout_block_context_completeness_skeleton_rejects_missing_fields)
{
    helsing::PayoutBlockContextSkeleton context;
    BOOST_CHECK(!helsing::IsCompletePayoutBlockContextSkeleton(context));

    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 1;
    context.prev_block_hash = DeterministicHash(179);
    context.selected_stake_id = DeterministicHash(180);
    BOOST_REQUIRE(helsing::IsCompletePayoutBlockContextSkeleton(context));

    helsing::PayoutBlockContextSkeleton changed = context;
    changed.chain_id.clear();
    BOOST_CHECK(!helsing::IsCompletePayoutBlockContextSkeleton(changed));

    changed = context;
    changed.block_height = -1;
    BOOST_CHECK(!helsing::IsCompletePayoutBlockContextSkeleton(changed));

    changed = context;
    changed.prev_block_hash.SetNull();
    BOOST_CHECK(!helsing::IsCompletePayoutBlockContextSkeleton(changed));

    changed = context;
    changed.selected_stake_id.SetNull();
    BOOST_CHECK(!helsing::IsCompletePayoutBlockContextSkeleton(changed));
}

BOOST_AUTO_TEST_CASE(payout_block_context_completeness_skeleton_does_not_define_chain_id_grammar)
{
    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x00};
    context.block_height = 2;
    context.prev_block_hash = DeterministicHash(181);
    context.payout_index = 11;
    context.selected_stake_id = DeterministicHash(182);
    BOOST_CHECK(helsing::IsCompletePayoutBlockContextSkeleton(context));

    context.chain_id = {0xff, 0x00, 0x66};
    context.payout_index = std::numeric_limits<uint32_t>::max();
    BOOST_CHECK(helsing::IsCompletePayoutBlockContextSkeleton(context));
}

BOOST_AUTO_TEST_CASE(payout_context_match_skeleton_compares_payout_index_and_selected_stake)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(235);
    tx.payout_index = 12;
    tx.addr_pk.bytes = {0x61, 0x64, 0x64, 0x72};
    tx.V_PAYOUT = 50;
    tx.coin.bytes = {0x63, 0x6f, 0x69, 0x6e};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 300;
    context.prev_block_hash = DeterministicHash(236);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    BOOST_CHECK(helsing::DoesPayoutContextMatchTxSkeleton(tx, context));
}

BOOST_AUTO_TEST_CASE(payout_context_match_skeleton_rejects_mismatched_identity_fields)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(237);
    tx.payout_index = 13;

    helsing::PayoutBlockContextSkeleton context;
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;
    BOOST_REQUIRE(helsing::DoesPayoutContextMatchTxSkeleton(tx, context));

    helsing::PayoutBlockContextSkeleton changed = context;
    ++changed.payout_index;
    BOOST_CHECK(!helsing::DoesPayoutContextMatchTxSkeleton(tx, changed));

    changed = context;
    changed.selected_stake_id = DeterministicHash(238);
    BOOST_CHECK(!helsing::DoesPayoutContextMatchTxSkeleton(tx, changed));
}

BOOST_AUTO_TEST_CASE(payout_context_match_skeleton_ignores_fields_outside_payout_id_identity)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(239);
    tx.payout_index = 14;
    tx.addr_pk.bytes = {0x61};
    tx.V_PAYOUT = 1;
    tx.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 1;
    context.prev_block_hash = DeterministicHash(240);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    BOOST_REQUIRE(helsing::DoesPayoutContextMatchTxSkeleton(tx, context));

    helsing::PayoutTxSkeleton changedTx = tx;
    changedTx.addr_pk.bytes = {0x64};
    changedTx.V_PAYOUT = 2;
    changedTx.coin.bytes = {0x64};
    BOOST_CHECK(helsing::DoesPayoutContextMatchTxSkeleton(changedTx, context));

    helsing::PayoutBlockContextSkeleton changedContext = context;
    changedContext.chain_id = {0x64};
    changedContext.block_height = 2;
    changedContext.prev_block_hash = DeterministicHash(241);
    BOOST_CHECK(helsing::DoesPayoutContextMatchTxSkeleton(tx, changedContext));
}

BOOST_AUTO_TEST_CASE(payout_id_input_skeleton_accepts_complete_context_and_matching_tx_identity)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(242);
    tx.payout_index = 15;
    BOOST_REQUIRE(!helsing::IsCompletePayoutTxSkeleton(tx));

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66, 0x69, 0x72, 0x6f};
    context.block_height = 500;
    context.prev_block_hash = DeterministicHash(243);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;

    BOOST_CHECK(helsing::ArePayoutIdInputsCompleteSkeleton(tx, context));
}

BOOST_AUTO_TEST_CASE(payout_id_input_skeleton_rejects_incomplete_context)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(244);
    tx.payout_index = 16;

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 501;
    context.prev_block_hash = DeterministicHash(245);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;
    BOOST_REQUIRE(helsing::ArePayoutIdInputsCompleteSkeleton(tx, context));

    helsing::PayoutBlockContextSkeleton changed = context;
    changed.chain_id.clear();
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));

    changed = context;
    changed.block_height = -1;
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));

    changed = context;
    changed.prev_block_hash.SetNull();
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));

    changed = context;
    changed.selected_stake_id.SetNull();
    tx.selected_stake_id.SetNull();
    BOOST_REQUIRE(helsing::DoesPayoutContextMatchTxSkeleton(tx, changed));
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));
}

BOOST_AUTO_TEST_CASE(payout_id_input_skeleton_rejects_tx_context_identity_mismatch)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(246);
    tx.payout_index = 17;

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 502;
    context.prev_block_hash = DeterministicHash(247);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;
    BOOST_REQUIRE(helsing::ArePayoutIdInputsCompleteSkeleton(tx, context));

    helsing::PayoutBlockContextSkeleton changed = context;
    ++changed.payout_index;
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));

    changed = context;
    changed.selected_stake_id = DeterministicHash(248);
    BOOST_CHECK(!helsing::ArePayoutIdInputsCompleteSkeleton(tx, changed));
}

BOOST_AUTO_TEST_CASE(payout_id_input_skeleton_ignores_fields_outside_identifier)
{
    helsing::PayoutTxSkeleton tx;
    tx.selected_stake_id = DeterministicHash(249);
    tx.payout_index = 18;
    tx.addr_pk.bytes = {0x61};
    tx.V_PAYOUT = 1;
    tx.coin.bytes = {0x63};

    helsing::PayoutBlockContextSkeleton context;
    context.chain_id = {0x66};
    context.block_height = 503;
    context.prev_block_hash = DeterministicHash(250);
    context.payout_index = tx.payout_index;
    context.selected_stake_id = tx.selected_stake_id;
    BOOST_REQUIRE(helsing::ArePayoutIdInputsCompleteSkeleton(tx, context));

    helsing::PayoutTxSkeleton changed = tx;
    changed.addr_pk.bytes = {0x64};
    changed.V_PAYOUT = 2;
    changed.coin.bytes = {0x64};
    BOOST_CHECK(helsing::ArePayoutIdInputsCompleteSkeleton(changed, context));
}

BOOST_AUTO_TEST_CASE(payout_index_skeleton_accepts_empty_single_and_distinct_indexes)
{
    BOOST_CHECK(helsing::ArePayoutIndexesDistinctSkeleton({}));

    helsing::PayoutTxSkeleton first;
    first.selected_stake_id = DeterministicHash(163);
    first.payout_index = 0;

    helsing::PayoutTxSkeleton second;
    second.selected_stake_id = DeterministicHash(164);
    second.payout_index = 1;

    helsing::PayoutTxSkeleton third;
    third.selected_stake_id = DeterministicHash(165);
    third.payout_index = std::numeric_limits<uint32_t>::max();

    BOOST_CHECK(helsing::ArePayoutIndexesDistinctSkeleton({first}));
    BOOST_CHECK(helsing::ArePayoutIndexesDistinctSkeleton({first, second, third}));
}

BOOST_AUTO_TEST_CASE(payout_index_skeleton_rejects_duplicate_indexes)
{
    helsing::PayoutTxSkeleton first;
    first.selected_stake_id = DeterministicHash(166);
    first.payout_index = 2;

    helsing::PayoutTxSkeleton duplicate = first;
    duplicate.selected_stake_id = DeterministicHash(167);

    helsing::PayoutTxSkeleton later;
    later.selected_stake_id = DeterministicHash(168);
    later.payout_index = 3;

    BOOST_CHECK(!helsing::ArePayoutIndexesDistinctSkeleton({first, duplicate}));
    BOOST_CHECK(!helsing::ArePayoutIndexesDistinctSkeleton({first, later, duplicate}));
}

BOOST_AUTO_TEST_CASE(payout_index_skeleton_treats_stake_id_and_amount_as_irrelevant)
{
    helsing::PayoutTxSkeleton first;
    first.selected_stake_id = DeterministicHash(169);
    first.payout_index = 4;
    first.V_PAYOUT = 10;

    helsing::PayoutTxSkeleton second = first;
    second.payout_index = 5;
    second.V_PAYOUT = 10;

    helsing::PayoutTxSkeleton duplicate = first;
    duplicate.selected_stake_id = DeterministicHash(170);
    duplicate.V_PAYOUT = 11;

    BOOST_CHECK(helsing::ArePayoutIndexesDistinctSkeleton({first, second}));
    BOOST_CHECK(!helsing::ArePayoutIndexesDistinctSkeleton({first, second, duplicate}));
}

BOOST_AUTO_TEST_CASE(stake_wire_deserialization_rejects_truncated_or_noncanonical_streams)
{
    std::vector<unsigned char> serializedOutput = ParseHex(WireHex(Output(1, 0)));
    BOOST_REQUIRE(!serializedOutput.empty());
    serializedOutput.pop_back();

    CDataStream truncatedOutput(serializedOutput, SER_NETWORK, PROTOCOL_VERSION);
    helsing::OutputId decodedOutput;
    BOOST_CHECK_THROW(truncatedOutput >> decodedOutput, std::ios_base::failure);

    const std::vector<unsigned char> serializedTx = ParseHex(WireHex(ValidStakeTx()));
    BOOST_REQUIRE(!serializedTx.empty());
    for (size_t prefixSize = 0; prefixSize < serializedTx.size(); ++prefixSize) {
        BOOST_TEST_CONTEXT("prefixSize=" << prefixSize)
        {
            std::vector<unsigned char> prefix(serializedTx.begin(), serializedTx.begin() + prefixSize);
            CDataStream truncated(prefix, SER_NETWORK, PROTOCOL_VERSION);
            helsing::StakeTx decodedTx;
            BOOST_CHECK_THROW(truncated >> decodedTx, std::ios_base::failure);
        }
    }

    CDataStream nonCanonicalInCoinIDs(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalInCoinIDs.write("\xfd\x00\x00", 3);
    helsing::StakeTx decodedTx;
    BOOST_CHECK_THROW(nonCanonicalInCoinIDs >> decodedTx, std::ios_base::failure);

    CDataStream nonCanonicalContext(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalContext.write("\xfd\x00\x00", 3);
    helsing::StakeContext decodedContext;
    BOOST_CHECK_THROW(nonCanonicalContext >> decodedContext, std::ios_base::failure);

    CDataStream nonCanonicalProof(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalProof.write("\xfd\xfc\x00", 3);
    helsing::ProofBlob decodedProof;
    BOOST_CHECK_THROW(nonCanonicalProof >> decodedProof, std::ios_base::failure);

    const std::vector<unsigned char> serializedUpdate = ParseHex(WireHex(helsing::StakeUpdateTx()));
    BOOST_REQUIRE(!serializedUpdate.empty());
    for (size_t prefixSize = 0; prefixSize < serializedUpdate.size(); ++prefixSize) {
        BOOST_TEST_CONTEXT("updatePrefixSize=" << prefixSize)
        {
            std::vector<unsigned char> prefix(serializedUpdate.begin(), serializedUpdate.begin() + prefixSize);
            CDataStream truncated(prefix, SER_NETWORK, PROTOCOL_VERSION);
            helsing::StakeUpdateTx decodedUpdate;
            BOOST_CHECK_THROW(truncated >> decodedUpdate, std::ios_base::failure);
        }
    }

    CDataStream nonCanonicalSignature(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalSignature.write("\xfd\x00\x00", 3);
    helsing::SignatureBlob decodedSignature;
    BOOST_CHECK_THROW(nonCanonicalSignature >> decodedSignature, std::ios_base::failure);

    const std::vector<unsigned char> serializedPayout = ParseHex(WireHex(helsing::PayoutTxSkeleton()));
    BOOST_REQUIRE(!serializedPayout.empty());
    for (size_t prefixSize = 0; prefixSize < serializedPayout.size(); ++prefixSize) {
        BOOST_TEST_CONTEXT("payoutPrefixSize=" << prefixSize)
        {
            std::vector<unsigned char> prefix(serializedPayout.begin(), serializedPayout.begin() + prefixSize);
            CDataStream truncated(prefix, SER_NETWORK, PROTOCOL_VERSION);
            helsing::PayoutTxSkeleton decodedPayout;
            BOOST_CHECK_THROW(truncated >> decodedPayout, std::ios_base::failure);
        }
    }

    CDataStream nonCanonicalPayoutAddress(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalPayoutAddress.write("\xfd\x00\x00", 3);
    helsing::PayoutAddressBlob decodedPayoutAddress;
    BOOST_CHECK_THROW(nonCanonicalPayoutAddress >> decodedPayoutAddress, std::ios_base::failure);

    CDataStream nonCanonicalPayoutCoin(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalPayoutCoin.write("\xfd\x00\x00", 3);
    helsing::PayoutCoinBlob decodedPayoutCoin;
    BOOST_CHECK_THROW(nonCanonicalPayoutCoin >> decodedPayoutCoin, std::ios_base::failure);

    const std::vector<unsigned char> serializedPayoutContext = ParseHex(WireHex(helsing::PayoutBlockContextSkeleton()));
    BOOST_REQUIRE(!serializedPayoutContext.empty());
    for (size_t prefixSize = 0; prefixSize < serializedPayoutContext.size(); ++prefixSize) {
        BOOST_TEST_CONTEXT("payoutContextPrefixSize=" << prefixSize)
        {
            std::vector<unsigned char> prefix(serializedPayoutContext.begin(), serializedPayoutContext.begin() + prefixSize);
            CDataStream truncated(prefix, SER_NETWORK, PROTOCOL_VERSION);
            helsing::PayoutBlockContextSkeleton decodedPayoutContext;
            BOOST_CHECK_THROW(truncated >> decodedPayoutContext, std::ios_base::failure);
        }
    }

    CDataStream nonCanonicalPayoutChainId(SER_NETWORK, PROTOCOL_VERSION);
    nonCanonicalPayoutChainId.write("\xfd\x00\x00", 3);
    helsing::PayoutBlockContextSkeleton decodedPayoutContext;
    BOOST_CHECK_THROW(nonCanonicalPayoutChainId >> decodedPayoutContext, std::ios_base::failure);
}

BOOST_AUTO_TEST_CASE(default_stake_tx_serialization_roundtrip)
{
    helsing::StakeTx tx;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::StakeTx decoded;
    stream >> decoded;

    CheckStakeTxEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(default_stake_update_tx_serialization_roundtrip)
{
    helsing::StakeUpdateTx tx;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::StakeUpdateTx decoded;
    stream >> decoded;

    CheckStakeUpdateTxEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(default_payout_tx_skeleton_serialization_roundtrip)
{
    helsing::PayoutTxSkeleton tx;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::PayoutTxSkeleton decoded;
    stream >> decoded;

    CheckPayoutTxSkeletonEqual(decoded, tx);
}

BOOST_AUTO_TEST_CASE(default_payout_block_context_skeleton_serialization_roundtrip)
{
    helsing::PayoutBlockContextSkeleton context;

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << context;

    helsing::PayoutBlockContextSkeleton decoded;
    stream >> decoded;

    CheckPayoutBlockContextSkeletonEqual(decoded, context);
}

BOOST_AUTO_TEST_CASE(stake_tx_serialization_preserves_malformed_incoinids)
{
    helsing::StakeTx tx = ValidStakeTx();
    tx.inCoinIDs = {Output(2, 0), Output(1, 0), Output(1, 0), helsing::OutputId()};

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << tx;

    helsing::StakeTx decoded;
    stream >> decoded;

    CheckStakeTxEqual(decoded, tx);
    BOOST_CHECK(!helsing::IsStrictlySortedAndDistinct(decoded.inCoinIDs));
}

BOOST_AUTO_TEST_CASE(proof_blob_empty_reflects_serialized_content)
{
    helsing::ProofBlob proof;
    BOOST_CHECK(proof.empty());

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << proof;

    helsing::ProofBlob decodedEmpty;
    stream >> decodedEmpty;

    BOOST_CHECK(decodedEmpty.empty());
    BOOST_CHECK(decodedEmpty.bytes.empty());

    proof.bytes = {0xaa, 0xbb};
    BOOST_CHECK(!proof.empty());

    CDataStream nonEmptyStream(SER_NETWORK, PROTOCOL_VERSION);
    nonEmptyStream << proof;

    helsing::ProofBlob decodedNonEmpty;
    nonEmptyStream >> decodedNonEmpty;

    BOOST_CHECK(!decodedNonEmpty.empty());
    BOOST_CHECK(decodedNonEmpty.bytes == proof.bytes);
}

BOOST_AUTO_TEST_CASE(signature_blob_empty_reflects_serialized_content)
{
    helsing::SignatureBlob sig;
    BOOST_CHECK(sig.empty());

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << sig;

    helsing::SignatureBlob decodedEmpty;
    stream >> decodedEmpty;

    BOOST_CHECK(decodedEmpty.empty());
    BOOST_CHECK(decodedEmpty.bytes.empty());

    sig.bytes = {0x30, 0x44};
    BOOST_CHECK(!sig.empty());

    CDataStream nonEmptyStream(SER_NETWORK, PROTOCOL_VERSION);
    nonEmptyStream << sig;

    helsing::SignatureBlob decodedNonEmpty;
    nonEmptyStream >> decodedNonEmpty;

    BOOST_CHECK(!decodedNonEmpty.empty());
    BOOST_CHECK(decodedNonEmpty.bytes == sig.bytes);
}

BOOST_AUTO_TEST_CASE(stake_context_serialization_roundtrip)
{
    helsing::StakeContext empty;
    helsing::StakeContext nonEmpty;
    nonEmpty.bytes = {0x01, 0x02, 0x03};

    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << empty << nonEmpty;

    helsing::StakeContext decodedEmpty;
    helsing::StakeContext decodedNonEmpty;
    stream >> decodedEmpty >> decodedNonEmpty;

    BOOST_CHECK(decodedEmpty.bytes.empty());
    BOOST_CHECK(decodedNonEmpty.bytes == nonEmpty.bytes);
}

BOOST_AUTO_TEST_CASE(validation_does_not_mutate_helsing_state)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);
    helsing::CHelsingState state;
    view.helsingState = &state;

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
    BOOST_CHECK_EQUAL(state.GetStakeRecordCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetActiveTagCount(), 0U);
    BOOST_CHECK_EQUAL(state.GetSpentTagCount(), 0U);
    BOOST_CHECK(!state.IsActiveTag(tx.T));
    BOOST_CHECK(!state.IsSpentTag(tx.T));
}

BOOST_AUTO_TEST_CASE(accepts_structural_stub)
{
    helsing::StakeTx tx = ValidStakeTx();
    helsing::ValidationStateView view = ValidView(tx);

    BOOST_CHECK(helsing::CheckStakeSkeleton(tx, view) == helsing::StakeValidationResult::OK);
}

BOOST_AUTO_TEST_SUITE_END()
