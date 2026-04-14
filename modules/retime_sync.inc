// 实时同步模块 - 用于插件和电脑端实时通信
// 功能：
// 1. 实时上报采集的商品数据
// 2. 高频检查停止指令
// 3. 心跳状态更新

static NSString *taskRealtimeDir = nil;
static NSString *taskRealtimeGoodsDir = nil;
static NSString *taskRealtimeEventsDir = nil;
static NSTimer *taskRealtimeHeartbeatTimer = nil;
static NSTimeInterval const kRealtimeHeartbeatInterval = 1.0; // 1秒心跳
static NSTimeInterval const kRealtimeStopCheckInterval = 0.5; // 0.5秒检查停止

// 初始化实时同步目录
static void realtime_ensureDirectories(void) {
    if (!taskDataRootDir) return;
    
    taskRealtimeDir = [taskDataRootDir stringByAppendingPathComponent:@"realtime"];
    taskRealtimeGoodsDir = [taskRealtimeDir stringByAppendingPathComponent:@"goods"];
    taskRealtimeEventsDir = [taskRealtimeDir stringByAppendingPathComponent:@"events"];
    
    NSFileManager *fm = [NSFileManager defaultManager];
    [fm createDirectoryAtPath:taskRealtimeGoodsDir 
  withIntermediateDirectories:YES 
                   attributes:nil 
                        error:nil];
    [fm createDirectoryAtPath:taskRealtimeEventsDir 
  withIntermediateDirectories:YES 
                   attributes:nil 
                        error:nil];
}

