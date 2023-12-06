# PSI v2 Configuration

## Table of Contents



- Messages
    - [DebugOptions](#debugoptions)
    - [EcdhConfig](#ecdhconfig)
    - [InnerJoinConfig](#innerjoinconfig)
    - [InputConfig](#inputconfig)
    - [InternalRecoveryRecord](#internalrecoveryrecord)
    - [KkrtConfig](#kkrtconfig)
    - [OutputConfig](#outputconfig)
    - [ProtocolConfig](#protocolconfig)
    - [PsiConfig](#psiconfig)
    - [PsiReport](#psireport)
    - [RecoveryCheckpoint](#recoverycheckpoint)
    - [RecoveryConfig](#recoveryconfig)
    - [Rr22Config](#rr22config)
    - [Table](#table)
    - [Table.Row](#tablerow)



- Enums
    - [IoType](#iotype)
    - [Protocol](#protocol)
    - [PsiConfig.AdvancedJoinType](#psiconfigadvancedjointype)
    - [RecoveryCheckpoint.Stage](#recoverycheckpointstage)
    - [Role](#role)





- Messages
    - [ContextDescProto](#contextdescproto)
    - [PartyProto](#partyproto)
    - [RetryOptionsProto](#retryoptionsproto)
    - [SSLOptionsProto](#ssloptionsproto)




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
| curve | [ psi.psi.CurveType](#psipsicurvetype) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### InnerJoinConfig
Internal usage only at this moment.


| Field | Type | Description |
| ----- | ---- | ----------- |
| input_path | [ string](#string) | Path of origin input. |
| role | [ Role](#role) | The role of party, |
| keys | [repeated string](#string) | Keys for PSI. |
| sorted_input_path | [ string](#string) | Path of sorted input depending on keys. |
| unique_input_keys_cnt_path | [ string](#string) | Path of unique keys and cnt |
| self_intersection_cnt_path | [ string](#string) | Path of PSI output with unique_input_keys_cnt_path as input. |
| peer_intersection_cnt_path | [ string](#string) | Path of received peer intersection count. |
| output_path | [ string](#string) | The path of output for inner join. |
 <!-- end Fields -->
 <!-- end HasFields -->


### InputConfig
Input configuration.


| Field | Type | Description |
| ----- | ---- | ----------- |
| type | [ IoType](#iotype) | none |
| path | [ string](#string) | Required for FILE. |
| raw | [ Table](#table) | Required for RAW. |
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


### KkrtConfig
Configs for KKRT protocol


| Field | Type | Description |
| ----- | ---- | ----------- |
| bucket_size | [ uint64](#uint64) | Since the total input may not fit in memory, the input may be splitted into buckets. bucket_size indicate the number of items in each bucket. If the memory of host is limited, you should set a smaller bucket size. Otherwise, you should use a larger one. If not set, use default value: 1 << 20. |
 <!-- end Fields -->
 <!-- end HasFields -->


### OutputConfig
Output configuration.


| Field | Type | Description |
| ----- | ---- | ----------- |
| input_type_followed | [ bool](#bool) | If true, type of output would be the same as input type. And type would be ngelected. |
| type | [ IoType](#iotype) | none |
| path | [ string](#string) | Required for FILE. |
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


| Field | Type | Description |
| ----- | ---- | ----------- |
| protocol_config | [ ProtocolConfig](#protocolconfig) | Configs for protocols. |
| input_config | [ InputConfig](#inputconfig) | Configs for input. |
| output_config | [ OutputConfig](#outputconfig) | Configs for output. |
| link_config | [ yacl.link.ContextDescProto](#yacllinkcontextdescproto) | Configs for network. |
| self_link_party | [ string](#string) | none |
| keys | [repeated string](#string) | keys for intersection. |
| debug_options | [ DebugOptions](#debugoptions) | Logging level. |
| check_duplicates | [ bool](#bool) | If true, a precheck of duplicated items will be conducted. An early exception would be throw before PSI. |
| output_difference | [ bool](#bool) | It true, output defference instead of intersection. |
| sort_output | [ bool](#bool) | It true, output is sorted according to keys in InputConfig. |
| recovery_config | [ RecoveryConfig](#recoveryconfig) | Configs for recovery. |
| advanced_join_type | [ PsiConfig.AdvancedJoinType](#psiconfigadvancedjointype) | none |
| left_side | [ Role](#role) | Required if advanced_join_type is ADVANCED_JOIN_TYPE_INNER_JOIN. |
| check_hash_digest | [ bool](#bool) | Check if hash digest of keys from parties are equal to determine whether to early-stop. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PsiReport
Execution Report.


| Field | Type | Description |
| ----- | ---- | ----------- |
| original_count | [ int64](#int64) | The data count of input. |
| intersection_count | [ int64](#int64) | The count of intersection. Get `-1` when self party can not get result. |
| output | [ Table](#table) | Maybe used if output type is RAW. |
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


### Table
Stores input or output data.
For IoType::IO_TYPE_MEM_RAW.


| Field | Type | Description |
| ----- | ---- | ----------- |
| header | [ Table.Row](#tablerow) | none |
| data | [repeated Table.Row](#tablerow) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### Table.Row



| Field | Type | Description |
| ----- | ---- | ----------- |
| values | [repeated string](#string) | none |
 <!-- end Fields -->
 <!-- end HasFields -->
 <!-- end messages -->

## Enums


### IoType


| Name | Number | Description |
| ---- | ------ | ----------- |
| IO_TYPE_UNSPECIFIED | 0 | none |
| IO_TYPE_FILE_CSV | 1 | Local csv file. |
| IO_TYPE_MEM_RAW | 2 | With Table pb msg. |




### Protocol
PSI protocols.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PROTOCOL_UNSPECIFIED | 0 | none |
| PROTOCOL_ECDH | 1 | [Mea86]C. Meadows, "A More Efficient Cryptographic Matchmaking Protocol for Use in the Absence of a Continuously Available Third Party," 1986 IEEE Symposium on Security and Privacy, Oakland, CA, USA, 1986, pp. 134-134, doi: 10.1109/SP.1986.10022. |
| PROTOCOL_KKRT | 2 | Efficient Batched Oblivious PRF with Applications to Private Set Intersection https://eprint.iacr.org/2016/799.pdf |
| PROTOCOL_RR22 | 3 | Blazing Fast PSI https://eprint.iacr.org/2022/320.pdf |




### PsiConfig.AdvancedJoinType
Advanced modes which allow duplicate keys.
Advanced mode: Inner Join
If selected, check_duplicates is invalid.
If selected, both parties could have duplicate keys.
```
e.g. If input of receiver is
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
and input of sender is
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| z    | c     |

After inner join.
The ourput of receiver is:
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
The output of sender is
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
| x    | a     |
| x    | b     |
```

Advanced mode: Left Join
If selected, check_duplicates is invalid.
If selected, both parties could have duplicate keys.
```
e.g. If input of left side is
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
and input of right side is
| key2 | value2|
|------|-------|
| x    | a     |
| x    | b     |
| z    | c     |

After inner join.
The ourput of left side is:
| key1 | value1|
|------|-------|
| x    | 1     |
| x    | 2     |
| x    | 3     |
| x    | 1     |
| x    | 2     |
| x    | 3     |
| y    | 4     |
The output of right side is
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

| Name | Number | Description |
| ---- | ------ | ----------- |
| ADVANCED_JOIN_TYPE_UNSPECIFIED | 0 | none |
| ADVANCED_JOIN_TYPE_INNER_JOIN | 1 | none |
| ADVANCED_JOIN_TYPE_LEFT_JOIN | 2 | none |




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
| ROLE_RECEIVER | 1 | receiver In 2P symmetric PSI, receiver would always receive the result. |
| ROLE_SENDER | 2 | sender In 2P symmetric PSI, sender is the other participant apart from receiver. |


 <!-- end Enums -->


 <!-- end services -->

## Messages


### ContextDescProto
Configuration for link config.

NOTE for 'recv time'

'recv time' is the max time that a party will wait for a given event.
for example:
```

     begin recv                 end recv
|--------|-------recv-time----------|------------------| alice's timeline

                        begin send     end send
|-----busy-work-------------|-------------|------------| bob's timeline

```
in above case, when alice begins recv for a specific event, bob is still
busy doing its job, when alice's wait time exceed wait_timeout_ms, it raise
exception, although bob now is starting to send data.

so for long time work(that one party may wait for the others for very long
time), this value should be changed accordingly.


| Field | Type | Description |
| ----- | ---- | ----------- |
| id | [ string](#string) | the UUID of this communication. optional |
| parties | [repeated PartyProto](#partyproto) | party description, describes the world. |
| connect_retry_times | [ uint32](#uint32) | connect to mesh retry time. |
| connect_retry_interval_ms | [ uint32](#uint32) | connect to mesh retry interval. |
| recv_timeout_ms | [ uint64](#uint64) | recv timeout in milliseconds. |
| http_max_payload_size | [ uint32](#uint32) | http max payload size, if a single http request size is greater than this limit, it will be unpacked into small chunks then reassembled. This field does affect performance. Please choose wisely. |
| http_timeout_ms | [ uint32](#uint32) | a single http request timetout. |
| throttle_window_size | [ uint32](#uint32) | throttle window size for channel. if there are more than limited size messages are flying, `SendAsync` will block until messages are processed or throw exception after wait for `recv_timeout_ms` |
| brpc_channel_protocol | [ string](#string) | BRPC client channel protocol. |
| brpc_channel_connection_type | [ string](#string) | BRPC client channel connection type. |
| enable_ssl | [ bool](#bool) | ssl options for link channel. |
| client_ssl_opts | [ SSLOptionsProto](#ssloptionsproto) | ssl configs for channel this config is ignored if enable_ssl == false; |
| server_ssl_opts | [ SSLOptionsProto](#ssloptionsproto) | ssl configs for service this config is ignored if enable_ssl == false; |
| chunk_parallel_send_size | [ uint32](#uint32) | chunk parallel send size for channel. if need chunked send when send message, the max paralleled send size is chunk_parallel_send_size |
| retry_opts | [ RetryOptionsProto](#retryoptionsproto) | retry options |
 <!-- end Fields -->
 <!-- end HasFields -->


### PartyProto



| Field | Type | Description |
| ----- | ---- | ----------- |
| id | [ string](#string) | none |
| host | [ string](#string) | none |
 <!-- end Fields -->
 <!-- end HasFields -->


### RetryOptionsProto
Retry options.


| Field | Type | Description |
| ----- | ---- | ----------- |
| max_retry | [ uint32](#uint32) | max retry count default 3 |
| retry_interval_ms | [ uint32](#uint32) | time between retries at first retry default 1 second |
| retry_interval_incr_ms | [ uint32](#uint32) | The amount of time to increase the interval between retries default 2s |
| max_retry_interval_ms | [ uint32](#uint32) | The maximum interval between retries default 10s |
| error_codes | [repeated uint32](#uint32) | retry on these brpc error codes, if empty, retry on all codes |
| http_codes | [repeated uint32](#uint32) | retry on these http codes, if empty, retry on all http codes |
| aggressive_retry | [ bool](#bool) | do aggressive retry， this means that retries will be made on additional error codes |
 <!-- end Fields -->
 <!-- end HasFields -->


### SSLOptionsProto
SSL options.


| Field | Type | Description |
| ----- | ---- | ----------- |
| certificate_path | [ string](#string) | Certificate file path |
| private_key_path | [ string](#string) | Private key file path |
| verify_depth | [ int32](#int32) | Set the maximum depth of the certificate chain for verification If 0, turn off the verification |
| ca_file_path | [ string](#string) | Set the trusted CA file to verify the peer's certificate If empty, use the system default CA files |
 <!-- end Fields -->
 <!-- end HasFields -->
 <!-- end messages -->

## Enums
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
