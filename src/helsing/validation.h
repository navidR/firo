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
    OUTPUT_SPARK_RULES_FAILED,
};

const char* StakeValidationResultToString(StakeValidationResult result);

enum class PayoutPublicValidationResult {
    OK,
    ADDRESS_MISMATCH,
    SELECTED_STAKE_MISMATCH,
    INVALID_PAYOUT_AMOUNT,
    INVALID_PAYOUT_ID_INPUTS,
    PAYOUT_COIN_MISMATCH,
};

enum class PayoutAmountDerivationSkeletonResult {
    PAYOUT_SELECTION_PREFIX_FAILED,
    CONSENSUS_REWARD_RULES_UNIMPLEMENTED,
    EXPECTED_PAYOUT_AMOUNT_DERIVATION_UNIMPLEMENTED,
    PAYOUT_AMOUNT_COMPARISON_UNIMPLEMENTED,
    PAYOUT_VALUE_DOMAIN_CONSENSUS_PARAMETERS_UNIMPLEMENTED,
};

enum class PayoutIdConstructionSkeletonResult {
    PAYOUT_ID_INPUTS_INCOMPLETE,
    CANONICAL_PAYOUT_ID_ENCODING_UNIMPLEMENTED,
    PAYOUT_ID_HASHING_UNIMPLEMENTED,
};

enum class PayoutAlgorithmSkeletonResult {
    PAYOUT_TX_INCOMPLETE,
    PAYOUT_IDENTIFIER_UNAVAILABLE,
    PAYOUT_ADDRESS_PARSING_UNIMPLEMENTED,
    PAYOUT_KEY_DERIVATION_UNIMPLEMENTED,
    PAYOUT_COIN_CONSTRUCTION_UNIMPLEMENTED,
    PAYOUT_COIN_COMPARISON_UNIMPLEMENTED,
};

enum class PayoutOutputRecordConstructionSkeletonResult {
    PAYOUT_VERIFY_NOT_ACCEPTED,
    OUTPUT_ID_UNAVAILABLE,
    PAYOUT_COIN_PARSING_UNIMPLEMENTED,
    PAYOUT_OUTPUT_RECORD_CONSTRUCTION_UNIMPLEMENTED,
};

enum class HelsingTransactionWiringSkeletonResult {
    CONSENSUS_TX_CARRIER_UNIMPLEMENTED,
    STAKE_TX_EXTRACTION_UNIMPLEMENTED,
    STAKE_UPDATE_TX_EXTRACTION_UNIMPLEMENTED,
    PAYOUT_TX_EXTRACTION_UNIMPLEMENTED,
    BLOCK_INTEGRATION_UNIMPLEMENTED,
};

enum class HelsingConsensusActivationSkeletonResult {
    SPARK_COMPATIBILITY_REVIEW_UNIMPLEMENTED,
    ACTIVATION_PARAMETERS_UNIMPLEMENTED,
    CHAINPARAMS_DEPLOYMENT_WIRING_UNIMPLEMENTED,
    HISTORICAL_STATE_BOOTSTRAP_UNIMPLEMENTED,
    ACTIVATION_ENFORCEMENT_UNIMPLEMENTED,
};

enum class HelsingMasternodeLifecycleSkeletonResult {
    ACCEPTED_STAKE_UNAVAILABLE,
    MASTERNODE_CONTEXT_EXTRACTION_UNIMPLEMENTED,
    MASTERNODE_REGISTRATION_INTEGRATION_UNIMPLEMENTED,
    STAKE_UPDATE_LIFECYCLE_INTEGRATION_UNIMPLEMENTED,
    PAYOUT_SELECTION_INTEGRATION_UNIMPLEMENTED,
    MASTERNODE_PAYMENT_INTEGRATION_UNIMPLEMENTED,
};

enum class PayoutVerificationPrefixSkeletonResult {
    OK,
    TX_INCOMPLETE,
    MALFORMED_OR_NONCANONICAL,
};

struct PayoutVerificationSkeletonResult {
    StakeValidationResult stake_result{StakeValidationResult::OK};
    PayoutPublicValidationResult public_result{PayoutPublicValidationResult::OK};
};

