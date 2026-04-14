# Troubleshooting

## 优先顺序

失败时按这个顺序判断，不要跳步：

1. SSH 链路是否可达
2. 控制 socket 是否健康
3. 远端 `PDDGoodsData` 根目录是否可发现
4. 任务有没有真正进入终态
5. `raw/*.txt` 是否含结构化详情
6. `debug_*` 里看到的是不是埋点事件
7. 最后才考虑 matcher 变更

## 详情成功的保守判定

以下都 **不够** 判定成功：

- `saved_count>0`
- `page_name=goods_detail`
- 单纯 `op=impr`
- 单纯 `goods_id`

优先确认是否有结构化字段：

- `goods`
- `sku`
- 价格对象

## 常见信号

### `SSH_OR_REMOTE_ROOT_BLOCKED`

含义：

- SSH 不通
- 远端根目录不可达
- doctor 还没通过

下一步：

- 先修 SSH / remote root
- 不要先改任务逻辑或 matcher

### `CONFIG_INVALID missing config keys: ssh_host, ssh_user`

含义：

- 常见于配置文件没有真正被读到，或者直接 SSH 参数没有真正传进去
- 也可能是 `device-config.json` 路径不对，或者当前命令其实没有读到你以为的配置文件

下一步：

- 先检查 `device-config.json` 里是否真的有 `ssh_host` / `ssh_user`
- 再确认你跑的是当前 bundle 入口 `python3 ./scripts/pddctl.py ...`
- 如果走直接 SSH override，确认参数放在顶层或最终动作后，且 `task/artifact/license` 层级写对了

### `NO_STRUCTURED_DETAIL`

含义：

- 任务可能结束了
- 但本地 `raw/*.txt` 里没有结构化详情

下一步：

- 不要把 `saved_count` 当真值
- 先看 `recommended_first_files`

### `LIKELY_EVENT_PAYLOAD`

含义：

- 候选 payload 或 raw 更像埋点事件
- 常见于：
  - `page_name=search_result` 且 `op=click/impr/epv`
  - `page_name=goods_detail` 且 `op=impr`

下一步：

- 继续查网络响应调试
- 不要盲目放宽详情匹配

### `CHECK_NETWORK_RESPONSE`

含义：

- 候选 payload 缺少真实请求 URL
- 更可能需要查看网络响应现场

下一步：

- 先看 `debug_*_network_response.txt`

### `CHECK_CLICK_TARGET`

含义：

- 详情页或中间页没有找到继续点击目标

下一步：

- 看 `debug_*_click_no_target.txt`

### `LICENSE_INVALID`

含义：

- 许可证文件本身无效
- 如果 `detail=issued_at or expires_at must use YYYY-MM-DD HH:mm:ss in UTC+8`，通常是签发时间格式不对
- 设备侧要求 `issued_at` / `expires_at` 使用东八区 `YYYY-MM-DD HH:mm:ss`
- 如果手工传了 `--expires-at`，还要确认命令行里给这个带空格的时间加了引号

下一步：

- 重新签发许可证
- 确认时间格式类似 `2026-04-06 12:44:13`
- 安装新文件后再跑 `license status` 和最小 `collect`

### 仓库签发端以为“没有签发私钥”

含义：

- 仓库根目录可能没有 `license_signing_key.pem`
- 但当前现场真正复用的签发私钥通常在 `./runtime/device_state/license/license_signing_key.pem`
- 如果忽略这一点临时生成新 key，主机端公钥、skill 公钥和设备侧 embedded public key 很容易和签发私钥失配
- 这条只适用于仓库内受控签发端，不适用于独立 skill bundle
- 独立 skill bundle 应只带公钥，不应携带签发私钥

下一步：

- 如果你当前在仓库签发端现场，先检查 `./runtime/device_state/license/license_signing_key.pem`
- 确认它和当前 bundle 根目录里的 `./license_public_key.pem` 是同一对
- 再由签发端用这把私钥签发许可证

### `license status` 看起来 active，但 `collect` 仍失败

含义：

- `license status` 只是一次状态探测
- 真正拦截任务的是设备侧运行时 gate
- 两者冲突时，运行时现场优先级更高

下一步：

- 先看本地 `./runtime/artifacts/<task_id>/metadata/manifest.json` / `./runtime/artifacts/<task_id>/metadata/status.json` 里的 `license` 字段
- 再看设备侧 `license/status.json`
- 再看 `./runtime/artifacts/<task_id>/log/debug_*_license_blocked.txt` / `./runtime/artifacts/<task_id>/log/debug_*_license_runtime_blocked.txt`

## 结论模板

给用户汇报时优先说：

1. 是否成功跑完
2. 是否真的拿到结构化详情
3. 是否已经汇总出商品列表，以及条数
4. 如果有商品列表，逐条展示商品编号、商品名称、价格、销量
5. 目前最先该看的文件
6. 下一步应该做什么
