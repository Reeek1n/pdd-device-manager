# Workflows

## 配置模板

先复制模板：

```bash
cp ./device-config.example.json ./device-config.json
```

再按现场填写 SSH 信息：

- `ssh_host`
- `ssh_user`
- `ssh_port`
- `ssh_password` 或 `ssh_key`
- 如有需要：`remote_root_override`

默认模板里的 `license_public_key_path` 指向 `./license_public_key.pem`。

- 如果配置文件放在 skill 根目录，直接可用
- 默认 `local_artifacts_dir` 会落到 `./runtime/artifacts/`
- 默认 `local_state_dir` 会落到 `./runtime/device_state/`
- 如果你把许可证请求和已签发许可证文件也留在 bundle 内，推荐统一放到 `./runtime/license/`
- 如果你把 `device-config.json` 拷到别处，记得把 `license_public_key.pem` 一起放到同目录，或手动改成正确路径
- 如果你希望 bundle 继续保持自包含，不要把配置文件移出 skill 根目录
- 独立 skill bundle 只负责生成请求、安装许可证和公钥验签；不负责签发，也不应携带私钥

## 直接 SSH 最小验证

```bash
python3 ./scripts/pddctl.py \
  doctor \
  --ssh-host 192.168.31.23 \
  --ssh-user root \
  --ssh-password alpine

python3 ./scripts/pddctl.py \
  task collect \
  --ssh-host 192.168.31.23 \
  --ssh-user root \
  --ssh-password alpine \
  --keyword 杯子 \
  --count 1 \
  --wait
```

公共 CLI 支持把 `--config` 和 SSH override 放在顶层，也支持放在最终动作后。

## 最小验证

```bash
mkdir -p ./runtime/license

python3 ./scripts/pddctl.py --config ./device-config.json doctor

python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  license fingerprint \
  --out ./runtime/license/license-request.json

把 `./runtime/license/license-request.json` 发给外部签发端，拿回已经签好的 `license.json` 后，再安装：

python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  license install \
  --file ./runtime/license/license.json

python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task collect \
  --keyword 杯子 \
  --count 1 \
  --wait
```

签发出来的 `license.json` 里，`issued_at` / `expires_at`，以及 `license-request.json` 里的 `generated_at`，统一使用东八区 `YYYY-MM-DD HH:mm:ss`，例如 `2026-04-06 12:44:13`、`2027-04-06 23:59:59`。如果手工传 `--expires-at`，记得写成 `--expires-at '2027-04-06 23:59:59'`。否则设备侧运行时可能报 `LICENSE_INVALID`，并带 `detail=issued_at or expires_at must use YYYY-MM-DD HH:mm:ss in UTC+8`。

不要把签发私钥带进这个 skill bundle。`./runtime/license/` 只用于存请求文件和已签发许可证，不用于本地签发。

带筛选的最小验证：

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task collect \
  --keyword 杯子 \
  --count 1 \
  --sort-by sales \
  --price-min 10 \
  --price-max 99 \
  --wait
```

## 实时 watch

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task watch
```

观察指定任务：

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task watch \
  --task-id pdd_20260405_005823
```

不增量拉取 raw：

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task watch \
  --no-sync-raw
```

## 导出最近任务

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  artifact export
```

成功后优先查看：

- `runtime/artifacts/<task_id>/goods_items.json`
- `runtime/artifacts/<task_id>/summary.json`
- `runtime/artifacts/<task_id>/metadata/manifest.json`
- `runtime/artifacts/<task_id>/metadata/status.json`

`artifact export` 返回的是本地目录入口，不再额外生成 zip。

## 查看本地产物

```bash
python3 ./scripts/pddctl.py \
  artifact list \
  --artifacts-dir ./runtime/artifacts
```

## 复盘失败任务

```bash
python3 ./scripts/pddctl.py \
  artifact triage \
  --task-root ./runtime/artifacts/pdd_20260405_005823
```

或在本地产物目录里直接分析最近任务：

```bash
python3 ./scripts/pddctl.py \
  artifact triage \
  --artifacts-dir ./runtime/artifacts
```
