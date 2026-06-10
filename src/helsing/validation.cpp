// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/validation.h"

#include "primitives/transaction.h"
#include "spark/state.h"

#include <exception>
#include <limits>
#include <set>
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
    case StakeValidationResult::OUTPUT_SPARK_RULES_FAILED:
        return "OUTPUT_SPARK_RULES_FAILED";
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

bool AreHelsingValueParametersInRangeSkeleton(CAmount stakeValue, CAmount payoutValue, CAmount vMax, bool vMaxLessThanGroupOrder)
{
    return vMaxLessThanGroupOrder &&
           IsHelsingValueInRange(stakeValue, vMax) &&
           IsHelsingValueInRange(payoutValue, vMax);
}

HelsingValueScalarConversionSkeletonResult CheckHelsingValueScalarConversionSkeleton(CAmount stakeValue, CAmount payoutValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool valueToScalarEncodingAvailable, bool injectiveScalarConversionAvailable)
{
    if (!IsHelsingValueInRange(stakeValue, vMax) ||
        !IsHelsingValueInRange(payoutValue, vMax)) {
        return HelsingValueScalarConversionSkeletonResult::VALUE_INTEGER_DOMAIN_INVALID;
    }
    if (!vMaxLessThanGroupOrder) {
        return HelsingValueScalarConversionSkeletonResult::SCALAR_ORDER_BOUND_INVALID;
    }
    if (!valueToScalarEncodingAvailable) {
        return HelsingValueScalarConversionSkeletonResult::VALUE_TO_SCALAR_ENCODING_UNIMPLEMENTED;
    }
    if (!injectiveScalarConversionAvailable) {
        return HelsingValueScalarConversionSkeletonResult::INJECTIVE_SCALAR_CONVERSION_UNIMPLEMENTED;
    }

    return HelsingValueScalarConversionSkeletonResult::CONSENSUS_WIRING_UNIMPLEMENTED;
}

HelsingStakeFeeHandlingSkeletonResult CheckHelsingStakeFeeHandlingSkeleton(bool stakeValueProofAccepted, bool ordinaryFeeMechanismAvailable, bool feeCollateralSeparationAvailable, bool consensusFeePolicyAvailable)
{
    if (!stakeValueProofAccepted) {
        return HelsingStakeFeeHandlingSkeletonResult::STAKE_VALUE_PROOF_NOT_ACCEPTED;
    }
    if (!ordinaryFeeMechanismAvailable) {
        return HelsingStakeFeeHandlingSkeletonResult::ORDINARY_FEE_MECHANISM_UNIMPLEMENTED;
    }
    if (!feeCollateralSeparationAvailable) {
        return HelsingStakeFeeHandlingSkeletonResult::FEE_COLLATERAL_SEPARATION_UNIMPLEMENTED;
    }
    if (!consensusFeePolicyAvailable) {
        return HelsingStakeFeeHandlingSkeletonResult::CONSENSUS_FEE_POLICY_UNIMPLEMENTED;
    }

    return HelsingStakeFeeHandlingSkeletonResult::CONSENSUS_WIRING_UNIMPLEMENTED;
}

bool IsStakeValueParameterInRangeSkeleton(CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder)
{
    return vMaxLessThanGroupOrder && IsHelsingValueInRange(stakeValue, vMax);
}

bool IsExpectedPayoutAmountInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax)
{
    return payoutValue == expectedAmount && IsHelsingValueInRange(payoutValue, vMax);
}

bool IsPayoutValueParameterInRangeSkeleton(CAmount payoutValue, CAmount expectedAmount, CAmount vMax, bool vMaxLessThanGroupOrder)
{
    return vMaxLessThanGroupOrder && IsExpectedPayoutAmountInRangeSkeleton(payoutValue, expectedAmount, vMax);
}

PayoutAmountDerivationSkeletonResult CheckPayoutAmountDerivationSkeleton(bool payoutSelectionPrefixAccepted, bool consensusRewardRulesAvailable, bool expectedPayoutAmountDerivationAvailable, bool payoutAmountComparisonAvailable)
{
    if (!payoutSelectionPrefixAccepted) {
        return PayoutAmountDerivationSkeletonResult::PAYOUT_SELECTION_PREFIX_FAILED;
    }
    if (!consensusRewardRulesAvailable) {
        return PayoutAmountDerivationSkeletonResult::CONSENSUS_REWARD_RULES_UNIMPLEMENTED;
    }
    if (!expectedPayoutAmountDerivationAvailable) {
        return PayoutAmountDerivationSkeletonResult::EXPECTED_PAYOUT_AMOUNT_DERIVATION_UNIMPLEMENTED;
    }
    if (!payoutAmountComparisonAvailable) {
        return PayoutAmountDerivationSkeletonResult::PAYOUT_AMOUNT_COMPARISON_UNIMPLEMENTED;
    }

    return PayoutAmountDerivationSkeletonResult::PAYOUT_VALUE_DOMAIN_CONSENSUS_PARAMETERS_UNIMPLEMENTED;
}

bool IsCompletePayoutTxSkeleton(const PayoutTxSkeleton& tx)
{
    return !tx.selected_stake_id.IsNull() &&
           !tx.addr_pk.empty() &&
           !tx.coin.empty();
}

PayoutVerificationPrefixSkeletonResult CheckPayoutVerificationPrefixSkeleton(const PayoutTxSkeleton& tx, bool canonicalEncodingsValid)
{
    if (!IsCompletePayoutTxSkeleton(tx)) {
        return PayoutVerificationPrefixSkeletonResult::TX_INCOMPLETE;
    }
    if (!canonicalEncodingsValid) {
        return PayoutVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL;
    }

    return PayoutVerificationPrefixSkeletonResult::OK;
}

bool DoesPayoutAddressMatchRegisteredSkeleton(const PayoutTxSkeleton& tx, const PayoutAddressBlob& registeredAddress)
{
    return !tx.addr_pk.empty() && !registeredAddress.empty() && tx.addr_pk.bytes == registeredAddress.bytes;
}

bool DoesPayoutStakeMatchExpectedSkeleton(const PayoutTxSkeleton& tx, const uint256& expectedStakeId)
{
    return !tx.selected_stake_id.IsNull() &&
           !expectedStakeId.IsNull() &&
           tx.selected_stake_id == expectedStakeId;
}

