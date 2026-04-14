---
name: pdd-ios-device-collect
description: 通过 skill bundle 自带的公共 `pddctl` 入口，经 SSH 控制越狱 iPhone 上的拼多多采集任务，支持健康检查、离线授权、任务采集、实时 watch、产物导出和本地 triage。适用于用户要在设备侧执行拼多多商品详情采集并需要 agent 直接给出结果与排障结论时；不适用于通用网页浏览、非拼多多站点或只做代码审计的场景。
---

# PDD iOS Device Collect

这个 skill 是一个 **独立、可移植、自包含** 的 bundle 目录。

- 入口脚本：`./scripts/pddctl.py`
- 运行时：`./runtime/pddctl_app/*.pyc`
- 配置模板：`./device-config.example.json`
- 默认本地产物目录：`./runtime/artifacts/`
- 默认本地状态目录：`./runtime/device_state/`
- 本地许可证工作目录：`./runtime/license/`
- bundle 自带验签公钥：`./license_public_key.pem`
- bundle 不携带签发私钥，也不提供许可证签发入口
- 输出契约：与仓库主公共 CLI 一致，统一使用 `command`

执行方式固定为公共命令面：

```bash
python3 ./scripts/pddctl.py <area> <command> ...
```

常用 area：

- `doctor`
- `task collect/status/watch/stop`
- `artifact export/list/triage`
- `license fingerprint/install/status`

## 适用场景

- 用户要在越狱 iPhone 上执行拼多多商品详情采集。
- 用户需要 agent 直接运行健康检查、最小采集验证、watch、导出或排障。
- 用户希望结果是机器可读 JSON / JSONL，同时 agent 能给出可读结论。
- 用户需要明确的失败归因，而不是让 agent 自己猜 SSH、任务状态或调试文件含义。

## 不适用场景

- 用户只是要读代码、重构现有控制端、做 code review。
- 目标站点不是拼多多。
- 用户要的是通用浏览器自动化，而不是 iPhone 设备侧采集。
- 当前没有可用的 SSH 设备链路。

## 运行前提

执行前先确认：

1. 桥接主机上有 `python3`、`ssh`、`scp`。
2. 如果设备走密码 SSH，本机还需要 `expect`。
3. 提供配置 JSON，或显式传入最少的 SSH 参数：
   - `--ssh-host`
   - `--ssh-user`
   - 如需要：`--ssh-password` / `--ssh-key`
   - 推荐模板：`./device-config.example.json`
   - 如果要保持 bundle 自包含，推荐把工作配置放在 `./device-config.json`
4. 本地产物目录和状态目录可写。

配置和 SSH override 可以放在顶层，也可以放在最终动作后。

例如这两种写法都可以：

```bash
python3 ./scripts/pddctl.py \
  --ssh-host 192.168.31.23 \
  --ssh-user root \
  --ssh-password alpine \
  doctor

python3 ./scripts/pddctl.py \
  task collect \
  --ssh-host 192.168.31.23 \
  --ssh-user root \
  --ssh-password alpine \
  --keyword 杯子 \
  --count 1 \
  --wait
```

如果前提不满足，先报告阻塞点，不要自行发明临时 `ssh` / `scp` 命令。

## 默认工作流

除非用户明确指定别的动作，否则按这个顺序执行：

1. `doctor`
2. `license status`
3. `task collect --keyword <keyword> --count 1 --wait`
4. 如果结果失败、`saved_count=0` 或 `detail_confident=false`，再跑 `artifact triage`

最小验证示例：

```bash
python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  doctor

python3 ./scripts/pddctl.py \
  --config ./device-config.json \
  task collect \
  --keyword 杯子 \
  --count 1 \
  --wait
```

默认配置会把产物写到 `./runtime/artifacts/`，把 SSH 控制状态写到 `./runtime/device_state/`。如果还要把本地许可证请求和已签发许可证文件也留在 bundle 内，先建 `./runtime/license/`，并把 `license-request.json`、`license.json` 也放在这里。这里不是签发端目录，不应放 `license_signing_key.pem`。

## 常用命令

### `doctor`

用于：

- SSH 链路健康检查
- 控制 socket 检查
- 远端 `PDDGoodsData` 根目录发现
- 远端状态文件可读性检查

### `task collect`

用于：

- 下发采集任务
- 可选等待终态
- 终态后自动同步、规整本地目录、生成 `goods_items.json` 并输出 triage 摘要

