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


import random
import csv
import argparse


def gen_test_data(
    reciver_item_cnt: int,
    sender_item_cnt: int,
    intersection_cnt: int,
    id_cnt: int,
    receiver_path: str,
    sender_path: str,
    intersection_path: str,
    seed: int,
) -> None:
    assert reciver_item_cnt > 0 and sender_item_cnt > 0 and intersection_cnt > 0
    assert reciver_item_cnt >= intersection_cnt and sender_item_cnt >= intersection_cnt

    receiver_minus_intersection_cnt = reciver_item_cnt - intersection_cnt
    total_cnt = reciver_item_cnt + sender_item_cnt - intersection_cnt

    ids = [list(range(0, total_cnt)) for _ in range(id_cnt)]

    random.seed(seed)
    random.shuffle(ids)

    for e in ids:
        random.shuffle(e)

    r = 0
    s = 0
    i = 0

    with open(receiver_path, 'w') as receiver_file, open(
        sender_path, 'w'
    ) as sender_file, open(intersection_path, 'w') as intersection_file:
        fieldnames = [f'id{i}' for i in range(id_cnt)]
        receiver_writer = csv.writer(receiver_file)
        receiver_writer.writerow(fieldnames)

        sender_writer = csv.writer(sender_file)
        sender_writer.writerow(fieldnames)

        intersection_writer = csv.writer(intersection_file)
        intersection_writer.writerow(fieldnames)

        for x in range(total_cnt):
            rows = [ids[idx][x] for idx in range(id_cnt)]
            if x < receiver_minus_intersection_cnt:
                receiver_writer.writerow(rows)
                r += 1
            elif x < reciver_item_cnt:
                receiver_writer.writerow(rows)
                sender_writer.writerow(rows)
                intersection_writer.writerow(rows)
                r += 1
                s += 1
                i += 1
            else:
                sender_writer.writerow(rows)
                s += 1

    assert r == reciver_item_cnt and s == sender_item_cnt and i == intersection_cnt

    print('## Test data generated:')
    print(f'reciver_item_cnt = {reciver_item_cnt} at {receiver_path}.')
    print(f'sender_item_cnt = {sender_item_cnt} at {sender_path}.')
    print(f'intersection_cnt = {intersection_cnt} at {intersection_path}.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='psi test data generator.')
    parser.add_argument(
        '--reciver_item_cnt', help='Item cnt for receiver.', default='1e4'
    )
    parser.add_argument('--sender_item_cnt', help='Item cnt for sender.', default='1e4')
    parser.add_argument(
        '--intersection_cnt', help='Item cnt for intersection.', default='8e3'
    )
    parser.add_argument('--id_cnt', help='Id col cnt.', default=1, type=int)
    parser.add_argument(
        '--receiver_path', help='Receiver path.', default="receiver.csv"
    )
    parser.add_argument('--sender_path', help='Sender path.', default="sender.csv")
    parser.add_argument(
        '--intersection_path', help='Intersection path.', default="intersection.csv"
    )
    parser.add_argument('--seed', help='Random seed.', default=0, type=int)

    args = parser.parse_args()

    gen_test_data(
        reciver_item_cnt=int(float(args.reciver_item_cnt)),
        sender_item_cnt=int(float(args.sender_item_cnt)),
        intersection_cnt=int(float(args.intersection_cnt)),
        id_cnt=args.id_cnt,
        receiver_path=args.receiver_path,
        sender_path=args.sender_path,
        intersection_path=args.intersection_path,
        seed=args.seed,
    )