bool DoesPayoutCoinMatchExpectedSkeleton(const PayoutTxSkeleton& tx, const PayoutCoinBlob& expectedCoin)
{
    return !tx.coin.empty() && !expectedCoin.empty() && tx.coin.bytes == expectedCoin.bytes;
}

bool IsCompletePayoutBlockContextSkeleton(const PayoutBlockContextSkeleton& context)
{
    return !context.chain_id.empty() &&
           context.block_height >= 0 &&
           !context.prev_block_hash.IsNull() &&
           !context.selected_stake_id.IsNull();
}

bool DoesPayoutContextMatchTxSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context)
{
    return tx.payout_index == context.payout_index &&
           tx.selected_stake_id == context.selected_stake_id;
}

bool ArePayoutIdInputsCompleteSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context)
{
    return IsCompletePayoutBlockContextSkeleton(context) &&
           DoesPayoutContextMatchTxSkeleton(tx, context);
}

PayoutIdConstructionSkeletonResult CheckPayoutIdConstructionSkeleton(const PayoutTxSkeleton& tx, const PayoutBlockContextSkeleton& context, bool canonicalPayoutIdEncodingAvailable)
{
    if (!ArePayoutIdInputsCompleteSkeleton(tx, context)) {
        return PayoutIdConstructionSkeletonResult::PAYOUT_ID_INPUTS_INCOMPLETE;
    }
    if (!canonicalPayoutIdEncodingAvailable) {
        return PayoutIdConstructionSkeletonResult::CANONICAL_PAYOUT_ID_ENCODING_UNIMPLEMENTED;
    }

    return PayoutIdConstructionSkeletonResult::PAYOUT_ID_HASHING_UNIMPLEMENTED;
}

PayoutAlgorithmSkeletonResult CheckPayoutAlgorithmSkeleton(const PayoutTxSkeleton& tx, bool payoutIdentifierAvailable, bool payoutAddressParserAvailable, bool payoutKeyDerivationAvailable, bool payoutCoinConstructionAvailable)
{
    if (!IsCompletePayoutTxSkeleton(tx)) {
        return PayoutAlgorithmSkeletonResult::PAYOUT_TX_INCOMPLETE;
    }
    if (!payoutIdentifierAvailable) {
        return PayoutAlgorithmSkeletonResult::PAYOUT_IDENTIFIER_UNAVAILABLE;
    }
    if (!payoutAddressParserAvailable) {
        return PayoutAlgorithmSkeletonResult::PAYOUT_ADDRESS_PARSING_UNIMPLEMENTED;
    }
    if (!payoutKeyDerivationAvailable) {
        return PayoutAlgorithmSkeletonResult::PAYOUT_KEY_DERIVATION_UNIMPLEMENTED;
    }
    if (!payoutCoinConstructionAvailable) {
        return PayoutAlgorithmSkeletonResult::PAYOUT_COIN_CONSTRUCTION_UNIMPLEMENTED;
    }

    return PayoutAlgorithmSkeletonResult::PAYOUT_COIN_COMPARISON_UNIMPLEMENTED;
}

PayoutPublicValidationResult CheckPayoutPublicFieldsSkeleton(
    const PayoutTxSkeleton& tx,
    const PayoutBlockContextSkeleton& context,
    const PayoutAddressBlob& registeredAddress,
    const uint256& expectedStakeId,
    CAmount expectedAmount,
    CAmount vMax,
    const PayoutCoinBlob& expectedCoin)
{
    if (!DoesPayoutAddressMatchRegisteredSkeleton(tx, registeredAddress)) {
        return PayoutPublicValidationResult::ADDRESS_MISMATCH;
    }
    if (!DoesPayoutStakeMatchExpectedSkeleton(tx, expectedStakeId)) {
        return PayoutPublicValidationResult::SELECTED_STAKE_MISMATCH;
    }
    if (!IsExpectedPayoutAmountInRangeSkeleton(tx.V_PAYOUT, expectedAmount, vMax)) {
        return PayoutPublicValidationResult::INVALID_PAYOUT_AMOUNT;
    }
    if (!ArePayoutIdInputsCompleteSkeleton(tx, context)) {
        return PayoutPublicValidationResult::INVALID_PAYOUT_ID_INPUTS;
    }
    if (!DoesPayoutCoinMatchExpectedSkeleton(tx, expectedCoin)) {
        return PayoutPublicValidationResult::PAYOUT_COIN_MISMATCH;
    }

    return PayoutPublicValidationResult::OK;
}

bool IsCompleteStakeUpdateTxSkeleton(const StakeUpdateTx& tx)
{
    return !tx.stake_id.IsNull() &&
           !tx.m_new.bytes.empty() &&
           !tx.sig_update.empty();
}

bool IsCompleteStakeTxSkeleton(const StakeTx& tx)
{
    return !tx.inCoinIDs.empty() &&
           !tx.m.bytes.empty() &&
           !tx.pi_par.empty() &&
           !tx.pi_val.empty() &&
           !tx.pi_tag.empty();
}

HelsingTransactionWiringSkeletonResult CheckHelsingTransactionWiringSkeleton(bool consensusTxCarrierAvailable, bool stakeTxExtractionAvailable, bool stakeUpdateTxExtractionAvailable, bool payoutTxExtractionAvailable)
{
    if (!consensusTxCarrierAvailable) {
        return HelsingTransactionWiringSkeletonResult::CONSENSUS_TX_CARRIER_UNIMPLEMENTED;
    }
    if (!stakeTxExtractionAvailable) {
        return HelsingTransactionWiringSkeletonResult::STAKE_TX_EXTRACTION_UNIMPLEMENTED;
    }
    if (!stakeUpdateTxExtractionAvailable) {
        return HelsingTransactionWiringSkeletonResult::STAKE_UPDATE_TX_EXTRACTION_UNIMPLEMENTED;
    }
    if (!payoutTxExtractionAvailable) {
        return HelsingTransactionWiringSkeletonResult::PAYOUT_TX_EXTRACTION_UNIMPLEMENTED;
    }

    return HelsingTransactionWiringSkeletonResult::BLOCK_INTEGRATION_UNIMPLEMENTED;
}

