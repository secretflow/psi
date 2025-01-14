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

import csv
import json
import os
import sys
import time
from datetime import datetime

import docker


def stream_container_stats(container_name, output_file):
    client = docker.from_env()

    try:
        container = client.containers.get(container_name)
        stats_stream = container.stats(stream=True)

        with open(output_file, "w", newline="") as csvfile:
            fieldnames = [
                "cpu_percent",
                "mem_usage_MB",
                "mem_limit_MB",
                "net_tx_kb",
                "net_rx_kb",
                "running_time_s",
                "time",
            ]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)

            writer.writeheader()
            prev_net_tx = 0
            prev_net_rx = 0
            prev_cpu_total = 0
            prev_cpu_system = 0
            start_unix_time = int(time.time())
            for stats in stats_stream:
                data = json.loads(stats)
                running_time_s = int(time.time()) - start_unix_time
                cpu_percent = (
                    (
                        (data["cpu_stats"]["cpu_usage"]["total_usage"] - prev_cpu_total)
                        / (data["cpu_stats"]["system_cpu_usage"] - prev_cpu_system)
                    )
                    * 100
                    * os.cpu_count()
                )
                mem_usage = (
                    (
                        data["memory_stats"]["usage"]
                        - data["memory_stats"]["stats"]["inactive_file"]
                    )
                    / 1024
                    / 1024
                )
                mem_limit = data["memory_stats"]["limit"] / 1024 / 1024
                net_tx = 0
                net_rx = 0
                for key, value in data["networks"].items():
                    net_tx += value["tx_bytes"] / 1024
                    net_rx += value["rx_bytes"] / 1024
                # skip first five seconds, due to running setting up network
                if running_time_s > 5:
                    writer.writerow(
                        {
                            "cpu_percent": cpu_percent,
                            "mem_usage_MB": int(mem_usage),
                            "mem_limit_MB": int(mem_limit),
                            "net_tx_kb": int((net_tx - prev_net_tx) * 8),
                            "net_rx_kb": int((net_rx - prev_net_rx) * 8),
                            "running_time_s": running_time_s,
                            "time": datetime.fromtimestamp(time.time()).strftime(
                                "%H:%M:%S"
                            ),
                        }
                    )
                prev_net_tx = net_tx
                prev_net_rx = net_rx
                prev_cpu_total = data["cpu_stats"]["cpu_usage"]["total_usage"]
                prev_cpu_system = data["cpu_stats"]["system_cpu_usage"]

    except docker.errors.NotFound:
        print(f"Container {container_name} not found.")
    except Exception as e:
        if container.status != "exited":
            print(f"An error occurred: {e} container.status: {container.status}")


if __name__ == "__main__":
    stream_container_stats(sys.argv[1], sys.argv[2])