常用参数：

- `--keyword`
- `--count`
- `--wait`
- `--fresh-start` / `--no-fresh-start`
- `--sort-by sales`
- `--price-min`
- `--price-max`

### `license fingerprint`

用于：

- 读取当前 iPhone 的授权绑定指纹
- 导出 `license-request.json`
- 供外部签发端生成离线许可证

### `license install`

用于：

- 安装签名后的 `license.json`
- 校验签名、产品标识、到期时间
- 如果当前连接了设备，还会核对绑定是否匹配
- `license-request.json` 的 `generated_at`，以及 `license.json` 里的 `issued_at` / `expires_at`，统一使用东八区 `YYYY-MM-DD HH:mm:ss`
- skill 只负责安装和验签，不负责生成 `license.json`

### `license status`

用于：

- 查看当前设备侧离线许可证状态
- 如果当前提供了 SSH 参数，还会报告是否与当前 iPhone 绑定一致
- 它只是状态探测，不是运行时最终真值

### `task watch`

用于：

- 实时附着当前任务或指定任务
- 读取 JSONL 事件流
- 在 `saved_count` 增加时增量拉取 `raw/<goods_id>.txt`

### `artifact export`

用于：

- 导出最近任务或指定任务的本地产物
- 如果本地没有，再尝试从设备端同步
- 命令返回的是本地目录入口，不再生成 zip

### `artifact list`

用于：

- 列出本地已有任务目录
- 快速找最近任务、summary、log 目录

### `artifact triage`

用于：

- 读取 `summary.json`、`raw/*.txt`、`log/debug_*`
- 判断是否真的抓到结构化详情
- 给出最应该先看的文件和下一步

## 输出规则

普通动作输出单个 JSON：

- `ok`
- `command`
- `message`
- `data`

失败时输出：

- `ok=false`
- `command`
- `error`

`task watch` 例外：

- 输出 JSONL
- 每行一个事件
- 固定字段：`ok`、`command`、`event`、`data`

更多字段说明见 [output-contract.md](./references/output-contract.md)。

## 结果判定护栏

必须遵守：

1. 不要只因为 `saved_count>0` 就认定抓到了真实商品详情。
2. 不要只因为 `page_name=goods_detail` 就认定成功。
3. 真实详情优先按结构判断：
   - `goods`
   - `sku`
   - 价格对象
4. `saved_count=0` 或 `detail_confident=false` 时，先看 `log/debug_*`，不要先改 matcher。
5. 如果候选 payload 都像事件埋点且没有 URL，优先看 `debug_*_network_response.txt`。
6. 如果页面进入详情页但没继续点击，优先看 `debug_*_click_no_target.txt`。
7. 如果 `collect` 终态失败且错误与授权有关，不要把 `license status` 单独当最终真值；先看本地 `./runtime/artifacts/<task_id>/metadata/manifest.json` / `./runtime/artifacts/<task_id>/metadata/status.json` 里的 `license` 字段、设备侧 `license/status.json` 和 `./runtime/artifacts/<task_id>/log/debug_*_license_blocked.txt` / `./runtime/artifacts/<task_id>/log/debug_*_license_runtime_blocked.txt`。

## 用户可见汇报要求

每次执行后都要把原始 JSON 转成可读结论：

- 成功时：
  - 任务是否完成
  - `task_id`
  - `saved_count`
  - `goods_item_count`
  - `detail_confident`
  - 最重要的路径，例如 `local_task_root`、`log_dir`、`summary_path`
  - 如果 `goods_item_count>0`，必须把商品列表逐条展示出来：
    - 商品编号
    - 商品名称
    - 价格
    - 销量
  - 如果 `goods_item_count=0`，必须明确说明当前没有提取到可展示的商品汇总，并给出 `goods_items_path` 或 `local_task_root`
- 失败时：
  - 先报错误码
  - 再说明当前阻塞点
  - 再给出下一步
  - 如果已经成功汇总出部分商品，可以补充说明已汇总的条数

不要把整段原始 JSON 直接扔给用户，除非用户明确要求。

## 何时读取 references

- 日常执行只需看本文件。
- 需要固定命令套路时，看 [workflows.md](./references/workflows.md)。
- 需要解释字段时，看 [output-contract.md](./references/output-contract.md)。
- 需要做失败归因和排障顺序时，看 [troubleshooting.md](./references/troubleshooting.md)。
