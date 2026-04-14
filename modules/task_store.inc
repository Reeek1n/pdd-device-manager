// 前置声明 - 实时 Socket 通信函数
static BOOL realtime_startServer(int port);
static void realtime_notifyTaskStarted(NSString *keyword, int targetCount);
static void realtime_notifyTaskFinished(NSString *state, NSString *error);

static void task_ensureCommandDirectories(void) {
    if (!taskDataRootDir) {
        return;
    }
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSArray *paths = @[taskCommandRootDir ?: @"",
                       taskCommandInboxDir ?: @"",
                       taskCommandStatusDir ?: @"",
                       taskCommandOutboxDir ?: @"",
                       taskCommandStopDir ?: @"",
                       taskRootDir ?: @"",
                       taskLicenseDir ?: @""];
    for (NSString *path in paths) {
        if (path.length == 0) {
            continue;
        }
        if (![fileManager fileExistsAtPath:path]) {
            [fileManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
        }
    }
}

static BOOL task_writeJSON(id jsonObject, NSString *path) {
    if (!jsonObject || path.length == 0) {
        return NO;
    }
    @try {
        NSError *serializeError = nil;
        NSData *jsonData = [NSJSONSerialization dataWithJSONObject:jsonObject options:NSJSONWritingPrettyPrinted error:&serializeError];
        if (!jsonData || serializeError) {
            return NO;
        }
        return [jsonData writeToFile:path atomically:YES];
    } @catch (NSException *exception) {
        return NO;
    }
}

static NSDictionary *task_readJSON(NSString *path) {
    if (path.length == 0) {
        return nil;
    }
    @try {
        NSData *jsonData = [NSData dataWithContentsOfFile:path];
        if (!jsonData) {
            return nil;
        }
        NSError *error = nil;
        id jsonObject = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&error];
        if (error || ![jsonObject isKindOfClass:[NSDictionary class]]) {
            return nil;
        }
        return (NSDictionary *)jsonObject;
    } @catch (NSException *exception) {
        return nil;
    }
}

static BOOL task_hasActiveTask(void) {
    return taskCurrentId.length > 0;
}

