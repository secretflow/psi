# Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT license.

import argparse
import random
import string

ap = argparse.ArgumentParser()
ap.add_argument(
    "-s", "--sender_size", help="The size of the sender's set", type=int, default=100
)
ap.add_argument(
    "-r", "--receiver_size", help="The size of the receiver's set", type=int, default=1
)
ap.add_argument(
    "-i",
    "--intersection_size",
    help="The desired size of the intersection",
    type=int,
    default=1,
)
ap.add_argument(
    "--db_file",
    nargs="?",
    help="The file to write the sender's set to",
    default="db.csv",
)
ap.add_argument(
    "--query_file",
    nargs="?",
    help="The file to write the sender's set to",
    default="query.csv",
)
ap.add_argument(
    "--intersection_file",
    nargs="?",
    help="The file to write the sender's set to",
    default="ground_truth.csv",
)
ap.add_argument(
    "--label_byte_count",
    nargs="?",
    help="The number of bytes used for the labels",
    type=int,
    default=0,
)
ap.add_argument(
    "--item_byte_count",
    nargs="?",
    help="The number of bytes used for the items",
    type=int,
    default=64,
)
args = ap.parse_args()

sender_sz = args.sender_size
recv_sz = args.receiver_size
int_sz = args.intersection_size
label_bc = args.label_byte_count
item_bc = args.item_byte_count

sender_list = []
letters = string.ascii_lowercase + string.ascii_uppercase

send_size = 0
recv_size = 0

import time

start = time.time()

with open(args.db_file, "w") as sender_file:
    sender_file.write("key" + (("," + "value") if label_bc != 0 else "") + "\n")
    with open(args.query_file, "w") as query_file:
        query_file.write("key\n")
        with open(args.intersection_file, "w") as truth_file:
            truth_file.write("key" + (("," + "value") if label_bc != 0 else "") + "\n")
            while send_size < sender_sz:
                item = "".join(random.choice(letters) for i in range(item_bc))
                label = "".join(random.choice(letters) for i in range(label_bc))
                sender_file.write(
                    item + (("," + label) if label_bc != 0 else "") + "\n"
                )
                send_size += 1
                if send_size % 100000 == 0:
                    print(
                        time.strftime("%Y%0m%0e %0H:%0M:%0S")
                        + ": Wrote "
                        + str(send_size)
                        + " items : "
                        + f"{send_size/sender_sz*100:.2f}%, "
                        + f" rest: {(time.time() - start) / send_size * sender_sz * (1 - send_size / sender_sz): .2f}s",
                        flush=True,
                    )
                if recv_size < int_sz:
                    query_file.write(item + "\n")
                    truth_file.write(
                        item + (("," + label) if label_bc != 0 else "") + "\n"
                    )
                    recv_size += 1
            while recv_size < recv_sz:
                query_file.write(
                    "".join(random.choice(letters) for i in range(item_bc)) + "\n"
                )
                recv_size += 1
            print(f"Wrote intersection set: {args.intersection_file}")
        print(f"Wrote query's set: {args.query_file}")
    print(f"Wrote sender's set: {args.db_file}")

"""
python examples/pir/apsi/test_data_creator.py -s=1000000 -r=1 -i=1  --db_file=db_1M.csv --query_file=query_1.csv --intersection_file=ground_truth_1.csv --label_byte_count=16
python examples/pir/apsi/test_data_creator.py -s=10000000 -r=1 -i=1  --db_file=db_10M.csv --query_file=query_1.csv --intersection_file=ground_truth_1.csv --label_byte_count=16
python examples/pir/apsi/test_data_creator.py -s=100000000 -r=1 -i=1  --db_file=db_100M.csv --query_file=query_1.csv --intersection_file=ground_truth_1.csv --label_byte_count=16
"""
