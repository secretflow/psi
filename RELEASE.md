> NOTE:
>
> - `[Feature]` prefix for new features.
> - `[Bugfix]` prefix for bug fixes.
> - `[API]` prefix for API changes.
> - `[Improvement]` prefix for implementation improvement.

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
