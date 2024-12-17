# Launch Configuration

Please check psi.BucketPsiConfig at **PSI v1 Configuration**.
Please check psi.v2.PsiConfig and psi.v2.UbPsiConfig at **PSI v2 Configuration**.

## Table of Contents



- Messages
    - [LaunchConfig](#launchconfig)
  





- Messages
    - [ContextDescProto](#contextdescproto)
    - [PartyProto](#partyproto)
    - [RetryOptionsProto](#retryoptionsproto)
    - [SSLOptionsProto](#ssloptionsproto)
  



- [Scalar Value Types](#scalar-value-types)



 <!-- end services -->

## Messages


### LaunchConfig



| Field | Type | Description |
| ----- | ---- | ----------- |
| link_config | [ yacl.link.ContextDescProto](#yacllinkcontextdescproto) | Configs for network. |
| self_link_party | [ string](#string) | With link_config. |
| [**oneof**](https://developers.google.com/protocol-buffers/docs/proto3#oneof) runtime_config.legacy_psi_config | [ BucketPsiConfig](#bucketpsiconfig) | Please check at psi.proto. |
| [**oneof**](https://developers.google.com/protocol-buffers/docs/proto3#oneof) runtime_config.psi_config | [ v2.PsiConfig](#v2psiconfig) | Please check at psi_v2.proto. |
| [**oneof**](https://developers.google.com/protocol-buffers/docs/proto3#oneof) runtime_config.ub_psi_config | [ v2.UbPsiConfig](#v2ubpsiconfig) | Please check at psi_v2.proto. |
| [**oneof**](https://developers.google.com/protocol-buffers/docs/proto3#oneof) runtime_config.apsi_sender_config | [ ApsiSenderConfig](#apsisenderconfig) | Please check at pir.proto. |
| [**oneof**](https://developers.google.com/protocol-buffers/docs/proto3#oneof) runtime_config.apsi_receiver_config | [ ApsiReceiverConfig](#apsireceiverconfig) | none |
 <!-- end Fields -->
 <!-- end HasFields -->
 <!-- end messages -->

## Enums
 <!-- end Enums -->


 <!-- end services -->

## Messages


### ContextDescProto
Configuration for link config.

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


| Field | Type | Description |
| ----- | ---- | ----------- |
| id | [ string](#string) | the UUID of this communication. optional |
| parties | [repeated PartyProto](#partyproto) | party description, describes the world. |
| connect_retry_times | [ uint32](#uint32) | connect to mesh retry time. |
| connect_retry_interval_ms | [ uint32](#uint32) | connect to mesh retry interval. |
| recv_timeout_ms | [ uint64](#uint64) | recv timeout in milliseconds. so for long time work(that one party may wait for the others for very long time), this value should be changed accordingly. |
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