HelsingConsensusActivationSkeletonResult CheckHelsingConsensusActivationSkeleton(bool sparkCompatibilityReviewAvailable, bool activationParametersAvailable, bool chainparamsDeploymentWiringAvailable, bool historicalStateBootstrapAvailable)
{
    if (!sparkCompatibilityReviewAvailable) {
        return HelsingConsensusActivationSkeletonResult::SPARK_COMPATIBILITY_REVIEW_UNIMPLEMENTED;
    }
    if (!activationParametersAvailable) {
        return HelsingConsensusActivationSkeletonResult::ACTIVATION_PARAMETERS_UNIMPLEMENTED;
    }
    if (!chainparamsDeploymentWiringAvailable) {
        return HelsingConsensusActivationSkeletonResult::CHAINPARAMS_DEPLOYMENT_WIRING_UNIMPLEMENTED;
    }
    if (!historicalStateBootstrapAvailable) {
        return HelsingConsensusActivationSkeletonResult::HISTORICAL_STATE_BOOTSTRAP_UNIMPLEMENTED;
    }

    return HelsingConsensusActivationSkeletonResult::ACTIVATION_ENFORCEMENT_UNIMPLEMENTED;
}

HelsingMasternodeLifecycleSkeletonResult CheckHelsingMasternodeLifecycleSkeleton(bool acceptedStakeAvailable, bool masternodeContextExtractionAvailable, bool masternodeRegistrationIntegrationAvailable, bool stakeUpdateLifecycleIntegrationAvailable, bool payoutSelectionIntegrationAvailable)
{
    if (!acceptedStakeAvailable) {
        return HelsingMasternodeLifecycleSkeletonResult::ACCEPTED_STAKE_UNAVAILABLE;
    }
    if (!masternodeContextExtractionAvailable) {
        return HelsingMasternodeLifecycleSkeletonResult::MASTERNODE_CONTEXT_EXTRACTION_UNIMPLEMENTED;
    }
    if (!masternodeRegistrationIntegrationAvailable) {
        return HelsingMasternodeLifecycleSkeletonResult::MASTERNODE_REGISTRATION_INTEGRATION_UNIMPLEMENTED;
    }
    if (!stakeUpdateLifecycleIntegrationAvailable) {
        return HelsingMasternodeLifecycleSkeletonResult::STAKE_UPDATE_LIFECYCLE_INTEGRATION_UNIMPLEMENTED;
    }
    if (!payoutSelectionIntegrationAvailable) {
        return HelsingMasternodeLifecycleSkeletonResult::PAYOUT_SELECTION_INTEGRATION_UNIMPLEMENTED;
    }

    return HelsingMasternodeLifecycleSkeletonResult::MASTERNODE_PAYMENT_INTEGRATION_UNIMPLEMENTED;
}

HelsingCanonicalTranscriptSkeletonResult CheckHelsingCanonicalTranscriptSkeleton(bool canonicalEncodingGrammarAvailable, bool hashToFieldMethodAvailable, bool domainSeparationStringsAvailable, bool proofTranscriptLabelsAvailable)
{
    if (!canonicalEncodingGrammarAvailable) {
        return HelsingCanonicalTranscriptSkeletonResult::CANONICAL_ENCODING_GRAMMAR_UNIMPLEMENTED;
    }
    if (!hashToFieldMethodAvailable) {
        return HelsingCanonicalTranscriptSkeletonResult::HASH_TO_FIELD_METHOD_UNIMPLEMENTED;
    }
    if (!domainSeparationStringsAvailable) {
        return HelsingCanonicalTranscriptSkeletonResult::DOMAIN_SEPARATION_STRINGS_UNIMPLEMENTED;
    }
    if (!proofTranscriptLabelsAvailable) {
        return HelsingCanonicalTranscriptSkeletonResult::PROOF_TRANSCRIPT_LABELS_UNIMPLEMENTED;
    }

    return HelsingCanonicalTranscriptSkeletonResult::CONSENSUS_WIRING_UNIMPLEMENTED;
}

StakeVerificationPrefixSkeletonResult CheckStakeVerificationPrefixSkeleton(const StakeTx& tx, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid)
{
    if (!IsCompleteStakeTxSkeleton(tx)) {
        return StakeVerificationPrefixSkeletonResult::TX_INCOMPLETE;
    }
    if (!canonicalEncodingsValid) {
        return StakeVerificationPrefixSkeletonResult::MALFORMED_OR_NONCANONICAL;
    }
    if (!IsStakeValueParameterInRangeSkeleton(stakeValue, vMax, vMaxLessThanGroupOrder)) {
        return StakeVerificationPrefixSkeletonResult::INVALID_VALUE_PARAMETER;
    }

    return StakeVerificationPrefixSkeletonResult::OK;
}

StakeValidationResult CheckStakeTagStateSkeleton(const GroupElement& tag, const CHelsingState& helsingState)
{
    if (helsingState.IsSpentTag(tag)) {
        return StakeValidationResult::TAG_ALREADY_SPENT;
    }
    if (helsingState.IsActiveTag(tag)) {
        return StakeValidationResult::TAG_ALREADY_ACTIVE;
    }

    return StakeValidationResult::OK;
}

bool IsStakeContextValidSkeleton(const StakeContext& context, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid)
{
    return !context.bytes.empty() &&
           canonicalContextValid &&
           payoutAddressValid &&
           updatePublicKeyValid &&
           nodeSigningKeyMaterialValid;
}

StakeContextValidationBlockedSkeletonResult CheckStakeContextValidationBlockedSkeleton(const StakeContext& context, bool canonicalContextEncodingAvailable, bool payoutAddressValidationAvailable, bool updatePublicKeyValidationAvailable, bool nodeSigningKeyValidationAvailable)
{
    if (context.bytes.empty()) {
        return StakeContextValidationBlockedSkeletonResult::STAKE_CONTEXT_EMPTY;
    }
    if (!canonicalContextEncodingAvailable) {
        return StakeContextValidationBlockedSkeletonResult::CANONICAL_CONTEXT_ENCODING_UNIMPLEMENTED;
    }
    if (!payoutAddressValidationAvailable) {
        return StakeContextValidationBlockedSkeletonResult::PAYOUT_ADDRESS_VALIDATION_UNIMPLEMENTED;
    }
    if (!updatePublicKeyValidationAvailable) {
        return StakeContextValidationBlockedSkeletonResult::UPDATE_PUBLIC_KEY_VALIDATION_UNIMPLEMENTED;
    }
    if (!nodeSigningKeyValidationAvailable) {
        return StakeContextValidationBlockedSkeletonResult::NODE_SIGNING_KEY_VALIDATION_UNIMPLEMENTED;
    }

    return StakeContextValidationBlockedSkeletonResult::MASTERNODE_CONTEXT_RULES_UNIMPLEMENTED;
}

