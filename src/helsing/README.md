# Helsing Skeleton

This directory is a consensus-inert implementation skeleton for Helsing private masternode staking.
It follows `research/Helsing - Private Masternode Staking - Revised Spec.pdf` as the primary model and keeps the earlier Helsing note as background.

Current scope:

- `OutputId` models the revised spec identity `output_id = (txid, vout)`.
- `StakeTx` models `HelsingStakeTx = {InCoinIDs, S_prime, C_prime, T, m, Pi_par, Pi_val, Pi_tag}`.
- `StakeUpdateTx` models the revised spec fields `{stake_id, m_new, sig_update}` as a byte-carrying data structure only.
- `PayoutTxSkeleton` models the revised spec fields `{selected_stake_id, payout_index, addr_pk, V_PAYOUT, Coin}` as a byte-carrying data structure only.
- `SparkOutputRecord` models the validator view of Spark outputs used by `InCoinIDs`.
- `ExtractSparkOutputRecords` builds a consensus-inert `(txid, vout) -> SparkOutputRecord` view from valid Spark mint and spend-created output scripts.
- `IsValidCoverSetCardinality` checks the revised spec formula `len(InCoinIDs) = N = n^m` for caller-supplied public parameters.
- `IsStakeMatureForPayout` checks the revised spec payout maturity inequality for caller-supplied heights and `STAKE_MATURITY`.
- `IsHelsingValueInRange` checks the revised spec integer-domain bound `0 <= value < V_MAX` for caller-supplied values.
- `IsExpectedPayoutAmountInRangeSkeleton` checks the revised spec `PayoutVerify` step 10 equality and integer range for caller-supplied values.
- `IsHelsingStakeValueWithMarginInRangeSkeleton` checks the revised spec optional collateral-margin integer sum rule for caller-supplied `V_STAKE`, `margin`, and `V_MAX`.
- `CHelsingState` models `SpentTags`, `ActiveTags`, and `StakeRecords`.
- `BuildBlockSpentTagsSkeleton` models the revised spec block pre-pass minimum rule for duplicate Spark spend tags and tags already in `SpentTags`; it assumes ordinary Spark spend validation supplied valid revealed tags.
- `CheckCoverSetOutputsSkeleton` performs revised spec cover-set output checks for caller-supplied public parameters: `N = n^m`, sorted distinct output identifiers, output lookup, output record consistency, and `helsing_eligible`.
- `CheckStakeSkeleton` performs structural checks only: sorted distinct `InCoinIDs`, non-infinity public group elements, non-empty proof blobs, tag conflicts, output existence, output record consistency, and `helsing_eligible`.
- `CheckStakeBlockSkeleton` performs non-mutating block-level tag-state checks before per-stake skeleton validation, including duplicate new stake tags in the same block.
- `CheckPayoutEligibilitySkeleton` performs revised spec `PayoutVerify` steps 3-7 only: stake record lookup, active status, spent-tag checks, and payout maturity.
- `ArePayoutIndexesDistinctSkeleton` checks the revised spec section 16 duplicate-`payout_index` rule for a caller-supplied payout set.
- `CheckStakeUpdateEligibilitySkeleton` performs revised spec `StakeUpdateVerify` steps 1-3 only: stake record lookup, active status, and spent-tag checks.
- `ApplyAcceptedStakeUpdateSkeleton` applies an already accepted update to `StakeRecords` by changing only `m` and `last_update_height`; it does not validate contexts or signatures.

Not implemented yet:

- canonical `stake_stmt`, `incoins_root`, and context hashing
- final `StakeVerify` check ordering matching the revised spec once value parameters and context validation are wired
- wiring cover-set output checks into `StakeVerify` after public Helsing parameters are selected
- Spark maturity and cover-set eligibility rules using current height and consensus maturity parameters
- wiring value-domain checks for `V_STAKE`, `V_PAYOUT`, `V_MAX < q`, scalar conversion bounds, and ordinary fees outside the collateral proof
- any decision to reintroduce an optional collateral margin as a consensus policy
- deriving the expected payout amount from consensus reward rules
- `ParVerify`, `RepVerify`, and `TagVerify`
- consensus block-level `BlockSpentTags` extraction/integration
- consensus block-level duplicate new stake tag integration
- full payout verification, including registered payout address extraction, stake selection, payout amount, payout identifier, and deterministic Spark payout coin comparison
- deterministic payout ordinal selection and payout identifier `j` construction
- real payout address and payout coin encodings for `PayoutTxSkeleton`
- canonical stake context grammar, including payout address, update key, node signing material, and rejection of empty or non-canonical contexts
- full stake update verification, including `update_pk` extraction, canonical `m_new` validation, `enc_context(m_new)`, canonical `sig_update`, update signature verification, and update effective-height rules
- consensus wiring for applying accepted stake updates during deterministic block application
- masternode registration/update/payout transaction wiring
- consensus activation rules

The important implementation decision left open by the revised spec is how to migrate Spark output identity from the current serialized `spark::Coin` state key toward `(txid, vout)` without changing existing Spark behavior accidentally.
