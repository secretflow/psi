# build psi binary with release-ci docker(for x86 only)

```bash
[//]: <> (OPENSOURCE-CLEANUP REMOVE KEYWORD_ONLY reg.docker.alibaba-inc.com/ )
docker run -it  --rm   --mount type=bind,source="$(pwd)/../../psi",target=/home/admin/dev/src -w /home/admin/dev  --cap-add=SYS_PTRACE --security-opt seccomp=unconfined --cap-add=NET_ADMIN --privileged=true reg.docker.alibaba-inc.com/secretflow/release-ci:1.2 /home/admin/dev/src/docker/entry.sh
```

# build psi dev docker

```bash
bash build.sh -v <version> -u -l
```
- *-u* means upload docker to reg.
- *-l* means tag docker as *latest* as well.

# This is a docker only for use who wish to use the shared library of apsi wrapper.
