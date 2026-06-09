// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FIRO_HELSING_VALIDATION_H
#define FIRO_HELSING_VALIDATION_H

#include "helsing/state.h"

#include <cstddef>
#include <map>
#include <unordered_set>

class CTransaction;

namespace helsing {

enum class StakeValidationResult {
    OK,
    EMPTY_INCOINIDS,
    INVALID_COVER_SET_CARDINALITY,
    INCOINIDS_NOT_SORTED_DISTINCT,
    INVALID_OUTPUT_ID,
    INVALID_GROUP_ELEMENT,
    MISSING_PROOF,
    TAG_ALREADY_SPENT,
    TAG_SPENT_IN_BLOCK,
    DUPLICATE_SPENT_TAG_IN_BLOCK,
    TAG_ALREADY_ACTIVE,
    DUPLICATE_STAKE_TAG_IN_BLOCK,
    STAKE_RECORD_NOT_FOUND,
    STAKE_NOT_ACTIVE,
    STAKE_NOT_MATURE,
    OUTPUT_NOT_FOUND,
    OUTPUT_ID_MISMATCH,
    INVALID_OUTPUT_RECORD,
    OUTPUT_NOT_ELIGIBLE,
};

const char* StakeValidationResultToString(StakeValidationResult result);

struct ValidationStateView {
    std::map<OutputId, SparkOutputRecord> sparkOutputs;
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    const CHelsingState* helsingState{nullptr};

    const SparkOutputRecord* FindSparkOutput(const OutputId& output_id) const;
    bool HasBlockSpentTag(const GroupElement& tag) const;
};

bool IsStrictlySortedAndDistinct(const std::vector<OutputId>& output_ids);
// Revised spec cover-set cardinality: len(InCoinIDs) = N = n^m, with n,m > 1.
bool IsValidCoverSetCardinality(size_t count, size_t n, size_t m);
// Revised spec payout eligibility inequality:
// activation_height + STAKE_MATURITY <= current_height.
bool IsStakeMatureForPayout(int activationHeight, int currentHeight, int stakeMaturity);
// Revised spec integer-domain value bound. The caller must separately enforce V_MAX < q.
bool IsHelsingValueInRange(CAmount value, CAmount vMax);
// Revised spec PayoutVerify step 10 subset: V_PAYOUT equals the caller-recomputed expected amount
// and satisfies the integer-domain range. The caller must separately enforce V_MAX < q.
bool IsExpectedPayoutAmountInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax);
// Revised spec PayoutTx field-completeness only. This does not check canonical
// encodings, value-domain parameters, registered address, selected stake, j, or payout coin.
bool IsCompletePayoutTxSkeleton(const PayoutTxSkeleton& tx);
// Revised spec PayoutVerify step 8 subset: compare the transaction address with the
// caller-supplied, already extracted registered address. This does not parse context m.
bool DoesPayoutAddressMatchRegisteredSkeleton(const PayoutTxSkeleton& tx, const PayoutAddressBlob& registeredAddress);
// Revised spec PayoutVerify step 13 subset: compare the transaction coin with the
// caller-supplied, already recomputed payout coin. This does not construct j or run Payout.
bool DoesPayoutCoinMatchExpectedSkeleton(const PayoutTxSkeleton& tx, const PayoutCoinBlob& expectedCoin);
// Revised spec section 16 payout-id input completeness only. This does not define
// canonical chain_id/enc_* grammar, chain binding, selected-stake rules, or construct j.
bool IsCompletePayoutBlockContextSkeleton(const PayoutBlockContextSkeleton& context);
// Revised spec section 16/18 tx-context consistency for fields that feed payout-id
// construction. This does not define block_context grammar or construct j.
bool DoesPayoutContextMatchTxSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context);
// Revised spec section 16 payout-id input availability across tx and block context.
// This does not define canonical encodings, selected-masternode rules, or construct j.
bool ArePayoutIdInputsCompleteSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context);
// Revised spec StakeUpdateTx field-completeness only. This does not parse contexts,
// extract update_pk, define enc_context(m_new), or verify sig_update.
bool IsCompleteStakeUpdateTxSkeleton(const StakeUpdateTx& tx);
// Revised spec optional collateral-margin subset: validate V_STAKE + margin as integers
// before scalar conversion. The caller must separately enforce V_MAX < q.
bool IsHelsingStakeValueWithMarginInRangeSkeleton(CAmount stakeValue, CAmount margin, CAmount vMax);
bool IsValidOutputId(const OutputId& output_id);
bool IsValidPublicPoint(const GroupElement& point);
bool IsValidSparkOutputRecord(const SparkOutputRecord& output);