struct PayoutVerificationEligibilitySkeletonResult {
    PayoutVerificationPrefixSkeletonResult prefix_result{PayoutVerificationPrefixSkeletonResult::OK};
    StakeValidationResult stake_result{StakeValidationResult::OK};
};

enum class PayoutVerificationSuffixSkeletonResult {
    PAYOUT_ELIGIBILITY_FAILED,
    REGISTERED_PAYOUT_ADDRESS_EXTRACTION_UNIMPLEMENTED,
    DETERMINISTIC_SELECTION_UNIMPLEMENTED,
    EXPECTED_PAYOUT_AMOUNT_UNIMPLEMENTED,
    PAYOUT_ID_CONSTRUCTION_UNIMPLEMENTED,
    PAYOUT_ALGORITHM_UNIMPLEMENTED,
    PAYOUT_COIN_COMPARISON_UNIMPLEMENTED,
};

struct PayoutVerificationBlockedSkeletonResult {
    PayoutVerificationEligibilitySkeletonResult eligibility_result;
    PayoutVerificationSuffixSkeletonResult suffix_result{PayoutVerificationSuffixSkeletonResult::PAYOUT_ELIGIBILITY_FAILED};
};

enum class StakeVerificationPrefixSkeletonResult {
    OK,
    TX_INCOMPLETE,
    MALFORMED_OR_NONCANONICAL,
    INVALID_VALUE_PARAMETER,
};

enum class StakeContextValidationBlockedSkeletonResult {
    STAKE_CONTEXT_EMPTY,
    CANONICAL_CONTEXT_ENCODING_UNIMPLEMENTED,
    PAYOUT_ADDRESS_VALIDATION_UNIMPLEMENTED,
    UPDATE_PUBLIC_KEY_VALIDATION_UNIMPLEMENTED,
    NODE_SIGNING_KEY_VALIDATION_UNIMPLEMENTED,
    MASTERNODE_CONTEXT_RULES_UNIMPLEMENTED,
};

struct StakeUpdateVerificationSkeletonResult {
    bool tx_complete{false};
    StakeValidationResult stake_result{StakeValidationResult::OK};
};

enum class StakeUpdateAuthorizationSkeletonResult {
    STAKE_UPDATE_PREFIX_FAILED,
    UPDATE_PUBLIC_KEY_EXTRACTION_UNIMPLEMENTED,
    CANONICAL_UPDATE_CONTEXT_UNIMPLEMENTED,
    UPDATE_SIGNATURE_HASHING_UNIMPLEMENTED,
    UPDATE_SIGNATURE_VERIFICATION_UNIMPLEMENTED,
};

struct StakeUpdateVerificationBlockedSkeletonResult {
    StakeUpdateVerificationSkeletonResult prefix_result;
    StakeUpdateAuthorizationSkeletonResult authorization_result{StakeUpdateAuthorizationSkeletonResult::STAKE_UPDATE_PREFIX_FAILED};
};

enum class StakeUpdateEffectiveHeightSkeletonResult {
    STAKE_UPDATE_VERIFY_NOT_ACCEPTED,
    EFFECTIVE_HEIGHT_RULE_UNIMPLEMENTED,
    EFFECTIVE_HEIGHT_STATE_TRANSITION_UNIMPLEMENTED,
};

enum class StakeUpdateSameBlockPolicySkeletonResult {
    ACCEPTED_STAKE_UPDATES_UNAVAILABLE,
    DUPLICATE_STAKE_UPDATE_POLICY_UNIMPLEMENTED,
    STAKE_UPDATE_APPLICATION_ORDER_UNIMPLEMENTED,
};

struct StakeVerificationContextSkeletonResult {
    StakeVerificationPrefixSkeletonResult prefix_result{StakeVerificationPrefixSkeletonResult::OK};
    StakeValidationResult tag_result{StakeValidationResult::OK};
    bool context_valid{true};
};

struct StakeVerificationCoverSetSkeletonResult {
    StakeVerificationContextSkeletonResult context_result;
    StakeValidationResult cover_set_result{StakeValidationResult::OK};
};