StakeVerificationContextSkeletonResult CheckStakeVerificationContextSkeleton(
    const StakeTx& tx,
    const CHelsingState& helsingState,
    CAmount stakeValue,
    CAmount vMax,
    bool vMaxLessThanGroupOrder,
    bool canonicalEncodingsValid,
    bool canonicalContextValid,
    bool payoutAddressValid,
    bool updatePublicKeyValid,
    bool nodeSigningKeyMaterialValid)
{
    StakeVerificationContextSkeletonResult result;
    result.prefix_result = CheckStakeVerificationPrefixSkeleton(tx, stakeValue, vMax, vMaxLessThanGroupOrder, canonicalEncodingsValid);
    if (result.prefix_result != StakeVerificationPrefixSkeletonResult::OK) {
        return result;
    }

    result.tag_result = CheckStakeTagStateSkeleton(tx.T, helsingState);
    if (result.tag_result != StakeValidationResult::OK) {
        return result;
    }

    result.context_valid = IsStakeContextValidSkeleton(tx.m, canonicalContextValid, payoutAddressValid, updatePublicKeyValid, nodeSigningKeyMaterialValid);
    return result;
}

StakeValidationResult CheckStakeCoverSetIdentifiersSkeleton(const std::vector<OutputId>& inCoinIDs, size_t n, size_t m)
{
    if (!IsValidCoverSetCardinality(inCoinIDs.size(), n, m)) {
        return StakeValidationResult::INVALID_COVER_SET_CARDINALITY;
    }
    if (!IsStrictlySortedAndDistinct(inCoinIDs)) {
        return StakeValidationResult::INCOINIDS_NOT_SORTED_DISTINCT;
    }

    return StakeValidationResult::OK;
}

StakeVerificationCoverSetSkeletonResult CheckStakeVerificationCoverSetSkeleton(
    const StakeTx& tx,
    const CHelsingState& helsingState,
    CAmount stakeValue,
    CAmount vMax,
    bool vMaxLessThanGroupOrder,
    bool canonicalEncodingsValid,
    bool canonicalContextValid,
    bool payoutAddressValid,
    bool updatePublicKeyValid,
    bool nodeSigningKeyMaterialValid,
    size_t n,
    size_t m)
{
    StakeVerificationCoverSetSkeletonResult result;
    result.context_result = CheckStakeVerificationContextSkeleton(
        tx,
        helsingState,
        stakeValue,
        vMax,
        vMaxLessThanGroupOrder,
        canonicalEncodingsValid,
        canonicalContextValid,
        payoutAddressValid,
        updatePublicKeyValid,
        nodeSigningKeyMaterialValid);
    if (result.context_result.prefix_result != StakeVerificationPrefixSkeletonResult::OK ||
        result.context_result.tag_result != StakeValidationResult::OK ||
        !result.context_result.context_valid) {
        return result;
    }

    result.cover_set_result = CheckStakeCoverSetIdentifiersSkeleton(tx.inCoinIDs, n, m);
    return result;
}

StakeValidationResult CheckStakeCoverSetOutputRulesSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view, const std::map<OutputId, bool>& outputSatisfiesSparkRules)
{
    for (const OutputId& output_id : inCoinIDs) {
        const SparkOutputRecord* output = view.FindSparkOutput(output_id);
        if (output == nullptr) {
            return StakeValidationResult::OUTPUT_NOT_FOUND;
        }
        if (!output->helsing_eligible) {
            return StakeValidationResult::OUTPUT_NOT_ELIGIBLE;
        }
        const auto rulesIt = outputSatisfiesSparkRules.find(output_id);
        if (rulesIt == outputSatisfiesSparkRules.end() || !rulesIt->second) {
            return StakeValidationResult::OUTPUT_SPARK_RULES_FAILED;
        }
    }

    return StakeValidationResult::OK;
}

StakeCoverSetSparkRulesBlockedSkeletonResult CheckStakeCoverSetSparkRulesBlockedSkeleton(const std::vector<OutputId>& inCoinIDs, const ValidationStateView& view)
{
    StakeCoverSetSparkRulesBlockedSkeletonResult result;
    if (inCoinIDs.empty()) {
        result.output_result = StakeValidationResult::EMPTY_INCOINIDS;
        return result;
    }

    for (const OutputId& output_id : inCoinIDs) {
        const SparkOutputRecord* output = view.FindSparkOutput(output_id);
        if (output == nullptr) {
            result.output_result = StakeValidationResult::OUTPUT_NOT_FOUND;
            return result;
        }
        if (!output->helsing_eligible) {
            result.output_result = StakeValidationResult::OUTPUT_NOT_ELIGIBLE;
            return result;
        }

        result.spark_rules_result = StakeCoverSetSparkRulesSkeletonResult::SPARK_MATURITY_OR_COVER_SET_RULES_UNIMPLEMENTED;
        return result;
    }

    return result;
}

StakeVerificationOutputSkeletonResult CheckStakeVerificationOutputsSkeleton(
    const StakeTx& tx,
    const CHelsingState& helsingState,
    const ValidationStateView& view,
    CAmount stakeValue,
    CAmount vMax,
    bool vMaxLessThanGroupOrder,
    bool canonicalEncodingsValid,
    bool canonicalContextValid,
    bool payoutAddressValid,
    bool updatePublicKeyValid,
    bool nodeSigningKeyMaterialValid,
    size_t n,
    size_t m,
    const std::map<OutputId, bool>& outputSatisfiesSparkRules)
{
    StakeVerificationOutputSkeletonResult result;
    result.cover_set_result = CheckStakeVerificationCoverSetSkeleton(
        tx,
        helsingState,
        stakeValue,
        vMax,
        vMaxLessThanGroupOrder,
        canonicalEncodingsValid,
        canonicalContextValid,
        payoutAddressValid,
        updatePublicKeyValid,
        nodeSigningKeyMaterialValid,
        n,
        m);
    if (result.cover_set_result.context_result.prefix_result != StakeVerificationPrefixSkeletonResult::OK ||
        result.cover_set_result.context_result.tag_result != StakeValidationResult::OK ||
        !result.cover_set_result.context_result.context_valid ||
        result.cover_set_result.cover_set_result != StakeValidationResult::OK) {
        return result;
    }

    result.output_result = CheckStakeCoverSetOutputRulesSkeleton(tx.inCoinIDs, view, outputSatisfiesSparkRules);
    return result;
}

