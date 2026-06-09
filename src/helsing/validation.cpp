// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/validation.h"

#include "primitives/transaction.h"
#include "spark/state.h"

#include <exception>
#include <limits>
#include <utility>

namespace helsing {

const char* StakeValidationResultToString(StakeValidationResult result)
{
    switch (result) {
    case StakeValidationResult::OK:
        return "OK";
    case StakeValidationResult::EMPTY_INCOINIDS:
        return "EMPTY_INCOINIDS";
    case StakeValidationResult::INVALID_COVER_SET_CARDINALITY:
        return "INVALID_COVER_SET_CARDINALITY";
    case StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT:
        return "INCOINIDS_NOT_SORTED_DISTINCT";
    case StakeValidationResult::INVALID_OUTPUT_ID:
        return "INVALID_OUTPUT_ID";
    case StakeValidationResult::INVALID_GROUP_ELEMENT:
        return "INVALID_GROUP_ELEMENT";
    case StakeValidationResult::MISSING_PROOF:
        return "MISSING_PROOF";
    case StakeValidationResult::TAG_ALREADY_SPENT:
        return "TAG_ALREADY_SPENT";
    case StakeValidationResult::TAG_SPENT_IN_BLOCK:
        return "TAG_SPENT_IN_BLOCK";
    case StakeValidationResult::DUPLICATE_SPENT_TAG_IN_BLOCK:
        return "DUPLICATE_SPENT_TAG_IN_BLOCK";
    case StakeValidationResult::TAG_ALREADY_ACTIVE:
        return "TAG_ALREADY_ACTIVE";
    case StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK:
        return "DUPLICATE_STAKE_TAG_IN_BLOCK";
    case StakeValidationResult::STAKE_RECORD_NOT_FOUND:
        return "STAKE_RECORD_NOT_FOUND";
    case StakeValidationResult::STAKE_NOT_ACTIVE:
        return "STAKE_NOT_ACTIVE";
    case StakeValidationResult::STAKE_NOT_MATURE:
        return "STAKE_NOT_MATURE";
    case StakeValidationResult::OUTPUT_NOT_FOUND:
        return "OUTPUT_NOT_FOUND";
    case StakeValidationResult::OUTPUT_ID_MISMATCH:
        return "OUTPUT_ID_MISMATCH";
    case StakeValidationResult::INVALID_OUTPUT_RECORD:
        return "INVALID_OUTPUT_RECORD";
    case StakeValidationResult::OUTPUT_NOT_ELIGIBLE:
        return "OUTPUT_NOT_ELIGIBLE";
    }

    return "UNKNOWN";
}

const SparkOutputRecord* ValidationStateView::FindSparkOutput(const OutputId& output_id) const
{
    auto it = sparkOutputs.find(output_id);
    if (it == sparkOutputs.end()) {
        return nullptr;
    }

    return &it->second;
}

bool ValidationStateView::HasBlockSpentTag(const GroupElement& tag) const
{
    return blockSpentTags.count(tag) != 0;
}

bool IsStrictlySortedAndDistinct(const std::vector<OutputId>& output_ids)
{
    if (output_ids.empty()) {
        return false;
    }

    for (size_t i = 1; i < output_ids.size(); ++i) {
        if (!(output_ids[i - 1] < output_ids[i])) {
            return false;
        }
    }

    return true;
}

bool IsValidCoverSetCardinality(size_t count, size_t n, size_t m)
{
    if (count == 0 || n <= 1 || m <= 1) {
        return false;
    }

    size_t expected = 1;
    for (size_t i = 0; i < m; ++i) {
        if (expected > std::numeric_limits<size_t>::max() / n) {
            return false;
        }
        expected *= n;
    }

    return count == expected;
}

bool IsStakeMatureForPayout(int activationHeight, int currentHeight, int stakeMaturity)
{
    if (activationHeight < 0 || currentHeight < 0 || stakeMaturity < 0) {
        return false;
    }
    if (activationHeight > std::numeric_limits<int>::max() - stakeMaturity) {
        return false;
    }

    return activationHeight + stakeMaturity <= currentHeight;
}

bool IsHelsingValueInRange(CAmount value, CAmount vMax)
{
    return vMax > 0 && value >= 0 && value < vMax;
}

bool IsExpectedPayoutAmountInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax)
{
    return payoutValue == expectedAmount && IsHelsingValueInRange(payoutValue, vMax);
}

bool IsValidOutputId(const OutputId& output_id)
{
    return !output_id.txid.IsNull();
}

bool IsValidPublicPoint(const GroupElement& point)
{
    return point.isMember() && !point.isInfinity();
}