struct StakeVerificationOutputSkeletonResult {
    StakeVerificationCoverSetSkeletonResult cover_set_result;
    StakeValidationResult output_result{StakeValidationResult::OK};
};

enum class StakeCoverSetSparkRulesSkeletonResult {
    STAKE_OUTPUT_PREFIX_FAILED,
    SPARK_MATURITY_OR_COVER_SET_RULES_UNIMPLEMENTED,
};

struct StakeCoverSetSparkRulesBlockedSkeletonResult {
    StakeValidationResult output_result{StakeValidationResult::OK};
    StakeCoverSetSparkRulesSkeletonResult spark_rules_result{StakeCoverSetSparkRulesSkeletonResult::STAKE_OUTPUT_PREFIX_FAILED};
};

enum class StakeProofVerificationSkeletonResult {
    STAKE_PREFIX_FAILED,
    STAKE_STATEMENT_UNAVAILABLE,
    PAR_VERIFY_UNIMPLEMENTED,
    REP_VERIFY_UNIMPLEMENTED,
    TAG_VERIFY_UNIMPLEMENTED,
};

enum class StakeStatementConstructionSkeletonResult {
    STAKE_PREFIX_FAILED,
    INCOINS_ROOT_HASHING_UNIMPLEMENTED,
    CONTEXT_HASHING_UNIMPLEMENTED,
    STAKE_STATEMENT_HASHING_UNIMPLEMENTED,
};

struct StakeVerificationBlockedSkeletonResult {
    StakeVerificationOutputSkeletonResult output_result;
    StakeCoverSetSparkRulesBlockedSkeletonResult spark_rules_result;
    StakeStatementConstructionSkeletonResult statement_result{StakeStatementConstructionSkeletonResult::STAKE_PREFIX_FAILED};
    StakeProofVerificationSkeletonResult proof_result{StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED};
};

struct BlockValidationPrefixWithSpentTagsSkeletonResult {
    StakeValidationResult block_spent_result{StakeValidationResult::OK};
    StakeValidationResult validation_result{StakeValidationResult::OK};
};

enum class BlockValidationSparkPrepassSkeletonResult {
    ORDINARY_SPARK_SPEND_VALIDATION_UNIMPLEMENTED,
    SPARK_SPEND_TAG_EXTRACTION_UNIMPLEMENTED,
};

enum class BlockValidationSuffixSkeletonResult {
    BLOCK_VALIDATION_PREFIX_FAILED,
    FULL_TRANSACTION_VERIFICATION_UNIMPLEMENTED,
};

struct BlockValidationBlockedSkeletonResult {
    BlockValidationSparkPrepassSkeletonResult spark_prepass_result{BlockValidationSparkPrepassSkeletonResult::ORDINARY_SPARK_SPEND_VALIDATION_UNIMPLEMENTED};
    BlockValidationPrefixWithSpentTagsSkeletonResult prefix_result;
    BlockValidationSuffixSkeletonResult suffix_result{BlockValidationSuffixSkeletonResult::BLOCK_VALIDATION_PREFIX_FAILED};
};

enum class PersistentBlockApplicationSkeletonResult {
    ACCEPTED_BLOCK_DATA_UNAVAILABLE,
    HELSING_STATE_STORAGE_UNIMPLEMENTED,
    SPARK_OUTPUT_STORAGE_UNIMPLEMENTED,
    UNDO_DATA_SERIALIZATION_UNIMPLEMENTED,
    DISCONNECT_REPLAY_UNIMPLEMENTED,
};

enum class StakeIdConstructionSkeletonResult {
    STAKE_VERIFY_NOT_ACCEPTED,
    CANONICAL_TX_ENCODING_UNIMPLEMENTED,
    STAKE_ID_HASHING_UNIMPLEMENTED,
};

