# Data

The files are generated with `examples/pir/apsi/test_data_creator.py` for testing purposes.

1. `labeled_db.csv` and `query_to_labeled_db.csv` are generated with

    ```bash
    python test_data_creator.py --sender_size=1000 --receiver_size=10 --intersection_size=10 --label_byte_count=32 --item_byte_count=32
    ```

2. `db.csv` and `query\.csv` are generated with

    ```bash
    python test_data_creator.py --sender_size=1000 --receiver_size=10 --intersection_size=10 --label_byte_count=0 --item_byte_count=32
    ```
