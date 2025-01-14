# RELEASE

> NOTE:
>
> - `[Feature]` prefix for new features.
> - `[Bugfix]` prefix for bug fixes.
> - `[API]` prefix for API changes.
> - `[Improvement]` prefix for implementation improvement.

## v0.5.0.dev241107

- `[API]` delete legacy ub psi function.
- `[API]` output csv null_rep can be specified.
- `[Feature]` join can be done with one receiver.
- `[Feature]` join can be done with ub psi protocol.

## v0.4.0.dev240801

- [Bugfix] Fix MacOS and arm build.

## v0.4.0.dev240731

- [Improvement] Port APSI again.
- [Feature] Add Bucketized APSI.
- [API] Remove SealPIR.

## v0.4.0.dev240521

- [API] remove BC22 protocol

## v0.4.0.dev240517

- [Improvement] upgrade yacl to 0.4.5b0.

## v0.4.0.dev240514

- [API] add entrypoint for docker file.
- [API] allow passing config JSON directly to main.
- [Bugfix] fix ic mode.
- [Bugfix] fix RR22, SealPIR and APSI.

## v0.4.0.dev240401

- [Improvement] upgrade download uri of xz.

## v0.4.0.dev240329

- [Improvement] upgrade yacl to 0.4.4b3.

## v0.3.0beta

- [Improvement] add uuid in system temp folder.
- [Improvement] use arrow csv reader in pir.
- [Bugfix] fix typo in psi config check.

## v0.3.0.dev240304

- [API] expose ic_mode in RunLegacyPsi api

## v0.3.0.dev240222

- [API] expose PIR API.

## v0.3.0.dev240219

- [Feature] add ecdh logger for debug purposes.
- [API] modify repo structure.

## v0.2.0.dev240123

- [Feature] add RFC9380 25519 elligator2 hash_to_curve.
- [Feature] add malicious vole psi.
- [API] expose ub psi in PSI v2 API.
- [Improvement] Modify buffer size in sort cmd.
- [Bugfix] Fix SimpleShuffledBatchProvider.
- [Bugfix] Fix flakiness in psi_test.
- [Bugfix] Fix race condition in rr22.

## v0.2.0.dev231228

- [Bugfix] Fix RR22 race condition.
- [Improvement] modify sort buffer size.

## v0.2.0.dev231221

- [API] Rename check_duplicates to skip_duplicates_check.
- [API] Rename sort_output to disable_alignment.
- [Feature] Support left join, right join and full join. The behavior of difference is modified.
- [Feature] Skip duplicate key check if recovery checkpoint exists.
- [Bugfix] Fix duplicate key check.
- [Bugfix] Fix SyncWait.

## v0.1.0beta

- [API] Add PSI v2 API.
- [Feature] Add RR22 protocol.
- [Feature] Support recovery from failure in v2 API.
- [Feature] Support inner join in v2 API.
- [Feature] Migrate ECDH, KKRT, RR22 protocol in v2 API.
