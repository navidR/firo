# Helsing Skeleton

This directory is a consensus-inert implementation skeleton for Helsing private masternode staking.
It follows `research/Helsing - Private Masternode Staking - Revised Spec.pdf` as the primary model and keeps the earlier Helsing note as background.

Current scope:

- `OutputId` models the revised spec identity `output_id = (txid, vout)`.
- `StakeTx` models `HelsingStakeTx = {InCoinIDs, S_prime, C_prime, T, m, Pi_par, Pi_val, Pi_tag}`.
- `StakeUpdateTx` models the revised spec fields `{stake_id, m_new, sig_update}` as a byte-carrying data structure only.
- `PayoutTxSkeleton` models the revised spec fields `{selected_stake_id, payout_index, addr_pk, V_PAYOUT, Coin}` as a byte-carrying data structure only.
- `PayoutBlockContextSkeleton` models the revised spec section 16 payout-id input fields `{chain_id, block_height, prev_block_hash, payout_index, selected_stake_id}` without defining or computing `j`.
- `IsCompletePayoutBlockContextSkeleton` checks that caller-supplied section 16 payout-id input fields are populated before any future `j` construction; it does not define canonical `chain_id` or `enc_*` grammar.
- `DoesPayoutContextMatchTxSkeleton` checks that caller-supplied payout tx and block-context skeletons agree on `payout_index` and `selected_stake_id`; it does not construct `j`.
- `ArePayoutIdInputsCompleteSkeleton` checks only that section 16 payout-id inputs are available across the block context and tx identity fields before any future `j` construction.
- `IsCompleteStakeTxSkeleton` checks that caller-supplied `StakeTx` cover-set, context, and proof fields are populated before future staking verification; it does not define context grammar or verify proofs.
- `IsCompleteStakeUpdateTxSkeleton` checks that caller-supplied `StakeUpdateTx` fields are populated before any future signature verification; it does not define context grammar or verify `sig_update`.
- `SparkOutputRecord` models the validator view of Spark outputs used by `InCoinIDs`.
- `ExtractSparkOutputRecords` builds a consensus-inert `(txid, vout) -> SparkOutputRecord` view from valid Spark mint and spend-created output scripts.
- `IsHelsingEligibleOutputCandidateSkeleton` checks the revised spec section 13 rule that outputs with any non-tag-revealing spend path must not be marked `helsing_eligible`; the caller supplies spend-path analysis.
- `MarkHelsingEligibleOutputCandidatesSkeleton` marks caller-selected in-memory `SparkOutputs` entries as `helsing_eligible` only after the section 13 candidate predicate passes.
- `ApplyAcceptedPayoutOutputRecordsSkeleton` applies already accepted payout output records to `SparkOutputs` by `(txid, vout)` only; it does not recompute payout coins or decide Helsing eligibility.
- `ApplyAcceptedBlockWithPayoutOutputsSkeleton` applies revised spec section 12 steps 6-9 to in-memory Helsing state and `SparkOutputs` atomically for already accepted block data.
- `IsValidCoverSetCardinality` checks the revised spec formula `len(InCoinIDs) = N = n^m` for caller-supplied public parameters.
- `IsStakeMatureForPayout` checks the revised spec payout maturity inequality for caller-supplied heights and `STAKE_MATURITY`.
- `IsHelsingValueInRange` checks the revised spec integer-domain bound `0 <= value < V_MAX` for caller-supplied values.
- `AreHelsingValueParametersInRangeSkeleton` checks revised spec section 20 integer-domain bounds for caller-supplied `V_STAKE`, `V_PAYOUT`, and `V_MAX`, plus a caller-supplied `V_MAX < q` result; it does not expose the scalar order or perform scalar conversion.
- `IsStakeValueParameterInRangeSkeleton` checks revised spec `StakeVerify` step 3 for caller-supplied `V_STAKE`, `V_MAX`, and `V_MAX < q`; it does not expose the scalar order, perform scalar conversion, or verify the collateral proof.
- `CheckStakeVerificationPrefixSkeleton` checks revised spec `StakeVerify` steps 1-3 only: field presence, a caller-supplied canonical-encoding result, and caller-supplied `V_STAKE` value parameters. It deliberately stops before tag-state, context, cover-set, statement-hash, and proof verification.
- `IsExpectedPayoutAmountInRangeSkeleton` checks the revised spec `PayoutVerify` step 10 equality and integer range for caller-supplied values.
- `IsPayoutValueParameterInRangeSkeleton` checks revised spec `PayoutVerify` step 10 for caller-supplied `V_PAYOUT`, expected amount, `V_MAX`, and `V_MAX < q`; it does not expose the scalar order, perform scalar conversion, or recompute payout amounts.
- `IsCompletePayoutTxSkeleton` checks that caller-supplied `PayoutTxSkeleton` identity/address/coin fields are populated before future payout verification; it does not define payout address or coin encodings.
- `DoesPayoutAddressMatchRegisteredSkeleton` checks revised spec `PayoutVerify` step 8 byte equality against a caller-supplied, already extracted registered payout address.
- `DoesPayoutStakeMatchExpectedSkeleton` checks revised spec `PayoutVerify` step 9 equality against a caller-supplied, already recomputed deterministic selected stake.
- `DoesPayoutCoinMatchExpectedSkeleton` checks revised spec `PayoutVerify` step 13 byte equality against a caller-supplied, already recomputed payout coin.
- `CheckPayoutPublicFieldsSkeleton` composes caller-supplied payout public-field checks in revised spec `PayoutVerify` step 8, 9, 10, 11, and 13 order; it does not compute deterministic selection, `j`, or payout coins.
- `IsHelsingStakeValueWithMarginInRangeSkeleton` checks the revised spec optional collateral-margin integer sum rule for caller-supplied `V_STAKE`, `margin`, and `V_MAX`.
- `CHelsingState` models `SpentTags`, `ActiveTags`, and `StakeRecords`.
- `BuildBlockSpentTagsSkeleton` models the revised spec block pre-pass minimum rule for duplicate Spark spend tags and tags already in `SpentTags`; it assumes ordinary Spark spend validation supplied valid revealed tags.
- `CheckCoverSetOutputsSkeleton` performs revised spec cover-set output checks for caller-supplied public parameters: `N = n^m`, sorted distinct output identifiers, output lookup, output record consistency, and `helsing_eligible`.
- `CheckStakeSkeleton` performs structural checks only: sorted distinct `InCoinIDs`, non-infinity public group elements, non-empty proof blobs, tag conflicts, output existence, output record consistency, and `helsing_eligible`.
- `CheckStakeBlockSkeleton` performs non-mutating block-level tag-state checks before per-stake skeleton validation, including duplicate new stake tags in the same block.
- `CheckPayoutEligibilitySkeleton` performs revised spec `PayoutVerify` steps 3-7 only: stake record lookup, active status, spent-tag checks, and payout maturity.
- `CheckPayoutBlockEligibilitySkeleton` performs revised spec `ValidateBlock` step 5 and `PayoutVerify` steps 3-7 only for caller-supplied payout transactions; it deliberately stops before payout index policy, address extraction, deterministic selection, payout amount derivation, payout identifier construction, and payout coin recomputation.
- `CheckPayoutVerificationSkeleton` composes revised spec `PayoutVerify` steps 3-13 using existing eligibility checks and caller-supplied expected public values; it deliberately does not parse contexts, select masternodes, compute `j`, run `Payout`, or verify payout coins.
- `ArePayoutIndexesDistinctSkeleton` checks the revised spec section 16 duplicate-`payout_index` rule for a caller-supplied payout set.
- `CheckStakeUpdateEligibilitySkeleton` performs revised spec `StakeUpdateVerify` steps 1-3 only: stake record lookup, active status, and spent-tag checks.
- `CheckStakeUpdateVerificationSkeleton` composes `StakeUpdateTx` field presence with revised spec `StakeUpdateVerify` steps 1-3; it deliberately stops before context parsing, `update_pk` extraction, `enc_context(m_new)`, signature verification, and update effective-height rules.
- `CheckStakeUpdateBlockSkeleton` performs revised spec `ValidateBlock` step 4 and `StakeUpdateVerify` steps 1-3 only for a caller-supplied update set; it deliberately stops before context parsing, signature verification, effective-height rules, and same-block duplicate-update policy.
- `CheckBlockValidationPrefixSkeleton` composes the existing stake, stake-update, and payout skeleton prefix checks in revised spec `ValidateBlock` steps 3-5 order; the caller must supply already collected `BlockSpentTags`.
- `ApplyAcceptedStakesSkeleton` applies already accepted new stakes to in-memory `ActiveTags` and `StakeRecords`; it takes caller-supplied `stake_id` values because canonical stake-id hashing is not implemented.
- `ApplyAcceptedStakeUpdateSkeleton` applies an already accepted update to `StakeRecords` by changing only `m` and `last_update_height`; it does not validate contexts or signatures.
- `ApplyAcceptedStakeUpdatesSkeleton` applies already accepted stake updates as an all-or-nothing batch; duplicate updates for the same `stake_id` are rejected by the unwired skeleton because the revised spec does not define same-block duplicate-update ordering.
- `ApplyBlockSpentTagsSkeleton` applies an already validated `BlockSpentTags` set to in-memory `SpentTags`, `ActiveTags`, and matching `StakeRecords`.
- `ApplyAcceptedBlockSkeleton` applies revised spec section 12 steps 6-8 to already accepted block data in order: block spent tags, accepted new stakes, then accepted stake updates. It is in-memory only and stops before accepted payout outputs.

