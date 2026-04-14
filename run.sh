#!/bin/bash

SSH_HOST="${SSH_HOST:-root@localhost}"
INBOX_DIR="${INBOX_DIR:-/var/mobile/Documents/pdd_ios_agent/commands/inbox}"

show_help() {
    cat << EOF
PDD 数据采集命令行工具

用法:
    $0 <关键词> <数量> [选项]
    $0 --stop <任务ID>
    $0 --batch <文件路径>

示例:
    $0 "手机" 50
    $0 "连衣裙" 100 --sort sales --price-min 20 --price-max 100
    $0 --stop task_001
    $0 --batch keywords.txt

选项:
    --sort <排序>      排序方式: sales(销量), price_asc, price_desc
    --price-min <价格> 最低价格
    --price-max <价格> 最高价格
    --stop <任务ID>   停止指定任务
    --batch <文件>    批量从文件读取关键词(每行一个)
    -h, --help        显示帮助
EOF
}

create_task() {
    local keyword=$1
    local count=$2
    local sort_by=$3
    local price_min=$4
    local price_max=$5

    local task_id="task_$(date +%s)_$$"

    local json="{"
    json+='"action":"collect",'
    json+="\"task_id\":\"$task_id\","
    json+="\"keyword\":\"$keyword\","
    json+="\"count\":$count"

    if [ -n "$sort_by" ]; then
        json+=",\"sort_by\":\"$sort_by\""
    fi
    if [ -n "$price_min" ]; then
        json+=",\"price_min\":\"$price_min\""
    fi
    if [ -n "$price_max" ]; then
        json+=",\"price_max\":\"$price_max\""
    fi
    json+="}"

    local filename="${task_id}.json"

    echo "📤 提交任务: $keyword (数量: $count)"
    echo "$json" | ssh $SSH_HOST "cat > $INBOX_DIR/$filename"

    if [ $? -eq 0 ]; then
        echo "✅ 任务已提交: $task_id"
    else
        echo "❌ 任务提交失败"
        return 1
    fi
}

stop_task() {
    local task_id=$1
    local json="{\"action\":\"stop\",\"task_id\":\"$task_id\"}"
    local filename="stop_$(date +%s)_$$.json"

    echo "🛑 停止任务: $task_id"
    echo "$json" | ssh $SSH_HOST "cat > $INBOX_DIR/$filename"

    if [ $? -eq 0 ]; then
        echo "✅ 停止命令已发送"
    else
        echo "❌ 发送失败"
    fi
}

batch_tasks() {
    local file=$1
    local count=${2:-50}
    local sort_by=$3
    local price_min=$4
    local price_max=$5

    if [ ! -f "$file" ]; then
        echo "❌ 文件不存在: $file"
        return 1
    fi

    local total=$(wc -l < "$file")
    local current=0

    while IFS= read -r keyword || [ -n "$keyword" ]; do
        [ -z "$keyword" ] && continue
        ((current++))
        echo "[$current/$total] 处理: $keyword"
        create_task "$keyword" "$count" "$sort_by" "$price_min" "$price_max"
        sleep 0.5
    done < "$file"

    echo "✅ 批量任务提交完成: $current 个"
}

main() {
    local keyword=""
    local count=50
    local sort_by=""
    local price_min=""
    local price_max=""
    local action="submit"
    local batch_file=""

    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            --stop)
                action="stop"
                shift
                [ -n "$1" ] && local stop_id="$1"
                shift
                ;;
            --batch)
                action="batch"
                shift
                [ -n "$1" ] && batch_file="$1"
                shift
                ;;
            --sort)
                shift
                sort_by="$1"
                shift
                ;;
            --price-min)
                shift
                price_min="$1"
                shift
                ;;
            --price-max)
                shift
                price_max="$1"
                shift
                ;;
            *)
                if [ -z "$keyword" ]; then
                    keyword="$1"
                elif [ -z "$count" ] && [[ "$1" =~ ^[0-9]+$ ]]; then
                    count="$1"
                fi
                shift
                ;;
        esac
    done

    case $action in
        submit)
            if [ -z "$keyword" ]; then
                show_help
                exit 1
            fi
            create_task "$keyword" "$count" "$sort_by" "$price_min" "$price_max"
            ;;
        stop)
            if [ -z "$stop_id" ]; then
                echo "❌ 请指定任务ID"
                exit 1
            fi
            stop_task "$stop_id"
            ;;
        batch)
            if [ -z "$batch_file" ]; then
                echo "❌ 请指定文件路径"
                exit 1
            fi
            batch_tasks "$batch_file" "$count" "$sort_by" "$price_min" "$price_max"
            ;;
    esac
}

main "$@"
