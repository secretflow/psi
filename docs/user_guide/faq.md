# Frequently Asked Questions (FAQ)

We will collect some popular questions from users and update this part promptly.

## Config Issues

1. In PSI config, what is difference of **broadcast_result** and **receiver**? Is it safe to turn on **broadcast_result**?

In PSI protocols, the parties who are promised to receive the intersection are called **receiver**s, the other parties are called **sender**s.
When **broadcast_result** is turn on, **sender**s also receive the intersection. Both parties must agree on the value of **broadcast_result**, otherwise the program will stop.

If **broadcast_result** is turn on, only **receiver**s and **sender**s could receive the result while any third parties could not see. So it is safe to set **broadcast_result** to true, if both **receiver**s and **sender**s wish to get the result.

2. What is **IO_TYPE_UNSPECIFIED**?

You must select a type as IoType. **IO_TYPE_UNSPECIFIED** is the default value of **IoType**, which is meaningless. At this moment, we only support **IO_TYPE_FILE_CSV**.

3. What is **ADVANCED_JOIN_TYPE_UNSPECIFIED**?

PSI protocols doesn‘t allow duplicates in ids of inputs. However, sometimes we may intend to have duplicates in ids and perform LEFT / RIGHT / FULL join following rules of SQL. This is called **AdvancedJoinType**.

**ADVANCED_JOIN_TYPE_UNSPECIFIED** is default value of AdvancedJoinType, which means default implementation of PSI configs and duplicates is disallowed. If ids of inputs contains duplicates at this moment, the behavior is undefined.

4. What is the recommendation value of bucket size?

The default value is 2^20. You shouldn't set this value unless you have very limited computation resource.

5. What is **disable_alignment**?

If **disable_alignment** turns on, the intersection received by **receiver**s and **sender**s are not promised to be aligned(the order doesn't match) and save time.

If any **AdvancedJoinType** is specified, aligement is promised due to implementation, **disable_alignment** is ignored.

6. What is **RetryOptionsProto** in **ContextDescProto**?

We have proper default values for all fields. You shouldn't set any values unless the network is pretty bad.

## Feature Issues

1. How to enable SSL?

We support mTLS and you should provide proper **ContextDescProto**:

- **enable_ssl** is enabled.
- In **client_ssl_opts**, set **verify_depth** and provide peer CA file with **ca_file_path**
- In **server_ssl_opts**, provide self certificate and private key file with **certificate_path** and **private_key_path**
- You must provide these settings at both sides.

```
{
    "psi_config": {},
    "link_config": {
        "parties": [
            {
                "id": "receiver",
                "host": "127.0.0.1:5300"
            },
            {
                "id": "sender",
                "host": "127.0.0.1:5400"
            }
        ],
        "enable_ssl": true,
        "client_ssl_opts": {
            "verify_depth": 1,
            "ca_file_path": "/path/to/peer/CA/file"
        },
        "server_ssl_opts": {
            "certificate_path": "/path/to/self/certificate/file",
            "private_key_path": "/path/to/self/private/key/file"
        }
    },
    "self_link_party": "sender"
}
```

2. How to use recovery?

We provide recovery feature in PSI v2.

You have to provide a proper **RecoveryConfig**:

- **enabled** set to true.
- **folder** is provided to store checkpoints.

If a PSI task fails, just restart the task with the same config, the progress will resume.

3. What is **Easy PSI**? Why and when to use **Easy PSI**?

[Easy PSI](https://www.secretflow.org.cn/docs/quickstart/easy-psi) is a standalone PSI product powered by this library. It provides a simple User Interface and utilize [Kuscia](https://www.secretflow.org.cn/docs/kuscia) to launch PSI binaries between both parties.