Not implemented yet:

- canonical `stake_stmt`, `incoins_root`, and context hashing
- canonical `stake_id = H("Helsing/stake-id/v1" || canonical(tx))` construction
- final `StakeVerify` check ordering matching the revised spec once value parameters and context validation are wired
- wiring cover-set output checks into `StakeVerify` after public Helsing parameters are selected
- Spark maturity and cover-set eligibility rules using current height and consensus maturity parameters
- wiring value-domain checks for `V_STAKE`, `V_PAYOUT`, `V_MAX < q`, scalar conversion bounds, and ordinary fees outside the collateral proof
- any decision to reintroduce an optional collateral margin as a consensus policy
- deriving the expected payout amount from consensus reward rules
- `ParVerify`, `RepVerify`, and `TagVerify`
- consensus block-level `BlockSpentTags` extraction/integration
- real Spark transaction-type spend-path analysis for deciding when every spend path reveals a Spark tag
- consensus wiring, persistence, and undo data for marking `SparkOutputs` as `helsing_eligible`
- persistent block undo data for Helsing state mutations
- consensus block-level duplicate new stake tag integration
- consensus wiring for applying accepted new stakes during deterministic block application
- full payout verification, including registered payout address extraction, stake selection, payout amount, payout identifier, and deterministic Spark payout coin comparison
- payout-output extraction from accepted payout transactions after `PayoutVerify`
- deterministic payout ordinal selection and payout identifier `j` construction
- canonical `chain_id`, `enc_int`, `enc_hash`, and `enc_bytes` encodings for payout-id construction
- real payout address and payout coin encodings for `PayoutTxSkeleton`
- canonical stake context grammar, including payout address, update key, node signing material, and rejection of empty or non-canonical contexts
- full stake update verification, including `update_pk` extraction, canonical `m_new` validation, `enc_context(m_new)`, canonical `sig_update`, update signature verification, and update effective-height rules
- consensus wiring for applying accepted stake updates during deterministic block application
- consensus policy for multiple `StakeUpdate` transactions targeting the same `stake_id` in one block
- masternode registration/update/payout transaction wiring
- consensus activation rules

The important implementation decision left open by the revised spec is how to migrate Spark output identity from the current serialized `spark::Coin` state key toward `(txid, vout)` without changing existing Spark behavior accidentally.
