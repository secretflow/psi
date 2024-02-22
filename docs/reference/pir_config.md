# PIR Configuration

## Table of Contents



- Messages
    - [ApsiServerConfig](#apsiserverconfig)
    - [PirClientConfig](#pirclientconfig)
    - [PirConfig](#pirconfig)
    - [PirResultReport](#pirresultreport)
    - [PirServerConfig](#pirserverconfig)



- Enums
    - [PirConfig.Mode](#pirconfigmode)
    - [PirProtocol](#pirprotocol)



- [Scalar Value Types](#scalar-value-types)



 <!-- end services -->

## Messages


### ApsiServerConfig
Server config for APSI protocol.


| Field | Type | Description |
| ----- | ---- | ----------- |
| oprf_key_path | [ string](#string) | The path of oprf_key file path. This field is not required for MODE_SERVER_FULL. |
| num_per_query | [ uint32](#uint32) | The number of per query. |
| compressed | [ bool](#bool) | compressed Seal ciphertext |
| max_items_per_bin | [ uint32](#uint32) | max items per bin, i.e. Interpolate polynomial max degree. optional. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PirClientConfig
Client configs.


| Field | Type | Description |
| ----- | ---- | ----------- |
| input_path | [ string](#string) | The input csv file path of pir. |
| key_columns | [repeated string](#string) | The key columns name of input data. |
| output_path | [ string](#string) | The path of query output csv file path. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PirConfig
The config for PIR. This is the entrypoint for all PIR tasks.


| Field | Type | Description |
| ----- | ---- | ----------- |
| mode | [ PirConfig.Mode](#pirconfigmode) | none |
| pir_protocol | [ PirProtocol](#pirprotocol) | The PIR protocol. |
| pir_server_config | [ PirServerConfig](#pirserverconfig) | Required for MODE_SERVER_SETUP, MODE_SERVER_ONLINE and MODE_SERVER_FULL. |
| pir_client_config | [ PirClientConfig](#pirclientconfig) | Required for MODE_CLIENT. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PirResultReport
The report of pir result.


| Field | Type | Description |
| ----- | ---- | ----------- |
| data_count | [ int64](#int64) | The data count of input/query. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PirServerConfig
Server configs.
setup_path is only required field for MODE_SERVER_ONLINE.
setup_path is not required for MODE_SERVER_FULL.


| Field | Type | Description |
| ----- | ---- | ----------- |
| input_path | [ string](#string) | The input csv file path. |
| setup_path | [ string](#string) | The path of setup output path. |
| key_columns | [repeated string](#string) | The key columns name of input data. |
| label_columns | [repeated string](#string) | The label columns name of input data. |
| label_max_len | [ uint32](#uint32) | The max number bytes of label. |
| bucket_size | [ uint32](#uint32) | split data bucket to do pir query |
| apsi_server_config | [ ApsiServerConfig](#apsiserverconfig) | For APSI protocol only |
 <!-- end Fields -->
 <!-- end HasFields -->
 <!-- end messages -->

## Enums


### PirConfig.Mode


| Name | Number | Description |
| ---- | ------ | ----------- |
| MODE_UNSPECIFIED | 0 | none |
| MODE_SERVER_SETUP | 1 | Server with setup stage. |
| MODE_SERVER_ONLINE | 2 | Server with online stage. |
| MODE_SERVER_FULL | 3 | Server with both online and offline stages. |
| MODE_CLIENT | 4 | Client |




### PirProtocol
The algorithm type of pir.

| Name | Number | Description |
| ---- | ------ | ----------- |
| PIR_PROTOCOL_UNSPECIFIED | 0 | none |
| PIR_PROTOCOL_KEYWORD_PIR_APSI | 1 | Keyword PIR APSI Reference: https://github.com/microsoft/APSI |


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
