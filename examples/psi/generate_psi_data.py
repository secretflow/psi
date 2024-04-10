# Copyright 2023 Ant Group Co., Ltd.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


import argparse
import csv
import uuid

import pyarrow as pa
import pyarrow.csv as csv

batch_size = 10000


def create_table(id_cnt: int, label_cnt: int):
    assert id_cnt >= 0 and label_cnt >= 0
    names = [f"id_{i}" for i in range(id_cnt)] + [
        f"label_{i}" for i in range(label_cnt)
    ]

    arrays = [
        pa.array([uuid.uuid4().hex], type=pa.string())
        for _ in range(id_cnt + label_cnt)
    ]

    return pa.table(arrays, names=names)


def write_table(
    id_cnt: int,
    label_cnt: int,
    intersection_cnt: int,
    difference_cnt: int,
    output_path: str,
    difference_id_prefix: str,
):
    assert (
        id_cnt >= 0 and label_cnt >= 0 and intersection_cnt > 0 and difference_cnt >= 0
    )

    if difference_cnt:
        assert len(difference_id_prefix) > 0

    table = create_table(id_cnt, label_cnt)

    with csv.CSVWriter(output_path, table.schema) as table_writer:
        cnt = 0
        while cnt < intersection_cnt:
            table_row = min(batch_size, intersection_cnt - cnt)

            arrays = []
            for idx in range(id_cnt):
                arrays.append(
                    pa.array([f"i_{idx}_{i}" for i in range(cnt, cnt + table_row)])
                )

            for idx in range(label_cnt):
                arrays.append(
                    pa.array(
                        [uuid.uuid4().hex for _ in range(cnt, cnt + table_row)],
                        type=pa.string(),
                    )
                )

            t = pa.table(arrays, names=table.schema.names)

            table_writer.write_table(t)

            cnt += table_row

        cnt = 0
        while cnt < difference_cnt:
            table_row = min(batch_size, difference_cnt - cnt)

            arrays = []
            for idx in range(id_cnt):
                arrays.append(
                    pa.array(
                        [
                            f"{difference_id_prefix}_{idx}_{i}"
                            for i in range(cnt, cnt + table_row)
                        ]
                    )
                )

            for idx in range(label_cnt):
                arrays.append(
                    pa.array(
                        [uuid.uuid4().hex for _ in range(cnt, cnt + table_row)],
                        type=pa.string(),
                    )
                )

            t = pa.table(arrays, names=table.schema.names)

            table_writer.write_table(t)

            cnt += table_row


def gen_test_data(
    receiver_item_cnt: int,
    sender_item_cnt: int,
    intersection_cnt: int,
    id_cnt: int,
    receiver_label_cnt: int,
    sender_label_cnt: int,
    receiver_path: str,
    sender_path: str,
    intersection_path: str,
    seed: int,
) -> None:
    assert (
        receiver_item_cnt > 0
        and sender_item_cnt > 0
        and intersection_cnt > 0
        and receiver_label_cnt >= 0
        and sender_label_cnt >= 0
    )

    assert receiver_item_cnt >= intersection_cnt and sender_item_cnt >= intersection_cnt

    write_table(id_cnt, 0, intersection_cnt, 0, intersection_path, "")
    write_table(
        id_cnt,
        receiver_label_cnt,
        intersection_cnt,
        receiver_item_cnt - intersection_cnt,
        receiver_path,
        "r",
    )
    write_table(
        id_cnt,
        sender_label_cnt,
        intersection_cnt,
        sender_item_cnt - intersection_cnt,
        sender_path,
        "s",
    )

    print("## Test data generated:")
    print(f"receiver_item_cnt = {receiver_item_cnt} at {receiver_path}.")
    print(f"sender_item_cnt = {sender_item_cnt} at {sender_path}.")
    print(f"intersection_cnt = {intersection_cnt} at {intersection_path}.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="psi test data generator.")
    parser.add_argument(
        "--receiver_item_cnt", help="Item cnt for receiver.", default="1e4"
    )
    parser.add_argument("--sender_item_cnt", help="Item cnt for sender.", default="1e4")
    parser.add_argument(
        "--intersection_cnt", help="Item cnt for intersection.", default="8e3"
    )
    parser.add_argument("--id_cnt", help="Id col cnt.", default=1, type=int)
    parser.add_argument("--receiver_label_cnt", help="Id col cnt.", default=0, type=int)
    parser.add_argument("--sender_label_cnt", help="Id col cnt.", default=0, type=int)
    parser.add_argument(
        "--receiver_path", help="Receiver path.", default="receiver.csv"
    )
    parser.add_argument("--sender_path", help="Sender path.", default="sender.csv")
    parser.add_argument(
        "--intersection_path", help="Intersection path.", default="intersection.csv"
    )
    parser.add_argument("--seed", help="Random seed.", default=0, type=int)

    args = parser.parse_args()

    gen_test_data(
        receiver_item_cnt=int(float(args.receiver_item_cnt)),
        sender_item_cnt=int(float(args.sender_item_cnt)),
        intersection_cnt=int(float(args.intersection_cnt)),
        id_cnt=args.id_cnt,
        receiver_label_cnt=args.receiver_label_cnt,
        sender_label_cnt=args.sender_label_cnt,
        receiver_path=args.receiver_path,
        sender_path=args.sender_path,
        intersection_path=args.intersection_path,
        seed=args.seed,
    )