bool IsValidSparkOutputRecord(const SparkOutputRecord& output)
{
    return IsValidOutputId(output.output_id) &&
           IsValidPublicPoint(output.S) &&
           IsValidPublicPoint(output.C) &&
           IsValidPublicPoint(output.K) &&
           output.nHeight >= 0 &&
           (output.type == SparkOutputType::MINT || output.type == SparkOutputType::SPEND);
}

StakeValidationResult BuildBlockSpentTagsSkeleton(const std::vector<GroupElement>& spentTags, const CHelsingState* helsingState, std::unordered_set<GroupElement, spark::CLTagHash>& blockSpentTags)
{
    std::unordered_set<GroupElement, spark::CLTagHash> collected;
    for (const GroupElement& tag : spentTags) {
        if (!collected.insert(tag).second) {
            return StakeValidationResult::DUPLICATE_SPENT_TAG_IN_BLOCK;
        }
    }

    if (helsingState != nullptr) {
        for (const GroupElement& tag : collected) {
            if (helsingState->IsSpentTag(tag)) {
                return StakeValidationResult::TAG_ALREADY_SPENT;
            }
        }
    }

    blockSpentTags = std::move(collected);
    return StakeValidationResult::OK;
}

bool ExtractSparkOutputRecords(const CTransaction& tx, int nHeight, std::map<OutputId, SparkOutputRecord>& outputs)
{
    if (nHeight < 0) {
        return false;
    }
    if (!tx.IsSparkTransaction()) {
        return true;
    }

    std::map<OutputId, SparkOutputRecord> extracted;
    const uint256 txid = tx.GetHash();
    for (uint32_t n = 0; n < tx.vout.size(); ++n) {
        const CScript& script = tx.vout[n].scriptPubKey;
        SparkOutputType type = SparkOutputType::UNKNOWN;
        if (script.IsSparkMint()) {
            type = SparkOutputType::MINT;
        } else if (script.IsSparkSMint()) {
            type = SparkOutputType::SPEND;
        } else {
            continue;
        }

        spark::Coin coin(spark::Params::get_default());
        try {
            spark::ParseSparkMintCoin(script, coin);
        } catch (const std::exception&) {
            return false;
        }

        SparkOutputRecord record;
        record.output_id = OutputId(txid, n);
        record.S = coin.S;
        record.C = coin.C;
        record.K = coin.K;
        record.nHeight = nHeight;
        record.type = type;
        record.helsing_eligible = false;
        if (!extracted.emplace(record.output_id, record).second) {
            return false;
        }
    }

    for (const auto& output : extracted) {
        if (outputs.count(output.first) != 0) {
            return false;
        }
    }

    for (auto& output : extracted) {
        outputs.emplace(output.first, std::move(output.second));
    }
    return true;
}

StakeValidationResult CheckStakeSkeleton(const StakeTx& tx, const ValidationStateView& view)
{
    if (tx.inCoinIDs.empty()) {
        return StakeValidationResult::EMPTY_INCOINIDS;
    }
    if (!IsStrictlySortedAndDistinct(tx.inCoinIDs)) {
        return StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT;
    }
    for (const auto& output_id : tx.inCoinIDs) {
        if (!IsValidOutputId(output_id)) {
            return StakeValidationResult::INVALID_OUTPUT_ID;
        }
    }
    if (!IsValidPublicPoint(tx.S_prime) || !IsValidPublicPoint(tx.C_prime) || !IsValidPublicPoint(tx.T)) {
        return StakeValidationResult::INVALID_GROUP_ELEMENT;
    }
    if (tx.pi_par.empty() || tx.pi_val.empty() || tx.pi_tag.empty()) {
        return StakeValidationResult::MISSING_PROOF;
    }

    if (view.helsingState != nullptr) {
        if (view.helsingState->IsSpentTag(tx.T)) {
            return StakeValidationResult::TAG_ALREADY_SPENT;
        }
        if (view.helsingState->IsActiveTag(tx.T)) {
            return StakeValidationResult::TAG_ALREADY_ACTIVE;
        }
    }
    if (view.HasBlockSpentTag(tx.T)) {
        return StakeValidationResult::TAG_SPENT_IN_BLOCK;
    }

    for (const auto& output_id : tx.inCoinIDs) {
        const SparkOutputRecord* output = view.FindSparkOutput(output_id);
        if (output == nullptr) {
            return StakeValidationResult::OUTPUT_NOT_FOUND;
        }
        if (output->output_id != output_id) {
            return StakeValidationResult::OUTPUT_ID_MISMATCH;
        }
        if (!IsValidSparkOutputRecord(*output)) {
            return StakeValidationResult::INVALID_OUTPUT_RECORD;
        }
        if (!output->helsing_eligible) {
            return StakeValidationResult::OUTPUT_NOT_ELIGIBLE;
        }
    }

    return StakeValidationResult::OK;
}

StakeValidationResult CheckCoverSetOutputsSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view, size_t n, size_t m)
{
    if (!IsValidCoverSetCardinality(inCoinIDs.size(), n, m)) {
        return StakeValidationResult::INVALID_COVER_SET_CARDINALITY;
    }
    if (!IsStrictlySortedAndDistinct(inCoinIDs)) {
        return StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT;
    }
    for (const auto& output_id : inCoinIDs) {
        if (!IsValidOutputId(output_id)) {
            return StakeValidationResult::INVALID_OUTPUT_ID;
        }
    }

    for (const auto& output_id : inCoinIDs) {
        const SparkOutputRecord* output = view.FindSparkOutput(output_id);
        if (output == nullptr) {
            return StakeValidationResult::OUTPUT_NOT_FOUND;
        }
        if (output->output_id != output_id) {
            return StakeValidationResult::OUTPUT_ID_MISMATCH;
        }
        if (!IsValidSparkOutputRecord(*output)) {
            return StakeValidationResult::INVALID_OUTPUT_RECORD;
        }
        if (!output->helsing_eligible) {
            return StakeValidationResult::OUTPUT_NOT_ELIGIBLE;
        }
    }

    return StakeValidationResult::OK;
}

StakeValidationResult CheckStakeBlockSkeleton(const std::vector<StakeTx>& stake_txs, const ValidationStateView& view)
{
    std::unordered_set<GroupElement, spark::CLTagHash> newStakeTags;

    for (const StakeTx& tx : stake_txs) {
        if (!IsValidPublicPoint(tx.T)) {
            continue;
        }
        if (view.helsingState != nullptr) {
            if (view.helsingState->IsSpentTag(tx.T)) {
                return StakeValidationResult::TAG_ALREADY_SPENT;
            }
        }
        if (view.HasBlockSpentTag(tx.T)) {
            return StakeValidationResult::TAG_SPENT_IN_BLOCK;
        }
        if (view.helsingState != nullptr && view.helsingState->IsActiveTag(tx.T)) {
            return StakeValidationResult::TAG_ALREADY_ACTIVE;
        }
        if (!newStakeTags.insert(tx.T).second) {
            return StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK;
        }
    }

    for (const StakeTx& tx : stake_txs) {
        const StakeValidationResult result = CheckStakeSkeleton(tx, view);
        if (result != StakeValidationResult::OK) {
            return result;
        }
    }

    return StakeValidationResult::OK;
}

StakeValidationResult CheckPayoutEligibilitySkeleton(const uint256& stake_id, const ValidationStateView& view, int currentHeight, int stakeMaturity)
{
    if (view.helsingState == nullptr) {
        return StakeValidationResult::STAKE_RECORD_NOT_FOUND;
    }

    const StakeRecord* record = view.helsingState->GetStakeRecord(stake_id);
    if (record == nullptr) {
        return StakeValidationResult::STAKE_RECORD_NOT_FOUND;
    }
    if (record->status != StakeStatus::ACTIVE) {
        return StakeValidationResult::STAKE_NOT_ACTIVE;
    }
    if (view.helsingState->IsSpentTag(record->T)) {
        return StakeValidationResult::TAG_ALREADY_SPENT;
    }
    if (view.HasBlockSpentTag(record->T)) {
        return StakeValidationResult::TAG_SPENT_IN_BLOCK;
    }
    if (!IsStakeMatureForPayout(record->nHeight, currentHeight, stakeMaturity)) {
        return StakeValidationResult::STAKE_NOT_MATURE;
    }

    return StakeValidationResult::OK;
}

bool ArePayoutIndexesDistinctSkeleton(const std::vector<PayoutTxSkeleton>& payout_txs)
{
    std::unordered_set<uint32_t> payoutIndexes;
    for (const PayoutTxSkeleton& tx : payout_txs) {
        if (!payoutIndexes.insert(tx.payout_index).second) {
            return false;
        }
    }

    return true;
}

StakeValidationResult CheckStakeUpdateEligibilitySkeleton(const uint256& stake_id, const ValidationStateView& view)
{
    if (view.helsingState == nullptr) {
        return StakeValidationResult::STAKE_RECORD_NOT_FOUND;
    }

    const StakeRecord* record = view.helsingState->GetStakeRecord(stake_id);
    if (record == nullptr) {
        return StakeValidationResult::STAKE_RECORD_NOT_FOUND;
    }
    if (record->status != StakeStatus::ACTIVE) {
        return StakeValidationResult::STAKE_NOT_ACTIVE;
    }
    if (view.helsingState->IsSpentTag(record->T)) {
        return StakeValidationResult::TAG_ALREADY_SPENT;
    }
    if (view.HasBlockSpentTag(record->T)) {
        return StakeValidationResult::TAG_SPENT_IN_BLOCK;
    }

    return StakeValidationResult::OK;
}

} // namespace helsing
