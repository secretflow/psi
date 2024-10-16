# Copyright 2024 Ant Group Co., Ltd.
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

import pandas as pd
import matplotlib.pyplot as plt
import sys
import os


def plot_cpu(docker_csv_path, output_path):
    df1 = pd.read_csv(docker_csv_path)

    plt.plot(df1["running_time_s"], df1["cpu_percent"], marker="o", linestyle="-", color="b")
    max_time_count = 10
    interval = 1
    if len(df1) > max_time_count:
        interval = len(df1) // max_time_count
    for i, row in df1.iterrows():
        if i % interval == 0:
            plt.text(
                row["running_time_s"] + 1,
                0,
                str(row["time"]),
                fontsize=9,
                ha="right",
                rotation=45,
            )
    plt.title("cpu over Time")
    plt.xlabel("running time sec")
    plt.ylabel("cpu")
    plt.grid(True)

    plt.savefig(output_path)
    plt.clf()

def plot_mem(docker_csv_path, output_path):
    df1 = pd.read_csv(docker_csv_path)

    plt.plot(df1["running_time_s"], df1["mem_usage_MB"], marker="o", linestyle="-", color="b")
    max_time_count = 10
    interval = 1
    if len(df1) > max_time_count:
        interval = len(df1) // max_time_count
    for i, row in df1.iterrows():
        if i % interval == 0:
            plt.text(
                row["running_time_s"] + 1,
                0,
                str(row["time"]),
                fontsize=9,
                ha="right",
                rotation=45,
            )
    plt.title("memory over Time")
    plt.xlabel("running time sec")
    plt.ylabel("memory MB")
    plt.grid(True)

    plt.savefig(output_path)
    plt.clf()

def plot_net(docker_csv_path, output_path):
    df1 = pd.read_csv(docker_csv_path)

    plt.plot(df1["running_time_s"], df1["net_tx_kb"], marker="o", linestyle="-", color="b")
    plt.plot(df1["running_time_s"], df1["net_rx_kb"], marker="*", linestyle="-", color="y")
    max_time_count = 10
    interval = 1
    if len(df1) > max_time_count:
        interval = len(df1) // max_time_count
    for i, row in df1.iterrows():
        if i % interval == 0:
            plt.text(
                row["running_time_s"] + 1,
                0,
                str(row["time"]),
                fontsize=9,
                ha="right",
                rotation=45,
            )
    plt.title("network over Time")
    plt.xlabel("running time sec")
    plt.ylabel("network kb")
    plt.grid(True)

    plt.savefig(output_path)
    plt.clf()


if __name__ == "__main__":
    plot_cpu(sys.argv[1], os.path.join(sys.argv[2], "cpu.png"))
    plot_mem(sys.argv[1], os.path.join(sys.argv[2], "mem.png"))
    plot_net(sys.argv[1], os.path.join(sys.argv[2], "net.png"))