enum class HelsingEligibleOutputMarkingSkeletonResult {
    OUTPUT_RECORD_INVALID,
    SPARK_SPEND_PATH_ANALYSIS_UNIMPLEMENTED,
    ELIGIBILITY_PERSISTENCE_UNIMPLEMENTED,
    ELIGIBILITY_UNDO_UNIMPLEMENTED,
    CONSENSUS_WIRING_UNIMPLEMENTED,
};

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
// Revised spec section 20 value-parameter bounds for caller-supplied public values.
// This does not expose q or perform scalar conversion; the caller supplies V_MAX < q.
bool AreHelsingValueParametersInRangeSkeleton(CAmount stakeValue, CAmount payoutValue, CAmount vMax, bool vMaxLessThanGroupOrder);
// Revised spec StakeVerify step 3 value-domain subset for caller-supplied public values.
// This does not expose q or perform scalar conversion; the caller supplies V_MAX < q.
bool IsStakeValueParameterInRangeSkeleton(CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder);
// Revised spec PayoutVerify step 10 subset: V_PAYOUT equals the caller-recomputed expected amount
// and satisfies the integer-domain range. The caller must separately enforce V_MAX < q.
bool IsExpectedPayoutAmountInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax);
// Revised spec PayoutVerify step 10 value-domain subset for caller-supplied public values.
// This does not expose q or perform scalar conversion; the caller supplies V_MAX < q.
bool IsPayoutValueParameterInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax, bool vMaxLessThanGroupOrder);
// Revised spec PayoutVerify step 10 requires recomputing the expected payout
// amount after step-9 deterministic selection. The revised spec does not define
// Firo reward-rule inputs or payout-amount derivation, so this blocker has no
// accepting result and does not compute an amount.
PayoutAmountDerivationSkeletonResult CheckPayoutAmountDerivationSkeleton(bool payoutSelectionPrefixAccepted, bool consensusRewardRulesAvailable, bool expectedPayoutAmountDerivationAvailable, bool payoutAmountComparisonAvailable);
// Revised spec PayoutTx field-completeness only. This does not check canonical
// encodings, value-domain parameters, registered address, selected stake, j, or payout coin.
bool IsCompletePayoutTxSkeleton(const PayoutTxSkeleton& tx);
// Revised spec PayoutVerify steps 1-2 only. The caller supplies the result of
// canonical-encoding validation because the consensus grammar is not implemented here.
PayoutVerificationPrefixSkeletonResult CheckPayoutVerificationPrefixSkeleton(const PayoutTxSkeleton& tx, bool canonicalEncodingsValid);
// Revised spec PayoutVerify step 8 subset: compare the transaction address with the
// caller-supplied, already extracted registered address. This does not parse context m.
bool DoesPayoutAddressMatchRegisteredSkeleton(const PayoutTxSkeleton& tx, const PayoutAddressBlob& registeredAddress);
// Revised spec PayoutVerify step 9 subset: compare the transaction selected stake
// with the caller-supplied, already recomputed deterministic selection.
bool DoesPayoutStakeMatchExpectedSkeleton(const PayoutTxSkeleton& tx, const uint256& expectedStakeId);
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
// Revised spec section 16 payout-id construction is deliberately unimplemented:
// canonical chain_id/enc_* grammar and hash construction are required before deriving j.
PayoutIdConstructionSkeletonResult CheckPayoutIdConstructionSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context, bool canonicalPayoutIdEncodingAvailable);
// Revised spec section 17 payout algorithm and section 18 steps 11-13 are deliberately
// blocked here. The caller supplies implementation-availability flags, not consensus
// validation results; this helper has no accepting result and does not construct Coin.
PayoutAlgorithmSkeletonResult CheckPayoutAlgorithmSkeleton(const PayoutTxSkeleton& tx, bool payoutIdentifierAvailable, bool payoutAddressParserAvailable, bool payoutKeyDerivationAvailable, bool payoutCoinConstructionAvailable);
// Revised spec PayoutVerify public-field suffix, using only caller-supplied expected values.
// This checks steps 8, 9, 10, 11 input availability, and 13 equality in order; it
// does not parse contexts, select masternodes, compute j, run Payout, or verify proofs.
PayoutPublicValidationResult CheckPayoutPublicFieldsSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context, const PayoutAddressBlob& registeredAddress, const uint256& expectedStakeId, CAmount expectedAmount, CAmount vMax, const PayoutCoinBlob& expectedCoin);
// Revised spec StakeUpdateTx field-completeness only. This does not parse contexts,
// extract update_pk, define enc_context(m_new), or verify sig_update.
bool IsCompleteStakeUpdateTxSkeleton(const StakeUpdateTx& tx);
// Revised spec StakeTx field-completeness only. This does not check output-id grammar,
// group-element encodings, context grammar, value parameters, or proof validity.
bool IsCompleteStakeTxSkeleton(const StakeTx& tx);
// Revised spec sections 9, 14, 15, and 12 define Helsing transaction payloads
// and block ordering, but not the Firo consensus carrier/extraction grammar.
// This blocker has no accepting result and must not be used to parse CTransaction.
HelsingTransactionWiringSkeletonResult CheckHelsingTransactionWiringSkeleton(bool consensusTxCarrierAvailable, bool stakeTxExtractionAvailable, bool stakeUpdateTxExtractionAvailable, bool payoutTxExtractionAvailable);
// Revised spec section 30 gives deployment compatibility questions, but not a
// Firo activation mechanism, activation parameters, or historical-state
// bootstrap rule. This blocker has no accepting result and must not be used to
// decide whether Helsing consensus rules are active.
HelsingConsensusActivationSkeletonResult CheckHelsingConsensusActivationSkeleton(bool sparkCompatibilityReviewAvailable, bool activationParametersAvailable, bool chainparamsDeploymentWiringAvailable, bool historicalStateBootstrapAvailable);
// Revised spec sections 7, 9, 11, 14, and 18 require public masternode
// registration metadata, stake updates, and deterministic payout selection,
// but do not define how these map into Firo deterministic masternode lifecycle
// state. This blocker has no accepting result and does not touch evo state.
HelsingMasternodeLifecycleSkeletonResult CheckHelsingMasternodeLifecycleSkeleton(bool acceptedStakeAvailable, bool masternodeContextExtractionAvailable, bool masternodeRegistrationIntegrationAvailable, bool stakeUpdateLifecycleIntegrationAvailable, bool payoutSelectionIntegrationAvailable);
// Revised spec StakeVerify steps 1-3 only. The caller supplies the result of
// canonical-encoding validation because the consensus grammar is not implemented here.
StakeVerificationPrefixSkeletonResult CheckStakeVerificationPrefixSkeleton(const StakeTx& tx, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid);
// Revised spec StakeVerify step 4 only, against parent consensus state.
// Same-block BlockSpentTags remain a separate block-validation pre-pass concern.
StakeValidationResult CheckStakeTagStateSkeleton(const GroupElement& tag, const CHelsingState& helsingState);
// Revised spec StakeVerify step 5 predicate with caller-supplied context facts.
// This does not define enc_context(m), address grammar, update-key grammar, or node-key grammar.
bool IsStakeContextValidSkeleton(const StakeContext& context, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid);
// Revised spec section 7 and StakeVerify step 5 context validation are deliberately
// blocked here. The caller supplies implementation-availability flags, not validation
// results; this helper has no accepting result and does not parse context fields.
StakeContextValidationBlockedSkeletonResult CheckStakeContextValidationBlockedSkeleton(const StakeContext& context, bool canonicalContextEncodingAvailable, bool payoutAddressValidationAvailable, bool updatePublicKeyValidationAvailable, bool nodeSigningKeyValidationAvailable);
// Revised spec StakeVerify steps 1-5 only. The caller supplies canonical
// encoding and context-validation facts; this stops before cover sets and proofs.
StakeVerificationContextSkeletonResult CheckStakeVerificationContextSkeleton(const StakeTx& tx, const CHelsingState& helsingState, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid);
// Revised spec StakeVerify step 6 only: check public cover-set identifier count
// and sorted-distinct order. Output lookup, maturity, and Spark rules are step 7.
StakeValidationResult CheckStakeCoverSetIdentifiersSkeleton(const std::vector<OutputId>& inCoinIDs, size_t n, size_t m);
// Revised spec StakeVerify steps 1-6 only. This composes the caller-supplied
// prefix/context facts with step 6 identifiers and stops before output lookup.
StakeVerificationCoverSetSkeletonResult CheckStakeVerificationCoverSetSkeleton(const StakeTx& tx, const CHelsingState& helsingState, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid, size_t n, size_t m);
// Revised spec StakeVerify step 7 explicit predicates only: output lookup,
// helsing_eligible, and caller-supplied Spark maturity/cover-set rule status.
StakeValidationResult CheckStakeCoverSetOutputRulesSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view, const std::map<OutputId, bool>& outputSatisfiesSparkRules);
// Revised spec StakeVerify step 7 real Spark maturity/cover-set rules are
// deliberately blocked here. This preserves output lookup and helsing_eligible
// precedence, but it does not accept caller-supplied Spark-rule facts.
StakeCoverSetSparkRulesBlockedSkeletonResult CheckStakeCoverSetSparkRulesBlockedSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view);
// Revised spec StakeVerify steps 1-7 only. This composes caller-supplied
// context/Spark facts and stops before statement hashes and proof verification.
StakeVerificationOutputSkeletonResult CheckStakeVerificationOutputsSkeleton(const StakeTx& tx, const CHelsingState& helsingState, const ValidationStateView& view, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid, size_t n, size_t m, const std::map<OutputId, bool>& outputSatisfiesSparkRules);
// Revised spec section 5.2 and StakeVerify step 8 statement construction is deliberately
// blocked here. The caller supplies implementation-availability flags; this helper
// has no accepting result and does not compute incoins_root, context_hash, or stake_stmt.
StakeStatementConstructionSkeletonResult CheckStakeStatementConstructionSkeleton(const StakeVerificationOutputSkeletonResult& prefixResult, bool incoinsRootHashingAvailable, bool contextHashingAvailable);
// Revised spec StakeVerify steps 9-11 are deliberately unimplemented here:
// real ParVerify/RepVerify/TagVerify bound to stake_stmt are required before any
// acceptance path may exist. The caller-supplied booleans are implementation
// availability flags, not proof-verification results.
StakeProofVerificationSkeletonResult CheckStakeProofVerificationSkeleton(const StakeVerificationOutputSkeletonResult& prefixResult, bool stakeStatementAvailable, bool parVerifierAvailable, bool repVerifierAvailable);
// Revised spec StakeVerify steps 1-11 composition with no accepting result. This
// runs the step-1-through-6 prefix and stops at the real step-7 Spark-rule blocker.
StakeVerificationBlockedSkeletonResult CheckStakeVerificationBlockedSkeleton(const StakeTx& tx, const CHelsingState& helsingState, const ValidationStateView& view, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid, size_t n, size_t m, const std::map<OutputId, bool>& outputSatisfiesSparkRules);
// Revised spec post-acceptance stake_id construction is deliberately unimplemented:
// canonical(tx) and the consensus hash domain must be defined before deriving stake_id.
StakeIdConstructionSkeletonResult CheckStakeIdConstructionSkeleton(bool stakeVerifyAccepted, bool canonicalStakeTxEncodingAvailable);
// Revised spec optional collateral-margin subset: validate V_STAKE + margin as integers
// before scalar conversion. The caller must separately enforce V_MAX < q.
bool IsHelsingStakeValueWithMarginInRangeSkeleton(CAmount stakeValue, CAmount margin, CAmount vMax);
bool IsValidOutputId(const OutputId& output_id);
bool IsValidPublicPoint(const GroupElement& point);
bool IsValidSparkOutputRecord(const SparkOutputRecord& output);
// Revised spec section 13 eligibility gate: an output may only be considered
// Helsing-eligible if every spend path reveals the Spark spend tag. The caller
// supplies that path analysis; this does not inspect scripts or mark state.
bool IsHelsingEligibleOutputCandidateSkeleton(const SparkOutputRecord& output, bool allSpendPathsRevealTags);
// Revised spec section 13 real eligibility marking is deliberately blocked:
// deployed consensus must analyze Spark spend paths, persist the marking, and
// provide undo data before any output may be marked Helsing-eligible.
HelsingEligibleOutputMarkingSkeletonResult CheckHelsingEligibleOutputMarkingSkeleton(const SparkOutputRecord& output, bool sparkSpendPathAnalysisAvailable, bool eligibilityPersistenceAvailable, bool eligibilityUndoAvailable);
// Marks caller-selected output records as Helsing-eligible after the section 13
// candidate predicate passes. This is in-memory only and does not decide spend-path
// policy, persist state, or provide undo data.
bool MarkHelsingEligibleOutputCandidatesSkeleton(const std::vector<std::pair<OutputId, bool>>& candidates, std::map<OutputId, SparkOutputRecord>& outputs);
// Revised spec ValidateBlock step 9 payout-output record construction is deliberately
// blocked here. Real code must use the payout transaction's (txid, vout) output_id and
// parse the verified payout Coin into Spark output fields before inserting SparkOutputs.
PayoutOutputRecordConstructionSkeletonResult CheckPayoutOutputRecordConstructionSkeleton(bool payoutVerifyAccepted, const OutputId& output_id, bool payoutCoinParsingAvailable);

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
// Revised-spec PayoutVerify steps 1-7 only. This composes field/canonical prefix
// checks with stake lookup, active status, spent-tag checks, and maturity.
PayoutVerificationEligibilitySkeletonResult CheckPayoutVerificationEligibilitySkeleton(const PayoutTxSkeleton& tx, const ValidationStateView& view, int currentHeight, int stakeMaturity, bool canonicalEncodingsValid);
// Revised-spec PayoutVerify steps 8-13 are deliberately blocked here. The caller
// supplies implementation-availability flags, not consensus validation results;
// this helper has no accepting result and does not call the payout algorithm.
PayoutVerificationSuffixSkeletonResult CheckPayoutVerificationSuffixSkeleton(const PayoutVerificationEligibilitySkeletonResult& eligibilityResult, bool registeredPayoutAddressExtractionAvailable, bool deterministicSelectionAvailable, bool expectedPayoutAmountAvailable, bool payoutIdConstructionAvailable, bool payoutAlgorithmAvailable);
// Revised-spec PayoutVerify steps 1-13 composition with no accepting result. This
// runs the step-1-through-7 eligibility checks, then the step-8-through-13 blocker.
PayoutVerificationBlockedSkeletonResult CheckPayoutVerificationBlockedSkeleton(const PayoutTxSkeleton& tx, const ValidationStateView& view, int currentHeight, int stakeMaturity, bool canonicalEncodingsValid, bool registeredPayoutAddressExtractionAvailable, bool deterministicSelectionAvailable, bool expectedPayoutAmountAvailable, bool payoutIdConstructionAvailable, bool payoutAlgorithmAvailable);
// Structural skeleton for revised-spec PayoutVerify steps 3-13 using caller-supplied
// expected public values for steps that are not implemented yet. This does not parse
// contexts, select masternodes, compute j, run Payout, or verify payout coins.
PayoutVerificationSkeletonResult CheckPayoutVerificationSkeleton(const PayoutTxSkeleton& tx, const ValidationStateView& view, int currentHeight, int stakeMaturity, const PayoutBlockContextSkeleton& context, const PayoutAddressBlob& registeredAddress, const uint256& expectedStakeId, CAmount expectedAmount, CAmount vMax, const PayoutCoinBlob& expectedCoin);

