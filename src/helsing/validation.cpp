// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/validation.h"

#include "primitives/transaction.h"
#include "spark/state.h"

#include <exception>
#include <utility>

namespace helsing {

const char* StakeValidationResultToString(StakeValidationResult result)
{
    switch (result) {
    case StakeValidationResult::OK:
        return "OK";
    case StakeValidationResult::EMPTY_INCOINIDS:
        return "EMPTY_INCOINIDS";
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
    case StakeValidationResult::TAG_ALREADY_ACTIVE:
        return "TAG_ALREADY_ACTIVE";
    case StakeValidationResult::DUPLICATE_STAKE_TAG_IN_BLOCK:
        return "DUPLICATE_STAKE_TAG_IN_BLOCK";
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

} // namespace helsing