// 实时上报采集到的商品
static void realtime_reportGoodsCaptured(NSDictionary *goodsData) {
    if (!taskRealtimeGoodsDir) {
        realtime_ensureDirectories();
    }
    
    NSString *goodsId = goodsData[@"goods_id"] ?: goodsData[@"id"];
    if (!goodsId) {
        goodsId = [NSString stringWithFormat:@"unknown_%lld", 
                   (long long)[[NSDate date] timeIntervalSince1970]];
    }
    
    // 构建实时数据
    NSMutableDictionary *realtimeData = [NSMutableDictionary dictionaryWithDictionary:goodsData];
    realtimeData[@"captured_at"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    realtimeData[@"task_id"] = taskCurrentId ?: @"";
    realtimeData[@"keyword"] = taskCurrentKeyword ?: @"";
    realtimeData[@"saved_count"] = @(taskSavedCount);
    realtimeData[@"target_count"] = @(taskTargetCount);
    realtimeData[@"progress_percent"] = @((taskTargetCount > 0) ? (taskSavedCount * 100 / taskTargetCount) : 0);
    
    // 写入文件
    NSString *fileName = [NSString stringWithFormat:@"%@.json", goodsId];
    NSString *filePath = [taskRealtimeGoodsDir stringByAppendingPathComponent:fileName];
    task_writeJSON(realtimeData, filePath);
    
    // 同时写入最新数据文件（方便快速读取）
    NSString *latestPath = [taskRealtimeDir stringByAppendingPathComponent:@"latest_goods.json"];
    task_writeJSON(realtimeData, latestPath);
    
    NSLog(@"[Realtime] 上报商品: %@, 进度: %ld/%ld", 
          goodsId, (long)taskSavedCount, (long)taskTargetCount);
}

// 上报事件
static void realtime_reportEvent(NSString *eventType, NSDictionary *data) {
    if (!taskRealtimeEventsDir) {
        realtime_ensureDirectories();
    }
    
    NSMutableDictionary *eventData = [NSMutableDictionary dictionary];
    eventData[@"event_type"] = eventType;
    eventData[@"timestamp"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    eventData[@"task_id"] = taskCurrentId ?: @"";
    eventData[@"keyword"] = taskCurrentKeyword ?: @"";
    eventData[@"saved_count"] = @(taskSavedCount);
    eventData[@"target_count"] = @(taskTargetCount);
    if (data) {
        eventData[@"data"] = data;
    }
    
    NSString *fileName = [NSString stringWithFormat:@"%@_%lld.json", 
                          eventType, 
                          (long long)[[NSDate date] timeIntervalSince1970]];
    NSString *filePath = [taskRealtimeEventsDir stringByAppendingPathComponent:fileName];
    task_writeJSON(eventData, filePath);
    
    // 更新最新事件
    NSString *latestPath = [taskRealtimeDir stringByAppendingPathComponent:@"latest_event.json"];
    task_writeJSON(eventData, latestPath);
}

// 高频检查停止指令
static BOOL realtime_checkStopCommand(void) {
    if (!task_hasActiveTask()) return NO;
    
    // 检查通用停止信号（不依赖任务ID）
    NSString *globalStopPath = [taskCommandStopDir stringByAppendingPathComponent:@"global.stop"];
    NSFileManager *fm = [NSFileManager defaultManager];
    
    if ([fm fileExistsAtPath:globalStopPath]) {
        [fm removeItemAtPath:globalStopPath error:nil];
        NSLog(@"[Realtime] 收到全局停止指令");
        return YES;
    }
    
    // 检查任务特定停止信号
    NSString *taskStopPath = task_stopFlagPath(taskCurrentId);
    if (taskStopPath && [fm fileExistsAtPath:taskStopPath]) {
        [fm removeItemAtPath:taskStopPath error:nil];
        NSLog(@"[Realtime] 收到任务停止指令: %@", taskCurrentId);
        return YES;
    }
    
    return NO;
}

// 心跳更新 - 每秒更新状态
static void realtime_heartbeatUpdate(void) {
    if (!task_hasActiveTask()) return;
    
    // 更新当前状态到实时目录
    if (!taskRealtimeDir) {
        realtime_ensureDirectories();
    }
    
    NSDictionary *status = task_buildCurrentStatus();
    NSString *statusPath = [taskRealtimeDir stringByAppendingPathComponent:@"status.json"];
    task_writeJSON(status, statusPath);
}

// 启动心跳定时器
static void realtime_startHeartbeat(void) {
    if (taskRealtimeHeartbeatTimer) {
        [taskRealtimeHeartbeatTimer invalidate];
    }
    
    taskRealtimeHeartbeatTimer = [NSTimer scheduledTimerWithTimeInterval:kRealtimeHeartbeatInterval
                                                                  repeats:YES
                                                                    block:^(NSTimer *timer) {
        realtime_heartbeatUpdate();
    }];
    
    // 立即执行一次
    realtime_heartbeatUpdate();
}

// 停止心跳定时器
static void realtime_stopHeartbeat(void) {
    if (taskRealtimeHeartbeatTimer) {
        [taskRealtimeHeartbeatTimer invalidate];
        taskRealtimeHeartbeatTimer = nil;
    }
}

// 清理实时数据
static void realtime_cleanup(void) {
    NSFileManager *fm = [NSFileManager defaultManager];
    
    // 清理商品数据
    if (taskRealtimeGoodsDir) {
        NSArray *files = [fm contentsOfDirectoryAtPath:taskRealtimeGoodsDir error:nil];
        for (NSString *file in files) {
            NSString *path = [taskRealtimeGoodsDir stringByAppendingPathComponent:file];
            [fm removeItemAtPath:path error:nil];
        }
    }
    
    // 清理事件数据
    if (taskRealtimeEventsDir) {
        NSArray *files = [fm contentsOfDirectoryAtPath:taskRealtimeEventsDir error:nil];
        for (NSString *file in files) {
            NSString *path = [taskRealtimeEventsDir stringByAppendingPathComponent:file];
            [fm removeItemAtPath:path error:nil];
        }
    }
    
    NSLog(@"[Realtime] 清理实时数据完成");
}

// 在任务开始时调用
static void realtime_onTaskStarted(void) {
    realtime_ensureDirectories();
    realtime_cleanup();
    realtime_startHeartbeat();
    realtime_reportEvent(@"task_started", @{@"keyword": taskCurrentKeyword ?: @""});
}

// 在任务结束时调用
static void realtime_onTaskFinished(NSString *state, NSString *error) {
    realtime_reportEvent(@"task_finished", @{
        @"state": state ?: @"",
        @"error": error ?: @"",
        @"final_saved_count": @(taskSavedCount)
    });
    realtime_stopHeartbeat();
    
    // 延迟清理，让电脑端有时间读取最终状态
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(5.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        realtime_cleanup();
    });
}
