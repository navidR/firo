// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FIRO_HELSING_VALIDATION_H
#define FIRO_HELSING_VALIDATION_H

#include "helsing/state.h"

#include <map>
#include <unordered_set>

namespace helsing {

enum class StakeValidationResult {
    OK,
    EMPTY_INCOINIDS,
    INCOINIDS_NOT_SORTED_DISTINCT,
    INVALID_GROUP_ELEMENT,
    MISSING_PROOF,
    TAG_ALREADY_SPENT,
    TAG_SPENT_IN_BLOCK,
    TAG_ALREADY_ACTIVE,
    OUTPUT_NOT_FOUND,
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
bool IsValidPublicPoint(const GroupElement& point);

// Structural skeleton for revised-spec StakeVerify. Proof verification is intentionally TODO.
StakeValidationResult CheckStakeSkeleton(const StakeTx& tx, const ValidationStateView& view);

} // namespace helsing

#endif // FIRO_HELSING_VALIDATION_H
