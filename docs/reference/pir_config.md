# PIR Configuration

## Table of Contents



- Messages
    - [ApsiReceiverConfig](#apsireceiverconfig)
    - [ApsiSenderConfig](#apsisenderconfig)
    - [PirResultReport](#pirresultreport)
  



- [Scalar Value Types](#scalar-value-types)



 <!-- end services -->

## Messages


### ApsiReceiverConfig



| Field | Type | Description |
| ----- | ---- | ----------- |
| threads | [ uint32](#uint32) | Number of threads to use |
| log_file | [ string](#string) | Log file path. For APSI only. |
| silent | [ bool](#bool) | Do not write output to console. For APSI only. |
| log_level | [ string](#string) | One of 'all', 'debug', 'info' (default), 'warning', 'error', 'off'. For APSI only. |
| query_file | [ string](#string) | Path to a text file containing query data (one per line). Header is not needed. |
| output_file | [ string](#string) | Path to a file where intersection result will be written. |
| params_file | [ string](#string) | Path to a JSON file describing the parameters to be used by the sender. If not set, receiver will ask sender, which results in additional communication. |
| experimental_enable_bucketize | [ bool](#bool) | Must be same as sender config. |
| experimental_bucket_cnt | [ uint32](#uint32) | Must be same as sender config. |
 <!-- end Fields -->
 <!-- end HasFields -->


### ApsiSenderConfig
NOTE(junfeng): We provide a config identical to original APSI CLI.
Please check
https://github.com/microsoft/APSI?tab=readme-ov-file#command-line-interface-cli
for details.


| Field | Type | Description |
| ----- | ---- | ----------- |
| threads | [ uint32](#uint32) | Number of threads to use |
| log_file | [ string](#string) | Log file path. For APSI only. |
| silent | [ bool](#bool) | Do not write output to console. For APSI only. |
| log_level | [ string](#string) | One of 'all', 'debug', 'info' (default), 'warning', 'error', 'off'. For APSI only. |
| db_file | [ string](#string) | Path to a CSV file describing the sender's dataset (an item-label pair on each row) or a file containing a serialized SenderDB; the CLI will first attempt to load the data as a serialized SenderDB, and – upon failure – will proceed to attempt to read it as a CSV file For CSV File: 1. the first col is processed as item while the second col as label. OTHER COLS ARE IGNORED. 2. NO HEADERS ARE ALLOWED. |
| params_file | [ string](#string) | Path to a JSON file describing the parameters to be used by the sender. Not required if db_file points to a serialized SenderDB. |
| sdb_out_file | [ string](#string) | Save the SenderDB in the given file. Required if gen_db_only is set true. Use experimental_bucket_folder instead if you turn experimental_enable_bucketize on. |
| nonce_byte_count | [ uint32](#uint32) | Number of bytes used for the nonce in labeled mode (default is 16) |
| compress | [ bool](#bool) | Whether to compress the SenderDB in memory; this will make the memory footprint smaller at the cost of increased computation. |
| save_db_only | [ bool](#bool) | Whether to save sender db only. |
| experimental_enable_bucketize | [ bool](#bool) | [experimental] Whether to split data in buckets and Each bucket would be a seperate SenderDB. If set, experimental_bucket_folder must be a valid folder. |
| experimental_bucket_cnt | [ uint32](#uint32) | [experimental] The number of bucket to fit data. |
| experimental_bucket_folder | [ string](#string) | [experimental] Folder to save bucketized small csv files and db files. |
 <!-- end Fields -->
 <!-- end HasFields -->


### PirResultReport
The report of pir task.


| Field | Type | Description |
| ----- | ---- | ----------- |
| match_cnt | [ int64](#int64) | none |
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

