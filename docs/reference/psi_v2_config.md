# PSI v2 Configuration

## Table of Contents



- Messages
    - [DebugOptions](#debugoptions)
    - [EcdhConfig](#ecdhconfig)
    - [InternalRecoveryRecord](#internalrecoveryrecord)
    - [IoConfig](#ioconfig)
    - [KkrtConfig](#kkrtconfig)
    - [ProtocolConfig](#protocolconfig)
    - [PsiConfig](#psiconfig)
    - [RecoveryCheckpoint](#recoverycheckpoint)
    - [RecoveryConfig](#recoveryconfig)
    - [Rr22Config](#rr22config)
    - [UbPsiConfig](#ubpsiconfig)
  


- Enums
    - [IoType](#iotype)
    - [Protocol](#protocol)
    - [PsiConfig.AdvancedJoinType](#psiconfigadvancedjointype)
    - [RecoveryCheckpoint.Stage](#recoverycheckpointstage)
    - [Role](#role)
    - [UbPsiConfig.Mode](#ubpsiconfigmode)
  


- [Scalar Value Types](#scalar-value-types)



 <!-- end services -->

## Messages


### DebugOptions
Logging level for default logger.
Default to info.
Supports:

- trace: SPDLOG_LEVEL_TRACE
- debug: SPDLOG_LEVEL_DEBUG
- info: SPDLOG_LEVEL_INFO
- warn: SPDLOG_LEVEL_WARN
- err: SPDLOG_LEVEL_ERROR
- critical: SPDLOG_LEVEL_CRITICAL
- off: SPDLOG_LEVEL_OFF


| Field | Type | Description |
| ----- | ---- | ----------- |
| logging_level | [ string](#string) | none |
| trace_path | [ string](#string) | The path of trace. Deafult to /tmp/psi.trace |
 <!-- end Fields -->
 <!-- end HasFields -->


### EcdhConfig
Configs for ECDH protocol.


| Field | Type | Description |
| ----- | ---- | ----------- |
| curve | [ psi.CurveType](#psicurvetype) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### InternalRecoveryRecord



| Field | Type | Description |
| ----- | ---- | ----------- |
| stage | [ RecoveryCheckpoint.Stage](#recoverycheckpointstage) | none |
| ecdh_dual_masked_item_peer_count | [ uint64](#uint64) | none |
| parsed_bucket_count | [ uint64](#uint64) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### IoConfig
IO configuration.


| Field | Type | Description |
| ----- | ---- | ----------- |
| type | [ IoType](#iotype) | none |
| path | [ string](#string) | Required for FILE. |
 <!-- end Fields -->
 <!-- end HasFields -->


### KkrtConfig
Configs for KKRT protocol


| Field | Type | Description |
| ----- | ---- | ----------- |
| bucket_size | [ uint64](#uint64) | Since the total input may not fit in memory, the input may be splitted into buckets. bucket_size indicate the number of items in each bucket. If the memory of host is limited, you should set a smaller bucket size. Otherwise, you should use a larger one. If not set, use default value: 1 << 20. |
 <!-- end Fields -->
 <!-- end HasFields -->


### ProtocolConfig
Any items related to PSI protocols.


| Field | Type | Description |
| ----- | ---- | ----------- |
| protocol | [ Protocol](#protocol) | none |
| role | [ Role](#role) | none |
| broadcast_result | [ bool](#bool) | Reveal result to sender. |
| ecdh_config | [ EcdhConfig](#ecdhconfig) | For ECDH protocol. |
| kkrt_config | [ KkrtConfig](#kkrtconfig) | For KKRT protocol. |
| rr22_config | [ Rr22Config](#rr22config) | For RR22 protocol. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PsiConfig
The top level of Configs.
run(PsiConfig)->PsiReport

Advanced Joins
Type: Inner Join
e.g. If input of receiver is
```
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
```
and input of sender is
```
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| z    | c     |
```

After inner join.
The output of receiver is:
```
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
```
The output of sender is
```
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
```

Type: Left Join
After left join.
The output of left side is:
```
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
```
The output of right side is
```
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| n/a  | n/a   |
```

Type: Right Join
After right join.
The output of left side is:
```
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
| n/a  | n/a   |
```
The output of right side is
```
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| z    | c     |
```

Type: Full Join
After full join.
The output of left side is:
```
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
| n/a  | n/a   |
```
The output of right side is
```
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| n/a  | n/a   |
| z    | c     |
```

Type: Difference
After difference.
The output of left side is:
```
| key1 | value1|
|------|-------|
| y    | 4     |
| n/a  | n/a   |
```
The output of right side is
```
| key2 | value2|
|------|-------|
| n/a  | n/a   |
| z    | c     |
```


| Field | Type | Description |
| ----- | ---- | ----------- |
| protocol_config | [ ProtocolConfig](#protocolconfig) | Configs for protocols. |
| input_config | [ IoConfig](#ioconfig) | Configs for input. |
| output_config | [ IoConfig](#ioconfig) | Configs for output. |
| keys | [repeated string](#string) | keys for intersection. |
| debug_options | [ DebugOptions](#debugoptions) | Logging level. |
| skip_duplicates_check | [ bool](#bool) | If true, the check of duplicated items will be skiped. |
| disable_alignment | [ bool](#bool) | It true, output is not promised to be aligned. |
| recovery_config | [ RecoveryConfig](#recoveryconfig) | Configs for recovery. |
| advanced_join_type | [ PsiConfig.AdvancedJoinType](#psiconfigadvancedjointype) | none |
| left_side | [ Role](#role) | Required if advanced_join_type is ADVANCED_JOIN_TYPE_LEFT_JOIN or ADVANCED_JOIN_TYPE_RIGHT_JOIN. |
| check_hash_digest | [ bool](#bool) | Check if hash digest of keys from parties are equal to determine whether to early-stop. |
 <!-- end Fields -->
 <!-- end HasFields -->


### RecoveryCheckpoint
Save some critical information for future recovery.


| Field | Type | Description |
| ----- | ---- | ----------- |
| stage | [ RecoveryCheckpoint.Stage](#recoverycheckpointstage) | Stage of PSI. |
| config | [ PsiConfig](#psiconfig) | A copy of origin PSI config. |
| input_hash_digest | [ string](#string) | Hash digest of input keys. |
| ecdh_dual_masked_item_self_count | [ uint64](#uint64) | Saved dual masked item count from self originally. PROTOCOL_ECDH only. |
| ecdh_dual_masked_item_peer_count | [ uint64](#uint64) | Saved dual masked item count from peer originally. PROTOCOL_ECDH only. |
| parsed_bucket_count | [ uint64](#uint64) | Saved parsed bucket count. PROTOCOL_KKRT and PROTOCOL_RR22 only. |
 <!-- end Fields -->
 <!-- end HasFields -->


### RecoveryConfig
Configuration for recovery.
If a PSI task failed unexpectedly, e.g. network failures and restart, the
task can resume to the latest checkpoint to save time.
However, enabling recovery would due in extra disk IOs and disk space
occupation.


| Field | Type | Description |
| ----- | ---- | ----------- |
| enabled | [ bool](#bool) | none |
| folder | [ string](#string) | Stores status and checkpoint files. |
 <!-- end Fields -->
 <!-- end HasFields -->


### Rr22Config
Configs for RR22 protocol.


| Field | Type | Description |
| ----- | ---- | ----------- |
| bucket_size | [ uint64](#uint64) | Since the total input may not fit in memory, the input may be splitted into buckets. bucket_size indicate the number of items in each bucket. If the memory of host is limited, you should set a smaller bucket size. Otherwise, you should use a larger one. If not set, use default value: 1 << 20. |
| low_comm_mode | [ bool](#bool) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### UbPsiConfig
config for unbalanced psi.


| Field | Type | Description |
| ----- | ---- | ----------- |
| mode | [ UbPsiConfig.Mode](#ubpsiconfigmode) | Required. |
| role | [ Role](#role) | Required for all modes except MODE_OFFLINE_GEN_CACHE. |
| input_config | [ IoConfig](#ioconfig) | Config for origin input. Servers: Required for MODE_OFFLINE_GEN_CACHE, MODE_OFFLINE, MODE_FULL. Clients: Required for MODE_ONLINE and MODE_FULL. |
| keys | [repeated string](#string) | Join keys. Servers: Required for MODE_OFFLINE_GEN_CACHE, MODE_OFFLINE, MODE_FULL. Clients: Required for MODE_ONLINE and MODE_FULL. |
| server_secret_key_path | [ string](#string) | Servers: Required for MODE_OFFLINE_GEN_CACHE, MODE_OFFLINE, MODE_ONLINE and MODE_FULL. |
| cache_path | [ string](#string) | Required. |
| server_get_result | [ bool](#bool) | none |
| client_get_result | [ bool](#bool) | none |
| disable_alignment | [ bool](#bool) | It true, output is not promised to be aligned. Valid if both server_get_result and client_get_result are true. |
| output_config | [ IoConfig](#ioconfig) | Required for MODE_ONLINE and MODE_FULL. |
| debug_options | [ DebugOptions](#debugoptions) | Logging level. |
 <!-- end Fields -->
 <!-- end HasFields -->
 <!-- end messages -->

## Enums


### IoType
TODO(junfeng): support more io types including oss, sql, etc.

| Name | Number | Description |
| ---- | ------ | ----------- |
| IO_TYPE_UNSPECIFIED | 0 | none |
| IO_TYPE_FILE_CSV | 1 | Local csv file. |




### Protocol
PSI protocols.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PROTOCOL_UNSPECIFIED | 0 | none |
| PROTOCOL_ECDH | 1 | [Mea86]C. Meadows, "A More Efficient Cryptographic Matchmaking Protocol for Use in the Absence of a Continuously Available Third Party," 1986 IEEE Symposium on Security and Privacy, Oakland, CA, USA, 1986, pp. 134-134, doi: 10.1109/SP.1986.10022. |
| PROTOCOL_KKRT | 2 | Efficient Batched Oblivious PRF with Applications to Private Set Intersection https://eprint.iacr.org/2016/799.pdf |
| PROTOCOL_RR22 | 3 | Blazing Fast PSI https://eprint.iacr.org/2022/320.pdf |




### PsiConfig.AdvancedJoinType
Advanced Join allow duplicate keys.
- If selected, duplicates_check is skipped.
- If selected, both parties are allowed to contain duplicate keys.
- If use left join, full join or difference, the size of difference set of
left party is revealed to right party.
- If use right join, full join or difference, the size of difference set of
right party is revealed to left party.

| Name | Number | Description |
| ---- | ------ | ----------- |
| ADVANCED_JOIN_TYPE_UNSPECIFIED | 0 | none |
| ADVANCED_JOIN_TYPE_INNER_JOIN | 1 | none |
| ADVANCED_JOIN_TYPE_LEFT_JOIN | 2 | none |
| ADVANCED_JOIN_TYPE_RIGHT_JOIN | 3 | none |
| ADVANCED_JOIN_TYPE_FULL_JOIN | 4 | none |
| ADVANCED_JOIN_TYPE_DIFFERENCE | 5 | none |




### RecoveryCheckpoint.Stage


| Name | Number | Description |
| ---- | ------ | ----------- |
| STAGE_UNSPECIFIED | 0 | none |
| STAGE_INIT_END | 1 | none |
| STAGE_PRE_PROCESS_END | 2 | none |
| STAGE_ONLINE_START | 3 | none |
| STAGE_ONLINE_END | 4 | none |
| STAGE_POST_PROCESS_END | 5 | none |




### Role
Role of parties.

| Name | Number | Description |
| ---- | ------ | ----------- |
| ROLE_UNSPECIFIED | 0 | none |
| ROLE_RECEIVER | 1 | receiver In 2P symmetric PSI, receivers would always receive the result in the origin protocol. |
| ROLE_SENDER | 2 | sender In 2P symmetric PSI, senders are the other participants apart from receiver. |
| ROLE_SERVER | 3 | server In 2P unbalanced PSI, servers own a much larger dataset. |
| ROLE_CLIENT | 4 | server In 2P unbalanced PSI, clients own a much smaller dataset. |




### UbPsiConfig.Mode


| Name | Number | Description |
| ---- | ------ | ----------- |
| MODE_UNSPECIFIED | 0 | none |
| MODE_OFFLINE_GEN_CACHE | 1 | Servers generate cache only. First part of offline stage. |
| MODE_OFFLINE_TRANSFER_CACHE | 2 | Servers send cache to clients only. Second part of offline stage. |
| MODE_OFFLINE | 3 | Run offline stage. |
| MODE_ONLINE | 4 | Run online stage. |
| MODE_FULL | 5 | Run all stages. |


 <!-- end Enums -->
 <!-- end Files -->

## Scalar Value Types

| .proto Type | Notes | C++ Type | Java Type | Python Type |
| ----------- | ----- | -------- | --------- | ----------- |
| <div><h4 id="double" /></div><a name="double" /> double |  | double | double | float |
| <div><h4 id="float" /></div><a name="float" /> float |  | float | float | float |
| <div><h4 id="int32" /></div><a name="int32" /> int32 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint32 instead. | int32 | int | int |
| <div><h4 id="int64" /></div><a name="int64" /> int64 | Uses variable-length encoding. Inefficient for encoding negative numbers – if your field is likely to have negative values, use sint64 instead. | int64 | long | int/long |
| <div><h4 id="uint32" /></div><a name="uint32" /> uint32 | Uses variable-length encoding. | uint32 | int | int/long |
| <div><h4 id="uint64" /></div><a name="uint64" /> uint64 | Uses variable-length encoding. | uint64 | long | int/long |
| <div><h4 id="sint32" /></div><a name="sint32" /> sint32 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int32s. | int32 | int | int |
| <div><h4 id="sint64" /></div><a name="sint64" /> sint64 | Uses variable-length encoding. Signed int value. These more efficiently encode negative numbers than regular int64s. | int64 | long | int/long |
| <div><h4 id="fixed32" /></div><a name="fixed32" /> fixed32 | Always four bytes. More efficient than uint32 if values are often greater than 2^28. | uint32 | int | int |
| <div><h4 id="fixed64" /></div><a name="fixed64" /> fixed64 | Always eight bytes. More efficient than uint64 if values are often greater than 2^56. | uint64 | long | int/long |
| <div><h4 id="sfixed32" /></div><a name="sfixed32" /> sfixed32 | Always four bytes. | int32 | int | int |
| <div><h4 id="sfixed64" /></div><a name="sfixed64" /> sfixed64 | Always eight bytes. | int64 | long | int/long |
| <div><h4 id="bool" /></div><a name="bool" /> bool |  | bool | boolean | boolean |
| <div><h4 id="string" /></div><a name="string" /> string | A string must always contain UTF-8 encoded or 7-bit ASCII text. | string | String | str/unicode |
| <div><h4 id="bytes" /></div><a name="bytes" /> bytes | May contain any arbitrary sequence of bytes. | string | ByteString | str |