StakeStatementConstructionSkeletonResult CheckStakeStatementConstructionSkeleton(const StakeVerificationOutputSkeletonResult& prefixResult, bool incoinsRootHashingAvailable, bool contextHashingAvailable)
{
    if (prefixResult.cover_set_result.context_result.prefix_result != StakeVerificationPrefixSkeletonResult::OK ||
        prefixResult.cover_set_result.context_result.tag_result != StakeValidationResult::OK ||
        !prefixResult.cover_set_result.context_result.context_valid ||
        prefixResult.cover_set_result.cover_set_result != StakeValidationResult::OK ||
        prefixResult.output_result != StakeValidationResult::OK) {
        return StakeStatementConstructionSkeletonResult::STAKE_PREFIX_FAILED;
    }
    if (!incoinsRootHashingAvailable) {
        return StakeStatementConstructionSkeletonResult::INCOINS_ROOT_HASHING_UNIMPLEMENTED;
    }
    if (!contextHashingAvailable) {
        return StakeStatementConstructionSkeletonResult::CONTEXT_HASHING_UNIMPLEMENTED;
    }

    return StakeStatementConstructionSkeletonResult::STAKE_STATEMENT_HASHING_UNIMPLEMENTED;
}

StakeProofVerificationSkeletonResult CheckStakeProofVerificationSkeleton(const StakeVerificationOutputSkeletonResult& prefixResult, bool stakeStatementAvailable, bool parVerifierAvailable, bool repVerifierAvailable)
{
    if (prefixResult.cover_set_result.context_result.prefix_result != StakeVerificationPrefixSkeletonResult::OK ||
        prefixResult.cover_set_result.context_result.tag_result != StakeValidationResult::OK ||
        !prefixResult.cover_set_result.context_result.context_valid ||
        prefixResult.cover_set_result.cover_set_result != StakeValidationResult::OK ||
        prefixResult.output_result != StakeValidationResult::OK) {
        return StakeProofVerificationSkeletonResult::STAKE_PREFIX_FAILED;
    }
    if (!stakeStatementAvailable) {
        return StakeProofVerificationSkeletonResult::STAKE_STATEMENT_UNAVAILABLE;
    }
    if (!parVerifierAvailable) {
        return StakeProofVerificationSkeletonResult::PAR_VERIFY_UNIMPLEMENTED;
    }
    if (!repVerifierAvailable) {
        return StakeProofVerificationSkeletonResult::REP_VERIFY_UNIMPLEMENTED;
    }

    return StakeProofVerificationSkeletonResult::TAG_VERIFY_UNIMPLEMENTED;
}

StakeVerificationBlockedSkeletonResult CheckStakeVerificationBlockedSkeleton(const StakeTx& tx, const CHelsingState& helsingState, const ValidationStateView& view, CAmount stakeValue, CAmount vMax, bool vMaxLessThanGroupOrder, bool canonicalEncodingsValid, bool canonicalContextValid, bool payoutAddressValid, bool updatePublicKeyValid, bool nodeSigningKeyMaterialValid, size_t n, size_t m, const std::map<OutputId, bool>& /*outputSatisfiesSparkRules*/)
{
    StakeVerificationBlockedSkeletonResult result;
    result.output_result.cover_set_result = CheckStakeVerificationCoverSetSkeleton(
        tx,
        helsingState,
        stakeValue,
        vMax,
        vMaxLessThanGroupOrder,
        canonicalEncodingsValid,
        canonicalContextValid,
        payoutAddressValid,
        updatePublicKeyValid,
        nodeSigningKeyMaterialValid,
        n,
        m);
    if (result.output_result.cover_set_result.context_result.prefix_result != StakeVerificationPrefixSkeletonResult::OK ||
        result.output_result.cover_set_result.context_result.tag_result != StakeValidationResult::OK ||
        !result.output_result.cover_set_result.context_result.context_valid ||
        result.output_result.cover_set_result.cover_set_result != StakeValidationResult::OK) {
        return result;
    }

    result.spark_rules_result = CheckStakeCoverSetSparkRulesBlockedSkeleton(tx.inCoinIDs, view);
    result.output_result.output_result = result.spark_rules_result.output_result;
    return result;
}

StakeIdConstructionSkeletonResult CheckStakeIdConstructionSkeleton(bool stakeVerifyAccepted, bool canonicalStakeTxEncodingAvailable)
{
    if (!stakeVerifyAccepted) {
        return StakeIdConstructionSkeletonResult::STAKE_VERIFY_NOT_ACCEPTED;
    }
    if (!canonicalStakeTxEncodingAvailable) {
        return StakeIdConstructionSkeletonResult::CANONICAL_TX_ENCODING_UNIMPLEMENTED;
    }

    return StakeIdConstructionSkeletonResult::STAKE_ID_HASHING_UNIMPLEMENTED;
}

