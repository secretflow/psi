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

import threading
import subprocess
import time

from numpy.random import randint

import psi.libpsi.link as link


def get_free_port():
    return randint(low=49152, high=65536)


def wc_count(file_name):
    out = subprocess.getoutput("wc -l %s" % file_name)
    return int(out.split()[0])


def create_link_desc(world_size: int):
    time_stamp = time.time()
    lctx_desc = link.Desc()
    lctx_desc.id = str(round(time_stamp * 1000))

    for rank in range(world_size):
        port = get_free_port()
        lctx_desc.add_party(f"id_{rank}", f"127.0.0.1:{port}")

    return lctx_desc


# https://stackoverflow.com/questions/2829329/catch-a-threads-exception-in-the-caller-thread-in-python
class PropagatingThread(threading.Thread):
    def run(self):
        self.exc = None
        try:
            self.ret = self._target(*self._args, **self._kwargs)
        except BaseException as e:
            self.exc = e

    def join(self):
        super(PropagatingThread, self).join()
        if self.exc:
            raise self.exc
        return self.ret
