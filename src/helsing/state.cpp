// Copyright (c) 2026 The Firo Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "helsing/state.h"

namespace helsing {
namespace {

bool IsUsableTag(const GroupElement& tag)
{
    return tag.isMember() && !tag.isInfinity();
}

} // namespace

void CHelsingState::Reset()
{
    spentTags.clear();
    activeTags.clear();
    stakeRecords.clear();
}

bool CHelsingState::IsSpentTag(const GroupElement& tag) const
{
    return spentTags.count(tag) != 0;
}

bool CHelsingState::IsActiveTag(const GroupElement& tag) const
{
    return activeTags.count(tag) != 0;
}

bool CHelsingState::GetActiveStakeId(const GroupElement& tag, uint256& stake_id) const
{
    auto it = activeTags.find(tag);
    if (it == activeTags.end()) {
        return false;
    }

    stake_id = it->second;
    return true;
}

const StakeRecord* CHelsingState::GetStakeRecord(const uint256& stake_id) const
{
    auto it = stakeRecords.find(stake_id);
    if (it == stakeRecords.end()) {
        return nullptr;
    }

    return &it->second;
}

bool CHelsingState::AddActiveStake(const StakeRecord& record)
{
    if (record.stake_id.IsNull() || !IsUsableTag(record.T) || IsSpentTag(record.T) || IsActiveTag(record.T)) {
        return false;
    }
    if (stakeRecords.count(record.stake_id) != 0) {
        return false;
    }

    StakeRecord activeRecord = record;
    activeRecord.status = StakeStatus::ACTIVE;
    activeRecord.nSpentHeight = -1;

    activeTags.emplace(activeRecord.T, activeRecord.stake_id);
    stakeRecords.emplace(activeRecord.stake_id, activeRecord);
    return true;
}

bool CHelsingState::AddSpentTag(const GroupElement& tag, int nHeight)
{
    if (!IsUsableTag(tag)) {
        return false;
    }

    const bool inserted = spentTags.insert(tag).second;

    auto activeIt = activeTags.find(tag);
    if (activeIt != activeTags.end()) {
        auto recordIt = stakeRecords.find(activeIt->second);
        if (recordIt != stakeRecords.end()) {
            recordIt->second.status = StakeStatus::SPENT;
            recordIt->second.nSpentHeight = nHeight;
        }
        activeTags.erase(activeIt);
    }

    return inserted;
}

bool CHelsingState::RevokeStake(const uint256& stake_id)
{
    auto recordIt = stakeRecords.find(stake_id);
    if (recordIt == stakeRecords.end() || recordIt->second.status != StakeStatus::ACTIVE) {
        return false;
    }

    activeTags.erase(recordIt->second.T);
    recordIt->second.status = StakeStatus::REVOKED;
    return true;
}

} // namespace helsing