bool IsHelsingStakeValueWithMarginInRangeSkeleton(CAmount stakeValue, CAmount margin, CAmount vMax)
{
    if (!IsHelsingValueInRange(stakeValue, vMax) || margin < 0) {
        return false;
    }
    if (stakeValue > std::numeric_limits<CAmount>::max() - margin) {
        return false;
    }

    return stakeValue + margin < vMax;
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

bool IsHelsingEligibleOutputCandidateSkeleton(const SparkOutputRecord& output, bool allSpendPathsRevealTags)
{
    return IsValidSparkOutputRecord(output) && allSpendPathsRevealTags;
}

HelsingEligibleOutputMarkingSkeletonResult CheckHelsingEligibleOutputMarkingSkeleton(const SparkOutputRecord& output, bool sparkSpendPathAnalysisAvailable, bool eligibilityPersistenceAvailable, bool eligibilityUndoAvailable)
{
    if (!IsValidSparkOutputRecord(output)) {
        return HelsingEligibleOutputMarkingSkeletonResult::OUTPUT_RECORD_INVALID;
    }
    if (!sparkSpendPathAnalysisAvailable) {
        return HelsingEligibleOutputMarkingSkeletonResult::SPARK_SPEND_PATH_ANALYSIS_UNIMPLEMENTED;
    }
    if (!eligibilityPersistenceAvailable) {
        return HelsingEligibleOutputMarkingSkeletonResult::ELIGIBILITY_PERSISTENCE_UNIMPLEMENTED;
    }
    if (!eligibilityUndoAvailable) {
        return HelsingEligibleOutputMarkingSkeletonResult::ELIGIBILITY_UNDO_UNIMPLEMENTED;
    }

    return HelsingEligibleOutputMarkingSkeletonResult::CONSENSUS_WIRING_UNIMPLEMENTED;
}

bool MarkHelsingEligibleOutputCandidatesSkeleton(const std::vector<std::pair<OutputId, bool>>& candidates, std::map<OutputId, SparkOutputRecord>& outputs)
{
    std::map<OutputId, SparkOutputRecord> next(outputs);
    for (const auto& candidate : candidates) {
        auto it = next.find(candidate.first);
        if (it == next.end()) {
            return false;
        }
        if (it->second.output_id != candidate.first) {
            return false;
        }
        if (!IsHelsingEligibleOutputCandidateSkeleton(it->second, candidate.second)) {
            return false;
        }
        it->second.helsing_eligible = true;
    }

    outputs = std::move(next);
    return true;
}

PayoutOutputRecordConstructionSkeletonResult CheckPayoutOutputRecordConstructionSkeleton(bool payoutVerifyAccepted, const OutputId& output_id, bool payoutCoinParsingAvailable)
{
    if (!payoutVerifyAccepted) {
        return PayoutOutputRecordConstructionSkeletonResult::PAYOUT_VERIFY_NOT_ACCEPTED;
    }
    if (!IsValidOutputId(output_id)) {
        return PayoutOutputRecordConstructionSkeletonResult::OUTPUT_ID_UNAVAILABLE;
    }
    if (!payoutCoinParsingAvailable) {
        return PayoutOutputRecordConstructionSkeletonResult::PAYOUT_COIN_PARSING_UNIMPLEMENTED;
    }

    return PayoutOutputRecordConstructionSkeletonResult::PAYOUT_OUTPUT_RECORD_CONSTRUCTION_UNIMPLEMENTED;
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

bool ApplyAcceptedPayoutOutputRecordsSkeleton(const std::vector<SparkOutputRecord>& acceptedOutputs, std::map<OutputId, SparkOutputRecord>& outputs)
{
    std::map<OutputId, SparkOutputRecord> next(outputs);
    for (const SparkOutputRecord& output : acceptedOutputs) {
        if (!IsValidSparkOutputRecord(output)) {
            return false;
        }
        if (!next.emplace(output.output_id, output).second) {
            return false;
        }
    }

    outputs = std::move(next);
    return true;
}

bool ApplyAcceptedBlockWithPayoutOutputsSkeleton(
    CHelsingState& helsingState,
    std::map<OutputId, SparkOutputRecord>& outputs,
    const std::unordered_set<GroupElement, spark::CLTagHash>& blockSpentTags,
    const std::vector<std::pair<uint256, StakeTx>>& acceptedStakes,
    const std::vector<std::pair<uint256, StakeContext>>& acceptedUpdates,
    const std::vector<SparkOutputRecord>& acceptedPayoutOutputs,
    int nHeight)
{
    CHelsingState nextState = helsingState;
    std::map<OutputId, SparkOutputRecord> nextOutputs(outputs);

    if (!nextState.ApplyAcceptedBlockSkeleton(blockSpentTags, acceptedStakes, acceptedUpdates, nHeight)) {
        return false;
    }
    if (!ApplyAcceptedPayoutOutputRecordsSkeleton(acceptedPayoutOutputs, nextOutputs)) {
        return false;
    }

    helsingState = nextState;
    outputs = std::move(nextOutputs);
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

StakeValidationResult CheckPayoutBlockEligibilitySkeleton(const std::vector<PayoutTxSkeleton>& payout_txs, const ValidationStateView& view, int currentHeight, int stakeMaturity)
{
    for (const PayoutTxSkeleton& tx : payout_txs) {
        const StakeValidationResult result = CheckPayoutEligibilitySkeleton(tx.selected_stake_id, view, currentHeight, stakeMaturity);
        if (result != StakeValidationResult::OK) {
            return result;
        }
    }

    return StakeValidationResult::OK;
}

PayoutVerificationEligibilitySkeletonResult CheckPayoutVerificationEligibilitySkeleton(const PayoutTxSkeleton& tx, const ValidationStateView& view, int currentHeight, int stakeMaturity, bool canonicalEncodingsValid)
{
    PayoutVerificationEligibilitySkeletonResult result;
    result.prefix_result = CheckPayoutVerificationPrefixSkeleton(tx, canonicalEncodingsValid);
    if (result.prefix_result != PayoutVerificationPrefixSkeletonResult::OK) {
        return result;
    }

    result.stake_result = CheckPayoutEligibilitySkeleton(tx.selected_stake_id, view, currentHeight, stakeMaturity);
    return result;
}

PayoutVerificationSuffixSkeletonResult CheckPayoutVerificationSuffixSkeleton(const PayoutVerificationEligibilitySkeletonResult& eligibilityResult, bool registeredPayoutAddressExtractionAvailable, bool deterministicSelectionAvailable, bool expectedPayoutAmountAvailable, bool payoutIdConstructionAvailable, bool payoutAlgorithmAvailable)
{
    if (eligibilityResult.prefix_result != PayoutVerificationPrefixSkeletonResult::OK ||
        eligibilityResult.stake_result != StakeValidationResult::OK) {
        return PayoutVerificationSuffixSkeletonResult::PAYOUT_ELIGIBILITY_FAILED;
    }
    if (!registeredPayoutAddressExtractionAvailable) {
        return PayoutVerificationSuffixSkeletonResult::REGISTERED_PAYOUT_ADDRESS_EXTRACTION_UNIMPLEMENTED;
    }
    if (!deterministicSelectionAvailable) {
        return PayoutVerificationSuffixSkeletonResult::DETERMINISTIC_SELECTION_UNIMPLEMENTED;
    }
    if (!expectedPayoutAmountAvailable) {
        return PayoutVerificationSuffixSkeletonResult::EXPECTED_PAYOUT_AMOUNT_UNIMPLEMENTED;
    }
    if (!payoutIdConstructionAvailable) {
        return PayoutVerificationSuffixSkeletonResult::PAYOUT_ID_CONSTRUCTION_UNIMPLEMENTED;
    }
    if (!payoutAlgorithmAvailable) {
        return PayoutVerificationSuffixSkeletonResult::PAYOUT_ALGORITHM_UNIMPLEMENTED;
    }

    return PayoutVerificationSuffixSkeletonResult::PAYOUT_COIN_COMPARISON_UNIMPLEMENTED;
}

PayoutVerificationBlockedSkeletonResult CheckPayoutVerificationBlockedSkeleton(const PayoutTxSkeleton& tx, const ValidationStateView& view, int currentHeight, int stakeMaturity, bool canonicalEncodingsValid, bool registeredPayoutAddressExtractionAvailable, bool deterministicSelectionAvailable, bool expectedPayoutAmountAvailable, bool payoutIdConstructionAvailable, bool payoutAlgorithmAvailable)
{
    PayoutVerificationBlockedSkeletonResult result;
    result.eligibility_result = CheckPayoutVerificationEligibilitySkeleton(tx, view, currentHeight, stakeMaturity, canonicalEncodingsValid);
    result.suffix_result = CheckPayoutVerificationSuffixSkeleton(result.eligibility_result, registeredPayoutAddressExtractionAvailable, deterministicSelectionAvailable, expectedPayoutAmountAvailable, payoutIdConstructionAvailable, payoutAlgorithmAvailable);
    return result;
}

PayoutVerificationSkeletonResult CheckPayoutVerificationSkeleton(
    const PayoutTxSkeleton& tx,
    const ValidationStateView& view,
    int currentHeight,
    int stakeMaturity,
    const PayoutBlockContextSkeleton& context,
    const PayoutAddressBlob& registeredAddress,
    const uint256& expectedStakeId,
    CAmount expectedAmount,
    CAmount vMax,
    const PayoutCoinBlob& expectedCoin)
{
    PayoutVerificationSkeletonResult result;
    result.stake_result = CheckPayoutEligibilitySkeleton(tx.selected_stake_id, view, currentHeight, stakeMaturity);
    if (result.stake_result != StakeValidationResult::OK) {
        return result;
    }

    result.public_result = CheckPayoutPublicFieldsSkeleton(tx, context, registeredAddress, expectedStakeId, expectedAmount, vMax, expectedCoin);
    return result;
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

PayoutOrdinalSelectionSkeletonResult CheckPayoutOrdinalSelectionSkeleton(const std::vector<PayoutTxSkeleton>& payout_txs, bool payoutSetAvailable, bool deterministicPayoutOrdinalRuleAvailable, bool payoutPositionValidationAvailable)
{
    if (!payoutSetAvailable) {
        return PayoutOrdinalSelectionSkeletonResult::PAYOUT_SET_UNAVAILABLE;
    }
    if (!ArePayoutIndexesDistinctSkeleton(payout_txs)) {
        return PayoutOrdinalSelectionSkeletonResult::PAYOUT_INDEXES_NOT_DISTINCT;
    }
    if (!deterministicPayoutOrdinalRuleAvailable) {
        return PayoutOrdinalSelectionSkeletonResult::DETERMINISTIC_PAYOUT_ORDINAL_RULE_UNIMPLEMENTED;
    }
    if (!payoutPositionValidationAvailable) {
        return PayoutOrdinalSelectionSkeletonResult::PAYOUT_POSITION_VALIDATION_UNIMPLEMENTED;
    }

    return PayoutOrdinalSelectionSkeletonResult::PAYOUT_ORDINAL_CONSENSUS_WIRING_UNIMPLEMENTED;
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

StakeUpdateVerificationSkeletonResult CheckStakeUpdateVerificationSkeleton(const StakeUpdateTx& tx, const ValidationStateView& view)
{
    StakeUpdateVerificationSkeletonResult result;
    result.tx_complete = IsCompleteStakeUpdateTxSkeleton(tx);
    if (!result.tx_complete) {
        return result;
    }

    result.stake_result = CheckStakeUpdateEligibilitySkeleton(tx.stake_id, view);
    return result;
}

StakeUpdateAuthorizationSkeletonResult CheckStakeUpdateAuthorizationSkeleton(const StakeUpdateVerificationSkeletonResult& prefixResult, bool updatePublicKeyExtractionAvailable, bool canonicalNewContextValidationAvailable, bool updateSignatureHashingAvailable)
{
    if (!prefixResult.tx_complete || prefixResult.stake_result != StakeValidationResult::OK) {
        return StakeUpdateAuthorizationSkeletonResult::STAKE_UPDATE_PREFIX_FAILED;
    }
    if (!updatePublicKeyExtractionAvailable) {
        return StakeUpdateAuthorizationSkeletonResult::UPDATE_PUBLIC_KEY_EXTRACTION_UNIMPLEMENTED;
    }
    if (!canonicalNewContextValidationAvailable) {
        return StakeUpdateAuthorizationSkeletonResult::CANONICAL_UPDATE_CONTEXT_UNIMPLEMENTED;
    }
    if (!updateSignatureHashingAvailable) {
        return StakeUpdateAuthorizationSkeletonResult::UPDATE_SIGNATURE_HASHING_UNIMPLEMENTED;
    }

    return StakeUpdateAuthorizationSkeletonResult::UPDATE_SIGNATURE_VERIFICATION_UNIMPLEMENTED;
}

StakeUpdateVerificationBlockedSkeletonResult CheckStakeUpdateVerificationBlockedSkeleton(const StakeUpdateTx& tx, const ValidationStateView& view, bool updatePublicKeyExtractionAvailable, bool canonicalNewContextValidationAvailable, bool updateSignatureHashingAvailable)
{
    StakeUpdateVerificationBlockedSkeletonResult result;
    result.prefix_result = CheckStakeUpdateVerificationSkeleton(tx, view);
    result.authorization_result = CheckStakeUpdateAuthorizationSkeleton(result.prefix_result, updatePublicKeyExtractionAvailable, canonicalNewContextValidationAvailable, updateSignatureHashingAvailable);
    return result;
}

StakeUpdateEffectiveHeightSkeletonResult CheckStakeUpdateEffectiveHeightSkeleton(bool stakeUpdateVerifyAccepted, bool effectiveHeightRuleAvailable)
{
    if (!stakeUpdateVerifyAccepted) {
        return StakeUpdateEffectiveHeightSkeletonResult::STAKE_UPDATE_VERIFY_NOT_ACCEPTED;
    }
    if (!effectiveHeightRuleAvailable) {
        return StakeUpdateEffectiveHeightSkeletonResult::EFFECTIVE_HEIGHT_RULE_UNIMPLEMENTED;
    }

    return StakeUpdateEffectiveHeightSkeletonResult::EFFECTIVE_HEIGHT_STATE_TRANSITION_UNIMPLEMENTED;
}

StakeUpdateSameBlockPolicySkeletonResult CheckStakeUpdateSameBlockPolicySkeleton(const std::vector<uint256>& acceptedStakeUpdateIds, bool stakeUpdatesAccepted)
{
    if (!stakeUpdatesAccepted) {
        return StakeUpdateSameBlockPolicySkeletonResult::ACCEPTED_STAKE_UPDATES_UNAVAILABLE;
    }

    std::set<uint256> seenStakeIds;
    for (const uint256& stake_id : acceptedStakeUpdateIds) {
        if (!seenStakeIds.insert(stake_id).second) {
            return StakeUpdateSameBlockPolicySkeletonResult::DUPLICATE_STAKE_UPDATE_POLICY_UNIMPLEMENTED;
        }
    }

    return StakeUpdateSameBlockPolicySkeletonResult::STAKE_UPDATE_APPLICATION_ORDER_UNIMPLEMENTED;
}

StakeValidationResult CheckStakeUpdateBlockSkeleton(const std::vector<StakeUpdateTx>& update_txs, const ValidationStateView& view)
{
    for (const StakeUpdateTx& tx : update_txs) {
        const StakeValidationResult result = CheckStakeUpdateEligibilitySkeleton(tx.stake_id, view);
        if (result != StakeValidationResult::OK) {
            return result;
        }
    }

    return StakeValidationResult::OK;
}

StakeValidationResult CheckBlockValidationPrefixSkeleton(
    const std::vector<StakeTx>& stake_txs,
    const std::vector<StakeUpdateTx>& update_txs,
    const std::vector<PayoutTxSkeleton>& payout_txs,
    const ValidationStateView& view,
    int currentHeight,
    int stakeMaturity)
{
    StakeValidationResult result = CheckStakeBlockSkeleton(stake_txs, view);
    if (result != StakeValidationResult::OK) {
        return result;
    }

    result = CheckStakeUpdateBlockSkeleton(update_txs, view);
    if (result != StakeValidationResult::OK) {
        return result;
    }

    return CheckPayoutBlockEligibilitySkeleton(payout_txs, view, currentHeight, stakeMaturity);
}

BlockValidationPrefixWithSpentTagsSkeletonResult CheckBlockValidationPrefixWithSpentTagsSkeleton(
    const std::vector<GroupElement>& sparkSpendTags,
    const std::vector<StakeTx>& stake_txs,
    const std::vector<StakeUpdateTx>& update_txs,
    const std::vector<PayoutTxSkeleton>& payout_txs,
    const ValidationStateView& parentView,
    int currentHeight,
    int stakeMaturity)
{
    BlockValidationPrefixWithSpentTagsSkeletonResult result;
    std::unordered_set<GroupElement, spark::CLTagHash> blockSpentTags;
    result.block_spent_result = BuildBlockSpentTagsSkeleton(sparkSpendTags, parentView.helsingState, blockSpentTags);
    if (result.block_spent_result != StakeValidationResult::OK) {
        return result;
    }

    ValidationStateView view(parentView);
    view.blockSpentTags = std::move(blockSpentTags);
    result.validation_result = CheckBlockValidationPrefixSkeleton(stake_txs, update_txs, payout_txs, view, currentHeight, stakeMaturity);
    return result;
}

BlockValidationSparkPrepassSkeletonResult CheckBlockValidationSparkPrepassSkeleton(bool ordinarySparkSpendValidationAvailable)
{
    if (!ordinarySparkSpendValidationAvailable) {
        return BlockValidationSparkPrepassSkeletonResult::ORDINARY_SPARK_SPEND_VALIDATION_UNIMPLEMENTED;
    }

    return BlockValidationSparkPrepassSkeletonResult::SPARK_SPEND_TAG_EXTRACTION_UNIMPLEMENTED;
}

BlockValidationBlockedSkeletonResult CheckBlockValidationBlockedSkeleton(
    const std::vector<GroupElement>& sparkSpendTags,
    const std::vector<StakeTx>& stake_txs,
    const std::vector<StakeUpdateTx>& update_txs,
    const std::vector<PayoutTxSkeleton>& payout_txs,
    const ValidationStateView& parentView,
    int currentHeight,
    int stakeMaturity)
{
    (void)sparkSpendTags;
    (void)stake_txs;
    (void)update_txs;
    (void)payout_txs;
    (void)parentView;
    (void)currentHeight;
    (void)stakeMaturity;

    BlockValidationBlockedSkeletonResult result;
    result.spark_prepass_result = CheckBlockValidationSparkPrepassSkeleton(false);
    return result;
}

PersistentBlockApplicationSkeletonResult CheckPersistentBlockApplicationSkeleton(bool acceptedBlockDataAvailable, bool helsingStateStorageAvailable, bool sparkOutputStorageAvailable, bool undoDataSerializationAvailable)
{
    if (!acceptedBlockDataAvailable) {
        return PersistentBlockApplicationSkeletonResult::ACCEPTED_BLOCK_DATA_UNAVAILABLE;
    }
    if (!helsingStateStorageAvailable) {
        return PersistentBlockApplicationSkeletonResult::HELSING_STATE_STORAGE_UNIMPLEMENTED;
    }
    if (!sparkOutputStorageAvailable) {
        return PersistentBlockApplicationSkeletonResult::SPARK_OUTPUT_STORAGE_UNIMPLEMENTED;
    }
    if (!undoDataSerializationAvailable) {
        return PersistentBlockApplicationSkeletonResult::UNDO_DATA_SERIALIZATION_UNIMPLEMENTED;
    }

    return PersistentBlockApplicationSkeletonResult::DISCONNECT_REPLAY_UNIMPLEMENTED;
}

} // namespace helsing
