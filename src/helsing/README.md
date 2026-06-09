# Helsing Skeleton

This directory is a consensus-inert implementation skeleton for Helsing private masternode staking.
It follows `research/Helsing - Private Masternode Staking - Revised Spec.pdf` as the primary model and keeps the earlier Helsing note as background.

Current scope:

- `OutputId` models the revised spec identity `output_id = (txid, vout)`.
- `StakeTx` models `HelsingStakeTx = {InCoinIDs, S_prime, C_prime, T, m, Pi_par, Pi_val, Pi_tag}`.
- `SparkOutputRecord` models the validator view of Spark outputs used by `InCoinIDs`.
- `CHelsingState` models `SpentTags`, `ActiveTags`, and `StakeRecords`.
- `CheckStakeSkeleton` performs structural checks only: sorted distinct `InCoinIDs`, non-infinity public group elements, non-empty proof blobs, tag conflicts, output existence, output record consistency, and `helsing_eligible`.

Not implemented yet:

- extraction of `SparkOutputs[(txid, vout)]` from real Spark mint/spend outputs
- canonical `stake_stmt`, `incoins_root`, and context hashing
- final `StakeVerify` check ordering matching the revised spec once value parameters and context validation are wired
- cover-set cardinality from public Helsing parameters
- Spark maturity and cover-set eligibility rules using current height and consensus maturity parameters
- value-domain checks for `V_STAKE`, `V_MAX`, scalar conversion bounds, and fees outside the collateral proof
- `ParVerify`, `RepVerify`, and `TagVerify`
- block-level `BlockSpentTags` integration
- block-level duplicate new stake tag integration
- canonical stake context grammar, including payout address, update key, node signing material, and rejection of empty or non-canonical contexts
- masternode registration/update/payout transaction wiring
- consensus activation rules

The important implementation decision left open by the revised spec is how to migrate Spark output identity from the current serialized `spark::Coin` state key toward `(txid, vout)` without changing existing Spark behavior accidentally.