// Revised-spec section 16 requires distinct payout_index values when a block
// contains more than one masternode payout.
bool ArePayoutIndexesDistinctSkeleton(const std::vector<PayoutTxSkeleton>& payout_txs);

// Structural skeleton for revised-spec StakeUpdateVerify steps 1-3 only.
StakeValidationResult CheckStakeUpdateEligibilitySkeleton(const uint256& stake_id, const ValidationStateView& view);

// Structural skeleton for revised-spec StakeUpdateVerify field presence plus steps 1-3.
// This deliberately stops before context parsing, update_pk extraction, enc_context(m_new),
// signature verification, and update effective-height rules.
StakeUpdateVerificationSkeletonResult CheckStakeUpdateVerificationSkeleton(const StakeUpdateTx& tx, const ValidationStateView& view);

// Revised-spec StakeUpdateVerify steps 4-6 are deliberately blocked here. The
// caller supplies implementation-availability flags, not consensus validation
// results; this helper has no accepting result.
StakeUpdateAuthorizationSkeletonResult CheckStakeUpdateAuthorizationSkeleton(const StakeUpdateVerificationSkeletonResult& prefixResult, bool updatePublicKeyExtractionAvailable, bool canonicalNewContextValidationAvailable, bool updateSignatureHashingAvailable);

