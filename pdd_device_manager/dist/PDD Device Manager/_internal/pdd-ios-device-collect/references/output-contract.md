# Output Contract

## 普通命令

普通命令返回单个 JSON 对象：

```json
{
  "ok": true,
  "command": "task.collect",
  "message": "任务已结束",
  "data": {}
}
```

失败时：

```json
{
  "ok": false,
  "command": "task.collect",
  "error": {
    "code": "TASK_FAILED",
    "message": "task failed"
  }
}
```

## `doctor`

`data` 至少包含：

- `summary`
- `checks`
- `license_required`
- `binding_id`
- `binding_source`

## `task.collect`

等待终态时，`data` 至少包含：

- `task_id`
- `state`
- `keyword`
- `target_count`
- `saved_count`
- `attempted_count`
- `failed_count`
- `local_task_root`
- `summary_path`
- `log_dir`
- `manifest_path`
- `goods_items_path`
- `goods_item_count`
- `raw_count`
- `detail_confident`
- `signal_codes`
- `recommended_first_files`
- `next_steps`

如果请求了筛选，还可能包含：

- `sort_by`
- `price_min`
- `price_max`
- `sales_sort_applied`
- `price_filter_applied`
- `filter_warning`

## `artifact.export`

`data` 结构与终态 `task.collect` 接近，但不包含 `doctor` 结果。

商品汇总文件 `goods_items.json` 固定包含：

- `task_id`
- `keyword`
- `goods_item_count`
- `items[]`
  - `goods_id`
  - `goods_name`
  - `price`
  - `sales`

## `artifact.list`

`data.items[]` 至少包含：

- `task_id`
- `state`
- `keyword`
- `saved_count`
- `target_count`
- `local_task_root`
- `log_dir`

## `artifact.triage`

`data` 至少包含：

- `task_id`
- `state`
- `saved_count`
- `raw_count`
- `detail_confident`
- `signal_codes`
- `recommended_first_files`
- `next_steps`
- `summary_path`
- `local_task_root`
- `log_dir`

## `license.fingerprint`

`data` 至少包含：

- `schema_version`
- `request_type`
- `product_code`
- `generated_at`
  - 格式统一为东八区 `YYYY-MM-DD HH:mm:ss`
- `binding_id`
- `binding_source`
- `device_name`
- `product_type`
- `os_version`
- 可选 `request_path`

## `license.install`

`data` 至少包含：

- `license_id`
- `product_code`
- `issued_at`
- `expires_at`
- `bound_binding_id`
- `features`
- `installed_path`
- `source_path`
- 可选 `binding_match`

时间字段统一为东八区 `YYYY-MM-DD HH:mm:ss`

## `license.status`

`data` 至少包含：

- `installed`
- `valid`
- `status`
- `installed_path`

如果已安装成功解析，还会包含：

- `license_id`
- `product_code`
- `issued_at`
- `expires_at`
- `bound_binding_id`
- `features`

这些许可证时间字段统一为东八区 `YYYY-MM-DD HH:mm:ss`

如果提供了 SSH 参数并做了绑定探测，还会包含：

- `binding_probe_ok`
- `current_binding_id`
- `binding_match`

## `task.watch`

`task.watch` 输出 JSONL，每行一个事件：

```json
{"ok":true,"command":"task.watch","event":"attached","data":{}}
{"ok":true,"command":"task.watch","event":"progress","data":{}}
{"ok":true,"command":"task.watch","event":"terminal","data":{}}
```

事件名固定为：

- `attached`
- `progress`
- `warning`
- `terminal`
- 失败时附加 `error`

`terminal` 事件的 `data` 会在可能时补充：

- `goods_items_path`
- `goods_item_count`
- `raw_count`
- `detail_confident`
- `signal_codes`
- `recommended_first_files`
- `next_steps`
- 如果任务请求了筛选，还会透出筛选请求和筛选结果字段