// Models revised spec ValidateBlock step 2 after ordinary Spark spend validation.
// The caller supplies tags revealed by valid Spark spends; this only checks
// same-block duplicates and tags already present in Helsing SpentTags.
StakeValidationResult BuildBlockSpentTagsSkeleton(const std::vector<GroupElement>& spentTags, const CHelsingState* helsingState, std::unordered_set<GroupElement, spark::CLTagHash>& blockSpentTags);

// Appends Spark mint/spend-created output records keyed by (txid, vout).
// Leaves outputs unchanged on parse failure or duplicate output_id.
bool ExtractSparkOutputRecords(const CTransaction& tx, int nHeight, std::map<OutputId, SparkOutputRecord>& outputs);

// Applies revised spec ValidateBlock step 9 to already accepted payout output records.
// The caller supplies records after payout-coin verification; this only inserts by output_id.
bool ApplyAcceptedPayoutOutputRecordsSkeleton(const std::vector<SparkOutputRecord>& acceptedOutputs, std::map<OutputId, SparkOutputRecord>& outputs);

// Applies revised spec ValidateBlock steps 6-9 to already accepted block data.
// This is in-memory only and does not perform StakeVerify, StakeUpdateVerify, or PayoutVerify.
bool ApplyAcceptedBlockWithPayoutOutputsSkeleton(CHelsingState& helsingState, std::map<OutputId, SparkOutputRecord>& outputs, const std::unordered_set<GroupElement, spark::CLTagHash>& blockSpentTags, const std::vector<std::pair<uint256, StakeTx>>& acceptedStakes, const std::vector<std::pair<uint256, StakeContext>>& acceptedUpdates, const std::vector<SparkOutputRecord>& acceptedPayoutOutputs, int nHeight);

// Structural skeleton for revised-spec StakeVerify. Proof verification is intentionally TODO.
StakeValidationResult CheckStakeSkeleton(const StakeTx& tx, const ValidationStateView& view);

// Revised-spec cover-set output checks with caller-supplied public parameters.
// This stops before Spark maturity/cover-set rules and proof verification.
StakeValidationResult CheckCoverSetOutputsSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view, size_t n, size_t m);

// Non-mutating block-level skeleton for same-block Helsing stake checks.
StakeValidationResult CheckStakeBlockSkeleton(const std::vector<StakeTx>& stake_txs, const ValidationStateView& view);

// Structural skeleton for revised-spec PayoutVerify steps 3-7 only.
StakeValidationResult CheckPayoutEligibilitySkeleton(const uint256& stake_id, const ValidationStateView& view, int currentHeight, int stakeMaturity);

// Block-level skeleton for revised-spec ValidateBlock step 5 and PayoutVerify steps 3-7 only.
// This deliberately stops before payout index policy, address extraction, deterministic selection,
// payout amount derivation, payout identifier construction, and payout coin recomputation.
StakeValidationResult CheckPayoutBlockEligibilitySkeleton(const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& view, int currentHeight, int stakeMaturity);

// Revised-spec section 16 requires distinct payout_index values when a block
// contains more than one masternode payout.
bool ArePayoutIndexesDistinctSkeleton(const std::vector<PayoutTxSkeleton>& payout_txs);

// Structural skeleton for revised-spec StakeUpdateVerify steps 1-3 only.
StakeValidationResult CheckStakeUpdateEligibilitySkeleton(const uint256& stake_id, const ValidationStateView& view);

// Block-level skeleton for revised-spec ValidateBlock step 4 and StakeUpdateVerify steps 1-3 only.
// This deliberately stops before context parsing, signature verification, effective-height rules,
// and same-block duplicate-update policy.
StakeValidationResult CheckStakeUpdateBlockSkeleton(const std::vector<StakeUpdateTx>& update_txs, const ValidationStateView& view);

// Block-level skeleton for revised-spec ValidateBlock steps 3-5 using the existing inert
// stake, update, and payout prefix helpers. The caller must supply already collected
// BlockSpentTags in view and this does not perform Spark spend validation or state mutation.
StakeValidationResult CheckBlockValidationPrefixSkeleton(const std::vector<StakeTx>& stake_txs, const std::vector<StakeUpdateTx>& update_txs, const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& view, int currentHeight, int stakeMaturity);

} // namespace helsing

#endif // FIRO_HELSING_VALIDATION_H
