# Quickstart — run bench_chroma.py over BXI

Minimum commands, in order.

Two defaults must be overridden or the run fails: the backing file must be 8 GB
 and the target must run `-m 0x7` with a
local log (`-m 0x1 | tee` onto NFS stalls the reactor).

---

## 1. Target — `carv@192.168.122.103`

```bash
sudo mkdir -p /home1/public/kostaskir
sudo sshfs -o allow_other,StrictHostKeyChecking=no \
    kostaskir@thegates.ics.forth.gr:/home1/public/kostaskir /home1/public/kostaskir

sudo sh -c 'echo 512 > /proc/sys/vm/nr_hugepages'

sudo pkill -9 -f nvmf_tgt
sudo rm -f /tmp/gesalous_junk0.dat
sudo fallocate -l 8G /tmp/gesalous_junk0.dat

sudo sh -c 'cd /home1/public/kostaskir/spdk && ROLE=target PORTALS_PID=11 \
    nohup ./build/bin/nvmf_tgt -m 0x7 > /tmp/target.log 2>&1 &'

cd /home1/public/kostaskir/spdk
sudo ./gesalous_create_target.sh 192.168.2.4 1
```

Wait for `*** NVMe/RDMA Target Listening on 192.168.2.4 port 4420 ***` in
`/tmp/target.log`.

---

## 2. Initiator — `carv@192.168.122.102`

```bash
lsmod | grep bxiv3_initiator && sudo rmmod bxiv3_initiator   # required after a branch change
```
Then:
```bash
cd /home1/public/kostaskir/spdk/kernel_initiator

make NVME_HOST_DIR=/root/rpmbuild/BUILD/linux-5.14.0-570.39.1.el9_6/drivers/nvme/host/ \
     KBUILD_EXTRA_SYMBOLS=/usr/src/bxi3-portals/Module.symvers \
     EXTRA_CFLAGS="-DPTL_RELEASE"

modprobe nvme_core
modprobe nvme_auth
modprobe nvme_keyring
modprobe nvme_fabrics
modprobe nvme
modprobe nvme_rdma

insmod ./bxiv3_initiator.ko
```
---

## 3. Connect, format, mount, run

Run these back to back — a gap over 15 s can drop the connection on branches
without the poll fallback.

```bash
nvme connect -t portals4 -a 192.168.2.4 -s 11 -n nqn.2016-06.io.spdk:cnode1

mkfs.xfs -f /dev/nvme0n1
mkdir -p /mnt/bxi && sudo mount /dev/nvme0n1 /mnt/bxi
chown $USER /mnt/bxi

setsid nohup ~/chroma-venv/bin/python -u ~/bench/bench_chroma.py \
    -p /mnt/bxi/chroma > /tmp/chroma_run.log 2>&1 < /dev/null &
```

Takes ~35 min. Watch:

```bash
tail -f /tmp/chroma_run.log #target
sudo dmesg -w #initiator
expr $(stat -c %s /mnt/bxi/chroma/*/length.bin) / 4     # vectors indexed
```

Result line: `Inserted 100000 vectors in Ns -> N vec/s`.

---

## 4. Stop

```bash
# initiator
sudo umount /mnt/bxi
sudo nvme disconnect -n nqn.2016-06.io.spdk:cnode1

# target
sudo pkill -9 -f nvmf_tgt          # SIGTERM is ignored
sudo rm -f /tmp/gesalous_junk0.dat /var/tmp/spdk.sock
sudo umount -l /home1/public/kostaskir
sudo sh -c 'echo 0 > /proc/sys/vm/nr_hugepages'
```

---

## If the venv does not exist yet

```bash
python3 -m venv ~/chroma-venv
~/chroma-venv/bin/pip install --no-cache-dir chromadb numpy tqdm pysqlite3-binary
cat > ~/chroma-venv/lib64/python3.9/site-packages/sitecustomize.py <<'EOF'
import sys
try:
    import pysqlite3
    sys.modules["sqlite3"] = pysqlite3
except ImportError:
    pass
EOF
~/chroma-venv/bin/python -c "import sqlite3; print(sqlite3.sqlite_version)"   # must be 3.51.x
```

Rocky 9 has sqlite 3.34.1 and Chroma needs >= 3.35 — without that
`sitecustomize.py`, `run_chroma_example.py` fails on import.