// Revised-spec StakeUpdateVerify steps 1-6 composition with no accepting result.
// This runs field/eligibility checks, then the authorization blocker.
StakeUpdateVerificationBlockedSkeletonResult CheckStakeUpdateVerificationBlockedSkeleton(const StakeUpdateTx& tx, const ValidationStateView& view, bool updatePublicKeyExtractionAvailable, bool canonicalNewContextValidationAvailable, bool updateSignatureHashingAvailable);

// Revised-spec section 14 leaves the update effective-height rule as a consensus
// parameter. This blocker has no accepting result and must not be used to apply updates.
StakeUpdateEffectiveHeightSkeletonResult CheckStakeUpdateEffectiveHeightSkeleton(bool stakeUpdateVerifyAccepted, bool effectiveHeightRuleAvailable);

// Revised-spec sections 12 and 14 do not define how multiple accepted updates
// for one stake_id in the same block are ordered or rejected. This blocker has
// no accepting result and must not be used to apply updates.
StakeUpdateSameBlockPolicySkeletonResult CheckStakeUpdateSameBlockPolicySkeleton(const std::vector<uint256>& acceptedStakeUpdateIds, bool stakeUpdatesAccepted);

// Block-level skeleton for revised-spec ValidateBlock step 4 and StakeUpdateVerify steps 1-3 only.
// This deliberately stops before context parsing, signature verification, effective-height rules,
// and same-block duplicate-update policy.
StakeValidationResult CheckStakeUpdateBlockSkeleton(const std::vector<StakeUpdateTx>& update_txs, const ValidationStateView& view);

