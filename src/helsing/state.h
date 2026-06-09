// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef FIRO_HELSING_STATE_H
#define FIRO_HELSING_STATE_H

#include "helsing/types.h"

#include <map>
#include <unordered_map>
#include <unordered_set>

namespace helsing {

class CHelsingState {
public:
    void Reset();

    bool IsSpentTag(const GroupElement& tag) const;
    bool IsActiveTag(const GroupElement& tag) const;
    bool GetActiveStakeId(const GroupElement& tag, uint256& stake_id) const;
    const StakeRecord* GetStakeRecord(const uint256& stake_id) const;

    bool AddActiveStake(const StakeRecord& record);
    bool AddAcceptedStake(const uint256& stake_id, const StakeTx& tx, int nHeight);
    bool ApplyAcceptedStakeUpdateSkeleton(const uint256& stake_id, const StakeContext& m_new, int nHeight);
    bool ApplyBlockSpentTagsSkeleton(const std::unordered_set<GroupElement, spark::CLTagHash>& blockSpentTags, int nHeight);
    bool AddSpentTag(const GroupElement& tag, int nHeight);
    bool RevokeStake(const uint256& stake_id);

    size_t GetSpentTagCount() const { return spentTags.size(); }
    size_t GetActiveTagCount() const { return activeTags.size(); }
    size_t GetStakeRecordCount() const { return stakeRecords.size(); }

private:
    std::unordered_set<GroupElement, spark::CLTagHash> spentTags;
    std::unordered_map<GroupElement, uint256, spark::CLTagHash> activeTags;
    std::map<uint256, StakeRecord> stakeRecords;
};

} // namespace helsing

#endif // FIRO_HELSING_STATE_H