static NSString *task_stopFlagPath(NSString *taskId) {
    if (taskId.length == 0 || taskCommandStopDir.length == 0) {
        return nil;
    }
    return [taskCommandStopDir stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.flag", taskId]];
}

static NSString *task_filePath(NSString *fileName) {
    if (taskCurrentDir.length == 0 || fileName.length == 0) {
        return nil;
    }
    return [taskCurrentDir stringByAppendingPathComponent:fileName];
}

static void task_cleanupDebugFiles(NSString *taskDir) {
    if (taskDir.length == 0) {
        return;
    }
    
    @try {
        NSFileManager *fm = [NSFileManager defaultManager];
        NSArray *files = [fm contentsOfDirectoryAtPath:taskDir error:nil];
        NSInteger deletedCount = 0;
        
        for (NSString *fileName in files) {
            // 保留 manifest.json, status.json 和 raw 目录
            if ([fileName isEqualToString:@"manifest.json"] ||
                [fileName isEqualToString:@"status.json"] ||
                [fileName isEqualToString:@"raw"]) {
                continue;
            }
            
            // 删除 debug 文件
            if ([fileName hasPrefix:@"debug_"] && [fileName hasSuffix:@".txt"]) {
                NSString *filePath = [taskDir stringByAppendingPathComponent:fileName];
                [fm removeItemAtPath:filePath error:nil];
                deletedCount++;
            }
        }
        
        if (deletedCount > 0) {
            NSLog(@"[PDDTweak] 已清理 %ld 个 debug 文件", (long)deletedCount);
        }
    } @catch (NSException *exception) {
    }
}

static NSDictionary *task_buildCurrentStatus(void) {
    NSMutableDictionary *status = [NSMutableDictionary dictionary];
    status[@"updated_at"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    status[@"enabled"] = @(tweakIsEnabled);
    status[@"active"] = @(task_hasActiveTask());
    status[@"state"] = taskCurrentState ?: @"idle";
    NSDictionary *licenseStatus = taskLastLicenseStatus ?: task_readJSON(taskLicenseStatusPath);
    if ([licenseStatus isKindOfClass:[NSDictionary class]]) {
        status[@"license"] = licenseStatus;
    }
    if (task_hasActiveTask()) {
        status[@"task_id"] = taskCurrentId ?: @"";
        status[@"keyword"] = taskCurrentKeyword ?: @"";
        status[@"target_count"] = @(taskTargetCount);
        status[@"saved_count"] = @(taskSavedCount);
        status[@"attempted_count"] = @(taskAttemptedCount);
        status[@"detail_entry_count"] = @(taskAttemptedCount);
        status[@"failed_count"] = @(taskFailedCount);
        status[@"duplicate_skip_count"] = @(taskDuplicateSkipCount);
        status[@"unknown_entry_count"] = @(taskUnknownEntryCount);
        status[@"no_new_scroll_rounds"] = @(taskConsecutiveNoNewRounds);
        status[@"last_goods_id"] = taskLastGoodsId ?: @"";
        if (taskRequestedSortBy.length > 0) {
            status[@"sort_by"] = taskRequestedSortBy;
            status[@"sales_sort_applied"] = @(taskSalesSortApplied);
        }
        if (taskRequestedPriceMin.length > 0) {
            status[@"price_min"] = taskRequestedPriceMin;
        }
        if (taskRequestedPriceMax.length > 0) {
            status[@"price_max"] = taskRequestedPriceMax;
        }
        if (taskRequestedReviewMin > 0) {
            status[@"review_min"] = @(taskRequestedReviewMin);
        }
        if (taskRequestedPriceMin.length > 0 || taskRequestedPriceMax.length > 0) {
            status[@"price_filter_applied"] = @(taskPriceFilterApplied);
        }
        if (taskFilterWarning.length > 0) {
            status[@"filter_warning"] = taskFilterWarning;
        }
        if (taskCurrentError.length > 0) {
            status[@"error"] = taskCurrentError;
        }
        if (taskStopReasonDetail.length > 0) {
            status[@"stop_reason_detail"] = taskStopReasonDetail;
        }
    }
    return status;
}

static void task_writeCurrentStatus(void) {
    if (taskCommandStatusDir.length == 0) {
        return;
    }
    task_writeJSON(task_buildCurrentStatus(), [taskCommandStatusDir stringByAppendingPathComponent:@"current.json"]);
}

static void task_clearActiveTaskState(void) {
    taskCurrentId = nil;
    taskCurrentKeyword = nil;
    taskCurrentDir = nil;
    taskCurrentRawDir = nil;
    taskCurrentState = @"idle";
    taskCurrentError = nil;
    taskLastGoodsId = nil;
    taskRequestedSortBy = nil;
    taskRequestedPriceMin = nil;
    taskRequestedPriceMax = nil;
    taskRequestedReviewMin = 0;
    taskTargetCount = 0;
    taskSavedCount = 0;
    taskAttemptedCount = 0;
    taskFailedCount = 0;
    taskConsecutiveFailureCount = 0;
    taskConsecutiveNoNewRounds = 0;
    taskSavedCountAtLastScroll = 0;
    taskDuplicateSkipCount = 0;
    taskUnknownEntryCount = 0;
    taskConsecutiveUnknownOrClickFailures = 0;
    taskConsecutiveSameSearchPageCount = 0;
    taskSearchRecoveryAttemptCount = 0;
    taskSearchTransitionRetryCount = 0;
    taskSalesSortApplied = NO;
    taskPriceFilterApplied = NO;
    taskFilterWarning = nil;
    taskSalesSortAttempted = NO;
    taskSalesSortPendingConfirmation = NO;
    taskSalesSortObservedListReload = NO;
    taskPriceFilterPanelOpened = NO;
    taskPriceFilterInputsFilled = NO;
    taskPriceFilterPendingConfirmation = NO;
    taskPriceFilterObservedListReload = NO;
    taskPriceFilterAttempted = NO;
    taskPriceFilterApplyAttemptCount = 0;
    taskPriceFilterConfirmAttemptCount = 0;
    taskSearchFilterSettleUntil = 0;
    taskSalesSortRequestedAt = 0;
    taskPriceFilterConfirmedAt = 0;
    taskStartedAt = 0;
    taskDetailNavigationStartedAt = 0;
    taskObservedDetailSignalAt = 0;
    taskSearchResultsPendingStartedAt = 0;
    taskSalesSortBaselineVisibleCount = 0;
    taskPriceFilterBaselineVisibleCount = 0;
    taskSavedCountBeforeDetailNavigation = 0;
    taskCandidateDebugCount = 0;
    taskNetworkDebugCount = 0;
    taskRequestDebugCount = 0;
    taskLifecycleDebugCount = 0;
    taskSearchSubmitted = NO;
    taskAwaitingSearchPage = NO;
    taskSearchTransitionStartedAt = 0;
    taskSearchTransitionSubmitAttempted = NO;
    taskNeedsBackNavigation = NO;
    taskDebugSequence = 0;
    taskDetailCollectionStage = 0;
    taskDetailCollectionLastActionAt = 0;
    taskDetailAttemptCounted = NO;
    taskLastDetailCandidateWasDuplicate = NO;
    taskStopReasonDetail = nil;
    taskLastSearchPageSignature = nil;
    taskSalesSortBaselineSignature = nil;
    taskPriceFilterBaselineSignature = nil;
    taskSeenGoodsIds = nil;
    taskSavedFiles = nil;
    taskSearchPayloadByGoodsId = nil;
    taskRecentCandidates = nil;
}

static void task_writeCurrentTaskStatus(void) {
    if (!task_hasActiveTask()) {
        task_writeCurrentStatus();
        return;
    }
    NSString *taskStatusPath = task_filePath(@"status.json");
    if (taskStatusPath.length > 0) {
        task_writeJSON(task_buildCurrentStatus(), taskStatusPath);
    }
    task_writeCurrentStatus();
}

static void task_appendFilterWarning(NSString *warning) {
    NSString *normalized = [[NSString stringWithFormat:@"%@", warning ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (normalized.length == 0) {
        return;
    }
    if (taskFilterWarning.length == 0) {
        taskFilterWarning = [normalized copy];
        return;
    }
    NSArray *parts = [taskFilterWarning componentsSeparatedByString:@" | "];
    if ([parts containsObject:normalized]) {
        return;
    }
    taskFilterWarning = [[taskFilterWarning stringByAppendingString:@" | "] stringByAppendingString:normalized];
}

static BOOL task_shouldStopCurrentTask(void) {
    if (!task_hasActiveTask()) {
        return NO;
    }
    NSString *stopPath = task_stopFlagPath(taskCurrentId);
    if (stopPath.length == 0) {
        return NO;
    }
    if ([[NSFileManager defaultManager] fileExistsAtPath:stopPath]) {
        [[NSFileManager defaultManager] removeItemAtPath:stopPath error:nil];
        return YES;
    }
    return NO;
}

static BOOL task_writeTaskMetadata(NSDictionary *taskInfo) {
    if (taskCurrentDir.length == 0 || !taskInfo) {
        return NO;
    }
    return task_writeJSON(taskInfo, task_filePath(@"task.json"));
}

static void task_finishActiveTask(NSString *state, NSString *errorMessage) {
    if (!task_hasActiveTask()) {
        task_writeCurrentStatus();
        return;
    }
    NSString *finalTaskId = [taskCurrentId copy];
    NSString *finalTaskDir = [taskCurrentDir copy];
    NSString *finalKeyword = [taskCurrentKeyword copy] ?: @"";
    NSString *finalState = [state copy] ?: @"failed";
    NSString *finalError = [errorMessage copy];
    NSArray *finalFiles = [taskSavedFiles copy] ?: @[];

    taskCurrentState = finalState;
    taskCurrentError = finalError;
    taskAutoSearchActive = NO;
    if (taskAutoSearchTimer) {
        [taskAutoSearchTimer invalidate];
        taskAutoSearchTimer = nil;
    }
    taskCurrentProductIndex = 0;
    task_writeCurrentTaskStatus();
    
    // 通知客户端任务结束
    realtime_notifyTaskFinished(finalState, finalError);

    NSMutableDictionary *manifest = [NSMutableDictionary dictionary];
    manifest[@"task_id"] = finalTaskId ?: @"";
    manifest[@"state"] = finalState;
    manifest[@"keyword"] = finalKeyword;
    manifest[@"target_count"] = @(taskTargetCount);
    manifest[@"saved_count"] = @(taskSavedCount);
    manifest[@"attempted_count"] = @(taskAttemptedCount);
    manifest[@"detail_entry_count"] = @(taskAttemptedCount);
    manifest[@"failed_count"] = @(taskFailedCount);
    manifest[@"duplicate_skip_count"] = @(taskDuplicateSkipCount);
    manifest[@"unknown_entry_count"] = @(taskUnknownEntryCount);
    manifest[@"no_new_scroll_rounds"] = @(taskConsecutiveNoNewRounds);
    manifest[@"started_at"] = @((long long)taskStartedAt);
    manifest[@"finished_at"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    manifest[@"files"] = finalFiles;
    if (finalError.length > 0) {
        manifest[@"error"] = finalError;
    }
    if (taskRequestedSortBy.length > 0) {
        manifest[@"sort_by"] = taskRequestedSortBy;
        manifest[@"sales_sort_applied"] = @(taskSalesSortApplied);
    }
    if (taskRequestedPriceMin.length > 0) {
        manifest[@"price_min"] = taskRequestedPriceMin;
    }
    if (taskRequestedPriceMax.length > 0) {
        manifest[@"price_max"] = taskRequestedPriceMax;
    }
    if (taskRequestedReviewMin > 0) {
        manifest[@"review_min"] = @(taskRequestedReviewMin);
    }
    if (taskRequestedPriceMin.length > 0 || taskRequestedPriceMax.length > 0) {
        manifest[@"price_filter_applied"] = @(taskPriceFilterApplied);
    }
    if (taskFilterWarning.length > 0) {
        manifest[@"filter_warning"] = taskFilterWarning;
    }
    if (taskStopReasonDetail.length > 0) {
        manifest[@"stop_reason_detail"] = taskStopReasonDetail;
    }
    if ([taskLastLicenseStatus isKindOfClass:[NSDictionary class]]) {
        manifest[@"license"] = taskLastLicenseStatus;
    }

    if (finalTaskDir.length > 0) {
        task_writeJSON(manifest, [finalTaskDir stringByAppendingPathComponent:@"manifest.json"]);
        task_writeJSON(manifest, [finalTaskDir stringByAppendingPathComponent:@"status.json"]);
        
        // 清理 debug 文件，只保留 raw 目录中的商品数据和 manifest.json
        task_cleanupDebugFiles(finalTaskDir);
    }
    if (taskCommandOutboxDir.length > 0 && finalTaskId.length > 0) {
        NSString *outboxPath = [taskCommandOutboxDir stringByAppendingPathComponent:[NSString stringWithFormat:@"%@.json", finalTaskId]];
        task_writeJSON(manifest, outboxPath);
    }

    task_clearActiveTaskState();
    task_writeCurrentStatus();
}

static void task_startCollectTask(NSDictionary *taskInfo) {
    if (![taskInfo isKindOfClass:[NSDictionary class]]) {
        return;
    }
    if (task_hasActiveTask()) {
        return;
    }

    NSString *taskId = [NSString stringWithFormat:@"%@", taskInfo[@"task_id"] ?: @""];
    NSString *keyword = [NSString stringWithFormat:@"%@", taskInfo[@"keyword"] ?: @""];
    NSString *sortBy = [[NSString stringWithFormat:@"%@", taskInfo[@"sort_by"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *priceMin = [[NSString stringWithFormat:@"%@", taskInfo[@"price_min"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *priceMax = [[NSString stringWithFormat:@"%@", taskInfo[@"price_max"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSInteger count = [taskInfo[@"count"] integerValue];
    if (taskId.length == 0 || keyword.length == 0 || count <= 0) {
        return;
    }

    initializePDDStoragePaths();
    task_ensureCommandDirectories();
    hook_installNSURLSessionDelegateHooks();
    taskCurrentId = taskId;
    taskCurrentKeyword = keyword;
    taskRequestedSortBy = sortBy.length > 0 ? sortBy : nil;
    taskRequestedPriceMin = priceMin.length > 0 ? priceMin : nil;
    taskRequestedPriceMax = priceMax.length > 0 ? priceMax : nil;
    taskRequestedReviewMin = 200;
    taskTargetCount = count;
    taskCurrentState = @"queued";
    taskStartedAt = [[NSDate date] timeIntervalSince1970];
    taskDetailNavigationStartedAt = 0;
    taskCandidateDebugCount = 0;
    taskNetworkDebugCount = 0;
    taskCurrentDir = [taskRootDir stringByAppendingPathComponent:taskId];
    taskCurrentRawDir = [taskCurrentDir stringByAppendingPathComponent:@"raw"];
    taskSeenGoodsIds = [NSMutableSet set];
    taskSavedFiles = [NSMutableArray array];
    taskRecentCandidates = [NSMutableArray array];

    NSFileManager *fileManager = [NSFileManager defaultManager];
    [fileManager createDirectoryAtPath:taskCurrentDir withIntermediateDirectories:YES attributes:nil error:nil];
    [fileManager createDirectoryAtPath:taskCurrentRawDir withIntermediateDirectories:YES attributes:nil error:nil];
    task_writeTaskMetadata(taskInfo);

    taskCurrentProductIndex = 0;
    taskSearchSubmitted = NO;
    taskAwaitingSearchPage = NO;
    taskSearchTransitionStartedAt = 0;
    taskSearchTransitionSubmitAttempted = NO;
    taskStopReasonDetail = nil;
    taskLastSearchPageSignature = nil;
    taskConsecutiveSameSearchPageCount = 0;
    taskSearchRecoveryAttemptCount = 0;
    taskSearchTransitionRetryCount = 0;
    taskSalesSortApplied = NO;
    taskPriceFilterApplied = NO;
    taskFilterWarning = nil;
    taskSalesSortAttempted = NO;
    taskSalesSortPendingConfirmation = NO;
    taskSalesSortObservedListReload = NO;
    taskPriceFilterPanelOpened = NO;
    taskPriceFilterInputsFilled = NO;
    taskPriceFilterPendingConfirmation = NO;
    taskPriceFilterObservedListReload = NO;
    taskPriceFilterAttempted = NO;
    taskPriceFilterApplyAttemptCount = 0;
    taskPriceFilterConfirmAttemptCount = 0;
    taskSearchFilterSettleUntil = 0;
    taskSalesSortRequestedAt = 0;
    taskPriceFilterConfirmedAt = 0;
    taskSalesSortBaselineVisibleCount = 0;
    taskSalesSortBaselineSignature = nil;
    taskPriceFilterBaselineVisibleCount = 0;
    taskPriceFilterBaselineSignature = nil;
    taskCurrentState = @"running";
    taskCurrentError = nil;
    NSDictionary *licenseStatus = task_refreshLicenseStatus(@"collect", YES);
    if (![[licenseStatus objectForKey:@"valid"] boolValue]) {
        NSString *taskError = task_licenseTaskErrorForStatus(licenseStatus);
        NSString *detail = [[NSString stringWithFormat:@"%@", licenseStatus[@"detail"] ?: @""]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        debug_writeTaskDump(@"license_blocked", [NSString stringWithFormat:
            @"task_id=%@\nerror_code=%@\nstatus=%@\ndetail=%@\ncurrent_binding_id=%@\nbound_binding_id=%@",
            taskCurrentId ?: @"",
            licenseStatus[@"error_code"] ?: @"",
            licenseStatus[@"status"] ?: @"",
            detail,
            licenseStatus[@"current_binding_id"] ?: @"",
            licenseStatus[@"bound_binding_id"] ?: @""]);
        task_finishActiveTask(@"failed", taskError);
        return;
    }

    taskAutoSearchActive = YES;
    task_writeCurrentTaskStatus();
    
    // 启动 Socket 实时通信服务端
    static BOOL socketStarted = NO;
    if (!socketStarted) {
        realtime_startServer(9999);
        socketStarted = YES;
    }
    
    // 通知客户端任务开始
    int targetCount = [taskInfo[@"target_count"] intValue];
    realtime_notifyTaskStarted(keyword, targetCount);
    
    task_runAutoSearchStep();
}

static void task_pollCommandInbox(void) {
    initializePDDStoragePaths();
    task_ensureCommandDirectories();
    NSDictionary *licenseStatus = task_refreshLicenseStatus(@"collect", NO);

    if (task_shouldStopCurrentTask()) {
        task_finishActiveTask(@"stopped", @"stop_requested");
        return;
    }

    if (task_hasActiveTask()) {
        if (![[licenseStatus objectForKey:@"valid"] boolValue]) {
            NSString *taskError = task_licenseTaskErrorForStatus(licenseStatus);
            NSString *detail = [[NSString stringWithFormat:@"%@", licenseStatus[@"detail"] ?: @""]
                stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            debug_writeTaskDump(@"license_runtime_blocked", [NSString stringWithFormat:
                @"task_id=%@\nerror_code=%@\nstatus=%@\ndetail=%@",
                taskCurrentId ?: @"",
                licenseStatus[@"error_code"] ?: @"",
                licenseStatus[@"status"] ?: @"",
                detail]);
            task_finishActiveTask(@"failed", taskError);
            return;
        }
        task_writeCurrentTaskStatus();
        return;
    }

    NSArray *files = [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:taskCommandInboxDir error:nil] sortedArrayUsingSelector:@selector(compare:)];
    for (NSString *fileName in files) {
        if (![fileName hasSuffix:@".json"]) {
            continue;
        }
        NSString *filePath = [taskCommandInboxDir stringByAppendingPathComponent:fileName];
        NSDictionary *taskInfo = task_readJSON(filePath);
        [[NSFileManager defaultManager] removeItemAtPath:filePath error:nil];
        if (![taskInfo isKindOfClass:[NSDictionary class]]) {
            continue;
        }
        NSString *action = [NSString stringWithFormat:@"%@", taskInfo[@"action"] ?: @""];
        if ([action isEqualToString:@"collect"]) {
            task_startCollectTask(taskInfo);
        } else if ([action isEqualToString:@"stop"]) {
            NSString *taskId = [NSString stringWithFormat:@"%@", taskInfo[@"task_id"] ?: @""];
            NSString *stopPath = task_stopFlagPath(taskId);
            if (stopPath.length > 0) {
                [@"stop" writeToFile:stopPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            }
        }
        break;
    }

    task_writeCurrentStatus();
}

static BOOL ui_scrollVisibleScrollableView(id view) {
    if (!view) {
        return NO;
    }
    @try {
        Class UIScrollViewClass = NSClassFromString(@"UIScrollView");
        if (UIScrollViewClass && [view isKindOfClass:UIScrollViewClass]) {
            SEL isHiddenSel = NSSelectorFromString(@"isHidden");
            BOOL isHidden = ((BOOL (*)(id, SEL))objc_msgSend)(view, isHiddenSel);
            CGRect frame = ((CGRect (*)(id, SEL))objc_msgSend)(view, NSSelectorFromString(@"frame"));
            if (!isHidden && frame.size.width > 0 && frame.size.height > 0) {
                CGSize contentSize = ((CGSize (*)(id, SEL))objc_msgSend)(view, NSSelectorFromString(@"contentSize"));
                CGPoint currentOffset = ((CGPoint (*)(id, SEL))objc_msgSend)(view, NSSelectorFromString(@"contentOffset"));
                CGFloat maxOffsetY = MAX(contentSize.height - frame.size.height, 0);
                if (maxOffsetY > currentOffset.y + 10.0) {
                    CGFloat nextOffsetY = MIN(maxOffsetY, currentOffset.y + frame.size.height * 0.75);
                    CGPoint nextOffset = CGPointMake(0, nextOffsetY);
                    ((void (*)(id, SEL, CGPoint, BOOL))objc_msgSend)(view, NSSelectorFromString(@"setContentOffset:animated:"), nextOffset, YES);
                    return YES;
                }
            }
        }

        NSArray *subviews = ((NSArray * (*)(id, SEL))objc_msgSend)(view, NSSelectorFromString(@"subviews"));
        for (id subview in subviews) {
            if (ui_scrollVisibleScrollableView(subview)) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }
    return NO;
}

static BOOL ui_scrollSearchResultsPage(void) {
    id rootVC = ui_currentRootViewController();
    if (!rootVC) {
        taskConsecutiveFailureCount++;
        return NO;
    }
    id rootView = ((id (*)(id, SEL))objc_msgSend)(rootVC, NSSelectorFromString(@"view"));
    if (!rootView) {
        taskConsecutiveFailureCount++;
        return NO;
    }
    BOOL didScroll = ui_scrollVisibleScrollableView(rootView);
    if (!didScroll) {
        taskConsecutiveFailureCount++;
    }
    return didScroll;
}