// Block-level skeleton for revised-spec ValidateBlock steps 3-5 using the existing inert
// stake, update, and payout prefix helpers. The caller must supply already collected
// BlockSpentTags in view and this does not perform Spark spend validation or state mutation.
StakeValidationResult CheckBlockValidationPrefixSkeleton(const std::vector<StakeTx>& stake_txs, const std::vector<StakeUpdateTx>& update_txs, const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& view, int currentHeight, int stakeMaturity);
// Block-level skeleton for revised-spec ValidateBlock steps 2-5. The caller supplies
// tags revealed by already valid Spark spends; this builds BlockSpentTags in a copied
// view, then runs the existing non-mutating stake/update/payout prefix checks.
BlockValidationPrefixWithSpentTagsSkeletonResult CheckBlockValidationPrefixWithSpentTagsSkeleton(const std::vector<GroupElement>& sparkSpendTags, const std::vector<StakeTx>& stake_txs, const std::vector<StakeUpdateTx>& update_txs, const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& parentView, int currentHeight, int stakeMaturity);
// Revised spec ValidateBlock step 1 is deliberately blocked here. Real ordinary
// Spark spend validation must run before revealed tags can be collected into
// BlockSpentTags.
BlockValidationSparkPrepassSkeletonResult CheckBlockValidationSparkPrepassSkeleton(bool ordinarySparkSpendValidationAvailable);
// Full block validation remains deliberately blocked at section 12 step 1:
// real Spark spend validation/tag extraction, StakeVerify, StakeUpdateVerify,
// PayoutVerify, persistence, and undo data are required.
BlockValidationBlockedSkeletonResult CheckBlockValidationBlockedSkeleton(const std::vector<GroupElement>& sparkSpendTags, const std::vector<StakeTx>& stake_txs, const std::vector<StakeUpdateTx>& update_txs, const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& parentView, int currentHeight, int stakeMaturity);

// Persistent integration for revised spec section 6 state and section 12
// mutation is deliberately blocked until accepted block data, storage, undo
// serialization, and disconnect replay are all implemented together.
PersistentBlockApplicationSkeletonResult CheckPersistentBlockApplicationSkeleton(bool acceptedBlockDataAvailable, bool helsingStateStorageAvailable, bool sparkOutputStorageAvailable, bool undoDataSerializationAvailable);

} // namespace helsing

#endif // FIRO_HELSING_VALIDATION_H
