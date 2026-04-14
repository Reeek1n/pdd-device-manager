// 执行搜索
static void ui_performSearch(NSString *keyword) {
    @try {
        NSLog(@"[AutoSearch] 开始搜索关键词: %@", keyword);

        id currentRootView = ui_currentRootView();
        if (!currentRootView) {
            NSLog(@"[AutoSearch] 警告：当前没有可用 rootView");
            debug_writeTaskDump(@"search_no_root", [NSString stringWithFormat:@"keyword=%@", keyword ?: @""]);
            return;
        }

        if (ui_trySubmitSearchFromRootView(currentRootView, keyword)) {
            NSLog(@"[AutoSearch] 已触发搜索提交");
            taskSearchSubmitted = YES;
            taskAwaitingSearchPage = NO;
            taskSearchTransitionStartedAt = 0;
            taskSearchTransitionSubmitAttempted = NO;
            taskSearchTransitionRetryCount = 0;
            return;
        }

        id searchEntryView = ui_bestSearchEntryView(currentRootView);
        BOOL didActivateSearchEntry = NO;
        if (searchEntryView) {
            didActivateSearchEntry = ui_tryActivateSearchEntry(searchEntryView);
        }
        if (searchEntryView && didActivateSearchEntry) {
            NSLog(@"[AutoSearch] 已点击搜索入口，等待输入框出现: %@ %@", NSStringFromClass([searchEntryView class]), ui_viewMetadataText(searchEntryView));
            taskAwaitingSearchPage = YES;
            taskSearchTransitionStartedAt = [[NSDate date] timeIntervalSince1970];
            taskSearchTransitionSubmitAttempted = NO;
            return;
        }

        if (searchEntryView) {
            debug_writeTaskDump(@"search_entry_activation_failed", [NSString stringWithFormat:@"keyword=%@\n%@\n%@",
                keyword ?: @"",
                debug_searchEntryText(searchEntryView),
                debug_searchHitTestText()]);
        }

        NSLog(@"[AutoSearch] 警告：没有找到可用搜索控件");
        debug_writeTaskDump(@"search_no_control", [NSString stringWithFormat:@"keyword=%@\n%@",
            keyword ?: @"",
            debug_searchHitTestText()]);
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 搜索失败: %@", exception);
        debug_writeTaskDump(@"search_exception", [NSString stringWithFormat:@"keyword=%@\nexception=%@\n%@",
            keyword ?: @"",
            exception,
            debug_searchHitTestText()]);
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
    }
}

// 点击商品：优先通过列表选中，再回退到 cell 上的事件/手势
static BOOL ui_clickProductAtIndex(NSInteger index) {
    @try {
        NSLog(@"[AutoSearch] 点击第 %ld 个商品", (long)index);

        id currentRootView = ui_currentRootView();
        BOOL didTriggerNavigation = NO;
        if (currentRootView) {
            id resultsListView = ui_bestResultsListView(currentRootView);
            if (resultsListView) {
                didTriggerNavigation = ui_selectProductFromListView(resultsListView, index);
            }

            if (!didTriggerNavigation) {
                didTriggerNavigation = ui_tapVisibleCellLikeView(currentRootView, index);
            }

            if (!didTriggerNavigation) {
                didTriggerNavigation = ui_tryTapProductAtIndexByPoint(index);
            }
        }

        taskNeedsBackNavigation = didTriggerNavigation;
        taskDetailNavigationStartedAt = didTriggerNavigation ? [[NSDate date] timeIntervalSince1970] : 0;
        taskObservedDetailSignalAt = 0;
        taskDetailCollectionStage = 0;
        taskDetailCollectionLastActionAt = 0;
        taskDetailAttemptCounted = NO;
        taskSavedCountBeforeDetailNavigation = taskSavedCount;
        if (didTriggerNavigation) {
            hook_installNSURLSessionDelegateHooks();
            if (task_hasActiveTask()) {
                taskConsecutiveFailureCount = 0;
                task_writeCurrentTaskStatus();
            }
            return YES;
        }

        NSLog(@"[AutoSearch] 未找到可点击的商品项");
        debug_writeTaskDump(@"click_no_target", [NSString stringWithFormat:@"index=%ld\n%@",
            (long)index,
            debug_productHitTestText(index)]);
        task_clearPendingDetailNavigation();
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
        return NO;
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 点击商品失败: %@", exception);
        task_clearPendingDetailNavigation();
        debug_writeTaskDump(@"click_exception", [NSString stringWithFormat:@"index=%ld\nexception=%@\n%@",
            (long)index,
            exception,
            debug_productHitTestText(index)]);
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
        return NO;
    }
}

static BOOL ui_performBackNavigation(BOOL requirePendingNavigation) {
    @try {
        NSLog(@"[AutoSearch] 返回上一页");

        if (requirePendingNavigation && task_hasActiveTask() && !taskNeedsBackNavigation) {
            NSLog(@"[AutoSearch] 上一步未触发有效导航，跳过返回");
            return NO;
        }

        task_clearPendingDetailNavigation();

        id currentRootViewController = ui_currentRootViewController();
        SEL navigationControllerSel = NSSelectorFromString(@"navigationController");
        Class UINavigationControllerClass = NSClassFromString(@"UINavigationController");
        id navigationController = (UINavigationControllerClass && [currentRootViewController isKindOfClass:UINavigationControllerClass]) ?
            currentRootViewController :
            ((id (*)(id, SEL))objc_msgSend)(currentRootViewController, navigationControllerSel);

        BOOL didNavigateBack = NO;
        if (navigationController) {
            SEL viewControllersSel = NSSelectorFromString(@"viewControllers");
            NSArray *navigationStack = [navigationController respondsToSelector:viewControllersSel] ? ((NSArray * (*)(id, SEL))objc_msgSend)(navigationController, viewControllersSel) : nil;
            SEL popViewControllerSel = NSSelectorFromString(@"popViewControllerAnimated:");
            if (navigationStack.count > 1 && [navigationController respondsToSelector:popViewControllerSel]) {
                ((void (*)(id, SEL, BOOL))objc_msgSend)(navigationController, popViewControllerSel, YES);
                didNavigateBack = YES;
            }
        }

        if (!didNavigateBack && currentRootViewController && [currentRootViewController respondsToSelector:NSSelectorFromString(@"popViewControllerAnimated:")]) {
            SEL popViewControllerSel = NSSelectorFromString(@"popViewControllerAnimated:");
            ((void (*)(id, SEL, BOOL))objc_msgSend)(currentRootViewController, popViewControllerSel, YES);
            didNavigateBack = YES;
        }

        if (!didNavigateBack && currentRootViewController) {
            SEL presentingViewControllerSel = NSSelectorFromString(@"presentingViewController");
            id presentingViewController = [currentRootViewController respondsToSelector:presentingViewControllerSel] ? ((id (*)(id, SEL))objc_msgSend)(currentRootViewController, presentingViewControllerSel) : nil;
            SEL dismissSel = NSSelectorFromString(@"dismissViewControllerAnimated:completion:");
            if (presentingViewController && [currentRootViewController respondsToSelector:dismissSel]) {
                ((void (*)(id, SEL, BOOL, id))objc_msgSend)(currentRootViewController, dismissSel, YES, nil);
                didNavigateBack = YES;
            }
        }

        if (!didNavigateBack) {
            id currentRootView = ui_currentRootView();
            didNavigateBack = ui_tryTapBackButton(currentRootView);
        }

        if (!didNavigateBack && task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
        return didNavigateBack;
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 返回失败: %@", exception);
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
        return NO;
    }
}

// 返回上一页
static void ui_goBack() {
    ui_performBackNavigation(YES);
}

static void ui_forceGoBack(void) {
    ui_performBackNavigation(NO);
}

// 自动搜索流程控制
static void task_runAutoSearchStep(void) {
    @try {
        if (!taskAutoSearchActive) return;
        if (task_hasActiveTask()) {
            if (task_failIfRiskControlDetected(@"auto_step")) {
                return;
            }
            if (task_shouldStopCurrentTask()) {
                task_finishActiveTask(@"stopped", @"stop_requested");
                return;
            }
            if (taskSavedCount >= taskTargetCount) {
                task_finishActiveTask(@"completed", nil);
                return;
            }
            if (taskConsecutiveFailureCount >= 5) {
                task_finishActiveTask(@"failed", @"too_many_failures");
                return;
            }
            if (!taskNeedsBackNavigation && taskAttemptedCount >= task_detailEntryBudget()) {
                taskStopReasonDetail = @"detail_entry_budget_exhausted";
                task_finishActiveTask(@"failed", @"no_new_goods");
                return;
            }
            if (!taskNeedsBackNavigation && taskConsecutiveNoNewRounds >= kTaskMaxNoNewScrollRounds) {
                taskStopReasonDetail = @"scroll_budget_exhausted";
                task_finishActiveTask(@"failed", @"no_new_goods");
                return;
            }
            if (!taskNeedsBackNavigation && taskConsecutiveUnknownOrClickFailures >= kTaskMaxConsecutiveUnknownOrClickFailures) {
                taskStopReasonDetail = @"unknown_or_click_failure_budget_exhausted";
                task_finishActiveTask(@"failed", @"no_new_goods");
                return;
            }

            NSString *taskKeyword = taskCurrentKeyword ?: @"";
            if (taskKeyword.length == 0) {
                task_finishActiveTask(@"failed", @"missing_keyword");
                return;
            }

            NSTimeInterval currentTimestamp = [[NSDate date] timeIntervalSince1970];
            id currentRootView = ui_currentRootView();
            BOOL rootIsSearchPage = ui_rootViewLooksLikeSearchPage(currentRootView);
            BOOL searchResultsReady = ui_rootViewHasReadySearchResults(currentRootView);
            NSInteger visibleResultCount = ui_visibleSearchResultCount(currentRootView);
            NSString *currentSearchPageSignature = searchResultsReady ? task_currentSearchPageSignature(currentRootView) : @"";

            if (taskSalesSortPendingConfirmation) {
                if (!searchResultsReady) {
                    taskSalesSortObservedListReload = YES;
                }

                id salesButton = ui_bestSearchToolbarButtonWithKeywords(currentRootView, @[@"销量"], NO);
                NSString *salesStateTrace = nil;
                BOOL salesLooksSelected = ui_searchToolbarItemLooksSelected(salesButton, &salesStateTrace);
                BOOL signatureChanged = NO;
                if (searchResultsReady && currentSearchPageSignature.length > 0) {
                    if (taskSalesSortBaselineSignature.length == 0) {
                        signatureChanged = YES;
                    } else {
                        signatureChanged = ![currentSearchPageSignature isEqualToString:taskSalesSortBaselineSignature];
                    }
                }
                BOOL visibleCountChanged = searchResultsReady &&
                    taskSalesSortBaselineVisibleCount > 0 &&
                    visibleResultCount > 0 &&
                    visibleResultCount != taskSalesSortBaselineVisibleCount;
                NSTimeInterval salesWaitSeconds = taskSalesSortRequestedAt > 0 ?
                    (currentTimestamp - taskSalesSortRequestedAt) :
                    0;
                BOOL didConfirmSalesSort = searchResultsReady &&
                    (signatureChanged ||
                     visibleCountChanged ||
                     (taskSalesSortObservedListReload && (currentSearchPageSignature.length > 0 || salesLooksSelected)));
                BOOL observedReload = taskSalesSortObservedListReload;
                NSInteger baselineVisibleCount = taskSalesSortBaselineVisibleCount;
                NSString *baselineSignature = [taskSalesSortBaselineSignature copy] ?: @"";

                if (didConfirmSalesSort) {
                    taskSalesSortApplied = YES;
                    taskSalesSortPendingConfirmation = NO;
                    taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                    debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                        @"step=sales_sort_confirm\nrequested_sort=%@\nconfirmed=true\nsignature_changed=%@\nvisible_count_changed=%@\nobserved_reload=%@\nlooks_selected=%@\nwait_seconds=%.2f\nbaseline_signature=%@\ncurrent_signature=%@\nbaseline_visible_count=%ld\ncurrent_visible_count=%ld\n%@",
                        taskRequestedSortBy ?: @"",
                        signatureChanged ? @"true" : @"false",
                        visibleCountChanged ? @"true" : @"false",
                        observedReload ? @"true" : @"false",
                        salesLooksSelected ? @"true" : @"false",
                        salesWaitSeconds,
                        baselineSignature,
                        currentSearchPageSignature ?: @"",
                        (long)baselineVisibleCount,
                        (long)visibleResultCount,
                        salesStateTrace ?: @""]);
                    taskSalesSortObservedListReload = NO;
                    taskSalesSortRequestedAt = 0;
                    taskSalesSortBaselineVisibleCount = 0;
                    taskSalesSortBaselineSignature = nil;
                    task_writeCurrentTaskStatus();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (salesWaitSeconds < 6.0) {
                    NSTimeInterval waitDelay = searchResultsReady ? 0.8 : 0.6;
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:waitDelay target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                taskSalesSortPendingConfirmation = NO;
                task_appendFilterWarning(@"sales_sort_no_list_refresh");
                debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                    @"step=sales_sort_confirm\nwarning=sales_sort_no_list_refresh\nrequested_sort=%@\nwait_seconds=%.2f\nobserved_reload=%@\nlooks_selected=%@\nbaseline_signature=%@\ncurrent_signature=%@\nbaseline_visible_count=%ld\ncurrent_visible_count=%ld\n%@",
                    taskRequestedSortBy ?: @"",
                    salesWaitSeconds,
                    observedReload ? @"true" : @"false",
                    salesLooksSelected ? @"true" : @"false",
                    baselineSignature,
                    currentSearchPageSignature ?: @"",
                    (long)baselineVisibleCount,
                    (long)visibleResultCount,
                    salesStateTrace ?: @""]);
                taskSalesSortObservedListReload = NO;
                taskSalesSortRequestedAt = 0;
                taskSalesSortBaselineVisibleCount = 0;
                taskSalesSortBaselineSignature = nil;
                task_writeCurrentTaskStatus();
            }

            if (taskPriceFilterPendingConfirmation) {
                if (!searchResultsReady) {
                    taskPriceFilterObservedListReload = YES;
                }

                BOOL signatureChanged = NO;
                if (searchResultsReady && currentSearchPageSignature.length > 0) {
                    if (taskPriceFilterBaselineSignature.length == 0) {
                        signatureChanged = YES;
                    } else {
                        signatureChanged = ![currentSearchPageSignature isEqualToString:taskPriceFilterBaselineSignature];
                    }
                }
                BOOL visibleCountChanged = searchResultsReady &&
                    taskPriceFilterBaselineVisibleCount > 0 &&
                    visibleResultCount > 0 &&
                    visibleResultCount != taskPriceFilterBaselineVisibleCount;
                NSTimeInterval priceWaitSeconds = taskPriceFilterConfirmedAt > 0 ?
                    (currentTimestamp - taskPriceFilterConfirmedAt) :
                    0;
                BOOL panelStillOpen = ui_searchPageHasOpenPriceFilterPanel(currentRootView);
                BOOL observedReload = taskPriceFilterObservedListReload;
                NSInteger baselineVisibleCount = taskPriceFilterBaselineVisibleCount;
                NSString *baselineSignature = [taskPriceFilterBaselineSignature copy] ?: @"";
                BOOL didConfirmPriceFilter = searchResultsReady &&
                    !panelStillOpen &&
                    (signatureChanged ||
                     visibleCountChanged ||
                     observedReload);

                if (didConfirmPriceFilter) {
                    taskPriceFilterApplied = YES;
                    taskPriceFilterPendingConfirmation = NO;
                    taskPriceFilterAttempted = YES;
                    taskPriceFilterInputsFilled = NO;
                    taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                    debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                        @"step=confirm_price_filter_result\nprice_min=%@\nprice_max=%@\nconfirmed=true\nsignature_changed=%@\nvisible_count_changed=%@\nobserved_reload=%@\npanel_still_open=%@\nwait_seconds=%.2f\nbaseline_signature=%@\ncurrent_signature=%@\nbaseline_visible_count=%ld\ncurrent_visible_count=%ld",
                        taskRequestedPriceMin ?: @"",
                        taskRequestedPriceMax ?: @"",
                        signatureChanged ? @"true" : @"false",
                        visibleCountChanged ? @"true" : @"false",
                        observedReload ? @"true" : @"false",
                        panelStillOpen ? @"true" : @"false",
                        priceWaitSeconds,
                        baselineSignature,
                        currentSearchPageSignature ?: @"",
                        (long)baselineVisibleCount,
                        (long)visibleResultCount]);
                    taskPriceFilterObservedListReload = NO;
                    taskPriceFilterConfirmedAt = 0;
                    taskPriceFilterBaselineVisibleCount = 0;
                    taskPriceFilterBaselineSignature = nil;
                    task_writeCurrentTaskStatus();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (priceWaitSeconds < 6.0) {
                    NSTimeInterval waitDelay = searchResultsReady ? 0.8 : 0.6;
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:waitDelay target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                taskPriceFilterPendingConfirmation = NO;
                taskPriceFilterAttempted = YES;
                taskPriceFilterInputsFilled = NO;
                task_appendFilterWarning(@"price_filter_no_list_refresh");
                debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                    @"step=confirm_price_filter_result\nwarning=price_filter_no_list_refresh\nprice_min=%@\nprice_max=%@\nobserved_reload=%@\npanel_still_open=%@\nwait_seconds=%.2f\nbaseline_signature=%@\ncurrent_signature=%@\nbaseline_visible_count=%ld\ncurrent_visible_count=%ld",
                    taskRequestedPriceMin ?: @"",
                    taskRequestedPriceMax ?: @"",
                    observedReload ? @"true" : @"false",
                    panelStillOpen ? @"true" : @"false",
                    priceWaitSeconds,
                    baselineSignature,
                    currentSearchPageSignature ?: @"",
                    (long)baselineVisibleCount,
                    (long)visibleResultCount]);
                taskPriceFilterObservedListReload = NO;
                taskPriceFilterConfirmedAt = 0;
                taskPriceFilterBaselineVisibleCount = 0;
                taskPriceFilterBaselineSignature = nil;
                task_writeCurrentTaskStatus();
            }

            if (taskNeedsBackNavigation) {
                BOOL didObserveDetailSignal = (taskObservedDetailSignalAt > 0 &&
                    taskObservedDetailSignalAt >= taskDetailNavigationStartedAt);
                BOOL detailSavedThisAttempt = taskSavedCount > taskSavedCountBeforeDetailNavigation;
                NSTimeInterval secondsSinceDetailNavigation = taskDetailNavigationStartedAt > 0 ?
                    (currentTimestamp - taskDetailNavigationStartedAt) :
                    0;
                NSTimeInterval secondsSinceLastDetailAction = taskDetailCollectionLastActionAt > 0 ?
                    (currentTimestamp - taskDetailCollectionLastActionAt) :
                    999.0;

                if (detailSavedThisAttempt && searchResultsReady) {
                    task_clearPendingDetailNavigation();
                    task_writeCurrentTaskStatus();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (detailSavedThisAttempt && !searchResultsReady && secondsSinceDetailNavigation >= 1.0) {
                    debug_writeTaskDump(@"detail_saved_wait_back", [NSString stringWithFormat:
                        @"seconds_since_navigation=%.2f\nroot_is_detail=%@\non_search_page=%@\nhas_ready_search_results=%@",
                        secondsSinceDetailNavigation,
                        ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false",
                        rootIsSearchPage ? @"true" : @"false",
                        searchResultsReady ? @"true" : @"false"]);
                    ui_goBack();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (!didObserveDetailSignal && secondsSinceDetailNavigation < 4.0) {
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.5 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (!didObserveDetailSignal && searchResultsReady) {
                    task_clearPendingDetailNavigation();
                    task_writeCurrentTaskStatus();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (didObserveDetailSignal && taskDetailCollectionStage == 0 &&
                    secondsSinceDetailNavigation >= 1.0 && secondsSinceLastDetailAction >= 1.0) {
                    BOOL didSaveAtCurrentStage = capture_trySaveGoodsDetailFromView(currentRootView);
                    if (!task_hasActiveTask()) {
                        return;
                    }
                    if (taskLastDetailCandidateWasDuplicate) {
                        debug_writeTaskDump(@"detail_duplicate_back", [NSString stringWithFormat:
                            @"stage=%ld\nseconds_since_navigation=%.2f\nroot_is_detail=%@",
                            (long)taskDetailCollectionStage,
                            secondsSinceDetailNavigation,
                            ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                        taskLastDetailCandidateWasDuplicate = NO;
                        ui_goBack();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.5 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }
                    detailSavedThisAttempt = taskSavedCount > taskSavedCountBeforeDetailNavigation;
                    if (didSaveAtCurrentStage || detailSavedThisAttempt) {
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }

                    BOOL didScrollDetailPage = ui_tryScrollDetailPage(currentRootView);
                    taskDetailCollectionStage = 1;
                    taskDetailCollectionLastActionAt = currentTimestamp;
                    debug_writeTaskDump(@"detail_stage_scroll", [NSString stringWithFormat:
                        @"seconds_since_navigation=%.2f\ndid_scroll=%@\nroot_is_detail=%@",
                        secondsSinceDetailNavigation,
                        didScrollDetailPage ? @"true" : @"false",
                        ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (didObserveDetailSignal && taskDetailCollectionStage == 1 &&
                    secondsSinceDetailNavigation >= 4.0 && secondsSinceLastDetailAction >= 1.5) {
                    BOOL didSaveAtCurrentStage = capture_trySaveGoodsDetailFromView(currentRootView);
                    if (!task_hasActiveTask()) {
                        return;
                    }
                    if (taskLastDetailCandidateWasDuplicate) {
                        debug_writeTaskDump(@"detail_duplicate_back", [NSString stringWithFormat:
                            @"stage=%ld\nseconds_since_navigation=%.2f\nroot_is_detail=%@",
                            (long)taskDetailCollectionStage,
                            secondsSinceDetailNavigation,
                            ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                        taskLastDetailCandidateWasDuplicate = NO;
                        ui_goBack();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.5 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }
                    detailSavedThisAttempt = taskSavedCount > taskSavedCountBeforeDetailNavigation;
                    if (didSaveAtCurrentStage || detailSavedThisAttempt) {
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }

                    BOOL didTapPurchaseButton = ui_tryTapDetailPurchaseButton(currentRootView);
                    taskDetailCollectionStage = 2;
                    taskDetailCollectionLastActionAt = currentTimestamp;
                    debug_writeTaskDump(@"detail_stage_purchase", [NSString stringWithFormat:
                        @"seconds_since_navigation=%.2f\ndid_tap_purchase=%@\nroot_is_detail=%@",
                        secondsSinceDetailNavigation,
                        didTapPurchaseButton ? @"true" : @"false",
                        ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (didObserveDetailSignal && taskDetailCollectionStage == 2 &&
                    secondsSinceDetailNavigation >= 6.0 && secondsSinceLastDetailAction >= 1.0) {
                    BOOL didSaveAtCurrentStage = capture_trySaveGoodsDetailFromView(currentRootView);
                    if (!task_hasActiveTask()) {
                        return;
                    }
                    if (taskLastDetailCandidateWasDuplicate) {
                        debug_writeTaskDump(@"detail_duplicate_back", [NSString stringWithFormat:
                            @"stage=%ld\nseconds_since_navigation=%.2f\nroot_is_detail=%@",
                            (long)taskDetailCollectionStage,
                            secondsSinceDetailNavigation,
                            ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                        taskLastDetailCandidateWasDuplicate = NO;
                        ui_goBack();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.5 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }
                    taskDetailCollectionStage = 3;
                    taskDetailCollectionLastActionAt = currentTimestamp;
                    debug_writeTaskDump(@"detail_stage_collect", [NSString stringWithFormat:
                        @"seconds_since_navigation=%.2f\ndid_save=%@\nroot_is_detail=%@",
                        secondsSinceDetailNavigation,
                        didSaveAtCurrentStage ? @"true" : @"false",
                        ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false"]);
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:(didSaveAtCurrentStage ? 1.0 : 1.5) target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (secondsSinceDetailNavigation >= 6.0 || detailSavedThisAttempt || (didObserveDetailSignal && taskDetailCollectionStage >= 3 && secondsSinceDetailNavigation >= 6.0)) {
                    if (task_failIfRiskControlDetected(@"before_back_navigation")) {
                        return;
                    }
                    if (!detailSavedThisAttempt) {
                        capture_trySaveGoodsDetailFromView(currentRootView);
                        if (!task_hasActiveTask()) {
                            return;
                        }
                        detailSavedThisAttempt = taskSavedCount > taskSavedCountBeforeDetailNavigation;
                    }
                    if (!detailSavedThisAttempt) {
                        debug_writeTaskDump(@"detail_no_goods", [NSString stringWithFormat:
                            @"seconds_since_navigation=%.2f\nobserved_detail_signal=%@\nvisible_search_result_count=%ld\non_search_page=%@\nhas_ready_search_results=%@",
                            secondsSinceDetailNavigation,
                            didObserveDetailSignal ? @"true" : @"false",
                            (long)visibleResultCount,
                            rootIsSearchPage ? @"true" : @"false",
                            searchResultsReady ? @"true" : @"false"]);
                    }
                    task_finalizeCurrentDetailAttemptIfNeeded();
                    ui_goBack();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.5 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.5 target:[NSBlockOperation blockOperationWithBlock:^{
                    task_runAutoSearchStep();
                }] selector:@selector(main) userInfo:nil repeats:NO];
                return;
            }

            if (!taskSearchSubmitted) {
                if (ui_rootViewLooksLikeGoodsDetailPage(currentRootView) && taskSearchRecoveryAttemptCount < 1) {
                    taskSearchRecoveryAttemptCount++;
                    debug_writeTaskDump(@"search_recover_back", [NSString stringWithFormat:
                        @"recovery_attempt=%ld\nroot_is_detail=%@\nroot_is_search=%@",
                        (long)taskSearchRecoveryAttemptCount,
                        ui_rootViewLooksLikeGoodsDetailPage(currentRootView) ? @"true" : @"false",
                        rootIsSearchPage ? @"true" : @"false"]);
                    ui_forceGoBack();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (taskAwaitingSearchPage) {
                    NSTimeInterval secondsSinceSearchTransition = taskSearchTransitionStartedAt > 0 ?
                        (currentTimestamp - taskSearchTransitionStartedAt) :
                        0;
                    BOOL rootLooksLikeHomePage = ui_rootViewLooksLikeHomePage(currentRootView);

                    if (!taskSearchTransitionSubmitAttempted &&
                        !rootLooksLikeHomePage &&
                        secondsSinceSearchTransition >= kTaskSearchTransitionSettleDelaySeconds) {
                        BOOL didSubmitKeyword = ui_trySubmitSearchFromRootView(currentRootView, taskKeyword);
                        taskSearchTransitionSubmitAttempted = YES;
                        if (didSubmitKeyword) {
                            NSLog(@"[AutoSearch] 搜索页已稳定，完成关键词提交");
                            taskSearchSubmitted = YES;
                            taskAwaitingSearchPage = NO;
                            taskSearchTransitionStartedAt = 0;
                            taskSearchTransitionSubmitAttempted = NO;
                            taskSearchTransitionRetryCount = 0;
                            task_writeCurrentTaskStatus();
                            taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:kTaskSearchTransitionPostSubmitDelaySeconds target:[NSBlockOperation blockOperationWithBlock:^{
                                task_runAutoSearchStep();
                            }] selector:@selector(main) userInfo:nil repeats:NO];
                            return;
                        }
                    }

                    if (secondsSinceSearchTransition >= kTaskSearchTransitionTimeoutSeconds) {
                        id searchEntryView = ui_bestSearchEntryView(currentRootView);
                        debug_writeTaskDump(@"search_transition_timeout", [NSString stringWithFormat:
                            @"keyword=%@\nseconds_since_transition=%.2f\nsubmit_attempted=%@\nretry_count=%ld\nroot_is_home=%@\nroot_is_search=%@\n%@\n%@",
                            taskKeyword,
                            secondsSinceSearchTransition,
                            taskSearchTransitionSubmitAttempted ? @"true" : @"false",
                            (long)taskSearchTransitionRetryCount,
                            rootLooksLikeHomePage ? @"true" : @"false",
                            rootIsSearchPage ? @"true" : @"false",
                            searchEntryView ? debug_searchEntryText(searchEntryView) : @"entry=<nil>",
                            debug_searchHitTestText()]);
                        taskAwaitingSearchPage = NO;
                        taskSearchTransitionStartedAt = 0;
                        taskSearchTransitionSubmitAttempted = NO;

                        if (taskSearchTransitionRetryCount < 1) {
                            taskSearchTransitionRetryCount++;
                            ui_performSearch(taskKeyword);
                            task_writeCurrentTaskStatus();
                            NSTimeInterval nextDelay = taskAwaitingSearchPage ? kTaskSearchTransitionFirstPollDelaySeconds : 1.5;
                            taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:nextDelay target:[NSBlockOperation blockOperationWithBlock:^{
                                task_runAutoSearchStep();
                            }] selector:@selector(main) userInfo:nil repeats:NO];
                            return;
                        }

                        taskFailedCount++;
                        taskConsecutiveFailureCount++;
                        task_writeCurrentTaskStatus();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.5 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }

                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:kTaskSearchTransitionPollDelaySeconds target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                taskSearchResultsPendingStartedAt = 0;
                ui_performSearch(taskKeyword);
                task_writeCurrentTaskStatus();
                NSTimeInterval nextDelay = taskAwaitingSearchPage ? kTaskSearchTransitionFirstPollDelaySeconds : 3.0;
                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:nextDelay target:[NSBlockOperation blockOperationWithBlock:^{
                    task_runAutoSearchStep();
                }] selector:@selector(main) userInfo:nil repeats:NO];
                return;
            }

            if (!searchResultsReady) {
                if (taskSearchResultsPendingStartedAt <= 0) {
                    taskSearchResultsPendingStartedAt = currentTimestamp;
                }

                if ((currentTimestamp - taskSearchResultsPendingStartedAt) >= 8.0) {
                    NSLog(@"[AutoSearch] 搜索结果页已提交，但列表长时间未就绪，重新发起搜索");
                    taskSearchSubmitted = NO;
                    taskAwaitingSearchPage = NO;
                    taskSearchTransitionStartedAt = 0;
                    taskSearchTransitionSubmitAttempted = NO;
                    taskSearchTransitionRetryCount = 0;
                    taskSearchResultsPendingStartedAt = 0;
                    taskFailedCount++;
                    taskConsecutiveFailureCount++;
                    task_writeCurrentTaskStatus();
                }

                NSTimeInterval nextDelay = rootIsSearchPage ? 1.5 : 2.0;
                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:nextDelay target:[NSBlockOperation blockOperationWithBlock:^{
                    task_runAutoSearchStep();
                }] selector:@selector(main) userInfo:nil repeats:NO];
                return;
            }

            taskSearchResultsPendingStartedAt = 0;
            taskSearchRecoveryAttemptCount = 0;
            taskSearchTransitionRetryCount = 0;
            taskSearchTransitionSubmitAttempted = NO;
            taskSearchTransitionStartedAt = 0;
            taskAwaitingSearchPage = NO;

            if (taskCurrentProductIndex == 0) {
                BOOL needsSalesSort = [taskRequestedSortBy isEqualToString:@"sales"];
                BOOL needsPriceFilter = taskRequestedPriceMin.length > 0 || taskRequestedPriceMax.length > 0;

                if (taskSearchFilterSettleUntil > currentTimestamp) {
                    NSTimeInterval waitDelay = MAX(taskSearchFilterSettleUntil - currentTimestamp, 0.6);
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:waitDelay target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (needsSalesSort && !taskSalesSortAttempted) {
                    NSString *filterTrace = nil;
                    taskSalesSortAttempted = YES;
                    BOOL didApplySalesSort = ui_tryApplySalesSortOnSearchPage(currentRootView, &filterTrace);
                    if (didApplySalesSort) {
                        taskSalesSortPendingConfirmation = YES;
                        taskSalesSortObservedListReload = NO;
                        taskSalesSortRequestedAt = currentTimestamp;
                        taskSalesSortBaselineVisibleCount = visibleResultCount;
                        taskSalesSortBaselineSignature = [currentSearchPageSignature copy];
                        taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                        debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                            @"step=sales_sort_click\nrequested_sort=%@\napplied=false\nbaseline_signature=%@\nbaseline_visible_count=%ld\n%@",
                            taskRequestedSortBy ?: @"",
                            currentSearchPageSignature ?: @"",
                            (long)visibleResultCount,
                            filterTrace ?: @""]);
                        task_writeCurrentTaskStatus();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }

                    taskSalesSortRequestedAt = 0;
                    taskSalesSortBaselineVisibleCount = 0;
                    taskSalesSortBaselineSignature = nil;
                    task_appendFilterWarning(@"sales_sort_not_applied");
                    debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                        @"step=sales_sort\nwarning=sales_sort_not_applied\nrequested_sort=%@\n%@",
                        taskRequestedSortBy ?: @"",
                        filterTrace ?: @""]);
                    task_writeCurrentTaskStatus();
                }

                if (needsPriceFilter && !taskPriceFilterAttempted) {
                    if (!taskPriceFilterPanelOpened) {
                        NSString *filterTrace = nil;
                        BOOL didOpenFilterPanel = ui_tryOpenPriceFilterPanelOnSearchPage(currentRootView, &filterTrace);
                        if (didOpenFilterPanel) {
                            taskPriceFilterPanelOpened = YES;
                            taskPriceFilterInputsFilled = NO;
                            taskPriceFilterPendingConfirmation = NO;
                            taskPriceFilterObservedListReload = NO;
                            taskPriceFilterConfirmedAt = 0;
                            taskPriceFilterBaselineVisibleCount = 0;
                            taskPriceFilterBaselineSignature = nil;
                            taskPriceFilterApplyAttemptCount = 0;
                            taskPriceFilterConfirmAttemptCount = 0;
                            taskSearchFilterSettleUntil = currentTimestamp + 1.6;
                            debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                                @"step=open_price_filter_panel\nprice_min=%@\nprice_max=%@\nopened=%@\n%@",
                                taskRequestedPriceMin ?: @"",
                                taskRequestedPriceMax ?: @"",
                                didOpenFilterPanel ? @"true" : @"false",
                                filterTrace ?: @""]);
                            task_writeCurrentTaskStatus();
                            taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.6 target:[NSBlockOperation blockOperationWithBlock:^{
                                task_runAutoSearchStep();
                            }] selector:@selector(main) userInfo:nil repeats:NO];
                            return;
                        }

                        taskPriceFilterAttempted = YES;
                        task_appendFilterWarning(@"price_filter_panel_not_found");
                        debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                            @"step=open_price_filter_panel\nwarning=price_filter_panel_not_found\nprice_min=%@\nprice_max=%@\n%@",
                            taskRequestedPriceMin ?: @"",
                            taskRequestedPriceMax ?: @"",
                            filterTrace ?: @""]);
                        task_writeCurrentTaskStatus();
                    } else {
                        NSString *filterTrace = nil;
                        NSString *filterWarning = nil;
                        if (!taskPriceFilterInputsFilled) {
                            taskPriceFilterApplyAttemptCount++;
                            BOOL didFillPriceFilter = ui_tryFillPriceFilterInputsOnSearchPage(
                                currentRootView,
                                taskRequestedPriceMin ?: @"",
                                taskRequestedPriceMax ?: @"",
                                &filterTrace,
                                &filterWarning
                            );
                            if (didFillPriceFilter) {
                                taskPriceFilterInputsFilled = YES;
                                taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                                debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                                    @"step=fill_price_filter_inputs\nprice_min=%@\nprice_max=%@\nfilled=true\napply_attempt=%ld\n%@",
                                    taskRequestedPriceMin ?: @"",
                                    taskRequestedPriceMax ?: @"",
                                    (long)taskPriceFilterApplyAttemptCount,
                                    filterTrace ?: @""]);
                                task_writeCurrentTaskStatus();
                                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                                    task_runAutoSearchStep();
                                }] selector:@selector(main) userInfo:nil repeats:NO];
                                return;
                            }

                            if ([filterWarning isEqualToString:@"price_filter_inputs_not_found"] &&
                                taskPriceFilterApplyAttemptCount < 2) {
                                debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                                    @"step=fill_price_filter_inputs_retry_wait\nprice_min=%@\nprice_max=%@\napply_attempt=%ld\nwarning=%@\n%@",
                                    taskRequestedPriceMin ?: @"",
                                    taskRequestedPriceMax ?: @"",
                                    (long)taskPriceFilterApplyAttemptCount,
                                    filterWarning ?: @"",
                                    filterTrace ?: @""]);
                                taskSearchFilterSettleUntil = currentTimestamp + 1.2;
                                task_writeCurrentTaskStatus();
                                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.2 target:[NSBlockOperation blockOperationWithBlock:^{
                                    task_runAutoSearchStep();
                                }] selector:@selector(main) userInfo:nil repeats:NO];
                                return;
                            }

                            taskPriceFilterAttempted = YES;
                            task_appendFilterWarning(filterWarning.length > 0 ? filterWarning : @"price_filter_not_applied");
                            taskPriceFilterPanelOpened = NO;
                            debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                                @"step=fill_price_filter_inputs\nwarning=%@\nprice_min=%@\nprice_max=%@\napply_attempt=%ld\n%@",
                                filterWarning.length > 0 ? filterWarning : @"price_filter_not_applied",
                                taskRequestedPriceMin ?: @"",
                                taskRequestedPriceMax ?: @"",
                                (long)taskPriceFilterApplyAttemptCount,
                                filterTrace ?: @""]);
                            task_writeCurrentTaskStatus();
                        } else {
                            taskPriceFilterConfirmAttemptCount++;
                            BOOL didConfirmPriceFilter = ui_tryConfirmPriceFilterOnSearchPage(
                                currentRootView,
                                &filterTrace,
                                &filterWarning
                            );
                            if (didConfirmPriceFilter) {
                                taskPriceFilterPendingConfirmation = YES;
                                taskPriceFilterPanelOpened = NO;
                                taskPriceFilterConfirmedAt = currentTimestamp;
                                taskPriceFilterBaselineVisibleCount = visibleResultCount;
                                taskPriceFilterBaselineSignature = [currentSearchPageSignature copy];
                                taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                                debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                                    @"step=confirm_price_filter\nprice_min=%@\nprice_max=%@\nconfirmed_tap=true\napply_attempt=%ld\nconfirm_attempt=%ld\nbaseline_signature=%@\nbaseline_visible_count=%ld\n%@",
                                    taskRequestedPriceMin ?: @"",
                                    taskRequestedPriceMax ?: @"",
                                    (long)taskPriceFilterApplyAttemptCount,
                                    (long)taskPriceFilterConfirmAttemptCount,
                                    currentSearchPageSignature ?: @"",
                                    (long)visibleResultCount,
                                    filterTrace ?: @""]);
                                task_writeCurrentTaskStatus();
                                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                                    task_runAutoSearchStep();
                                }] selector:@selector(main) userInfo:nil repeats:NO];
                                return;
                            }

                            if ([filterWarning isEqualToString:@"price_filter_keyboard_not_dismissed"] &&
                                taskPriceFilterConfirmAttemptCount < 3) {
                                debug_writeTaskDump(@"search_filter_trace", [NSString stringWithFormat:
                                    @"step=confirm_price_filter_retry_wait\nprice_min=%@\nprice_max=%@\napply_attempt=%ld\nconfirm_attempt=%ld\nwarning=%@\n%@",
                                    taskRequestedPriceMin ?: @"",
                                    taskRequestedPriceMax ?: @"",
                                    (long)taskPriceFilterApplyAttemptCount,
                                    (long)taskPriceFilterConfirmAttemptCount,
                                    filterWarning ?: @"",
                                    filterTrace ?: @""]);
                                taskSearchFilterSettleUntil = currentTimestamp + 0.8;
                                task_writeCurrentTaskStatus();
                                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                                    task_runAutoSearchStep();
                                }] selector:@selector(main) userInfo:nil repeats:NO];
                                return;
                            }

                            NSString *finalWarning = filterWarning;
                            if ([finalWarning isEqualToString:@"price_filter_keyboard_not_dismissed"]) {
                                finalWarning = @"price_filter_confirm_not_found";
                            }
                            taskPriceFilterAttempted = YES;
                            task_appendFilterWarning(finalWarning.length > 0 ? finalWarning : @"price_filter_confirm_not_found");
                            taskPriceFilterPanelOpened = NO;
                            debug_writeTaskDump(@"search_filter_warning", [NSString stringWithFormat:
                                @"step=confirm_price_filter\nwarning=%@\nprice_min=%@\nprice_max=%@\napply_attempt=%ld\nconfirm_attempt=%ld\n%@",
                                finalWarning.length > 0 ? finalWarning : @"price_filter_confirm_not_found",
                                taskRequestedPriceMin ?: @"",
                                taskRequestedPriceMax ?: @"",
                                (long)taskPriceFilterApplyAttemptCount,
                                (long)taskPriceFilterConfirmAttemptCount,
                                filterTrace ?: @""]);
                            task_writeCurrentTaskStatus();
                        }
                    }
                }

                taskCurrentProductIndex = 1;
                taskConsecutiveSameSearchPageCount = 0;
                taskLastSearchPageSignature = nil;
                taskSavedCountAtLastScroll = taskSavedCount;
                task_writeCurrentTaskStatus();
                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 target:[NSBlockOperation blockOperationWithBlock:^{
                    task_runAutoSearchStep();
                }] selector:@selector(main) userInfo:nil repeats:NO];
                return;
            }

            NSArray *visibleCells = ui_visibleSearchCells(currentRootView);
            NSSet *seenGoodsIdsSnapshot = task_seenGoodsIdsSnapshot();
            NSInteger maxVisibleIndex = visibleCells.count > 0 ? (NSInteger)visibleCells.count : (visibleResultCount > 0 ? visibleResultCount : 10);
            if (taskCurrentProductIndex <= maxVisibleIndex) {
                NSInteger targetIndex = taskCurrentProductIndex - 1;
                id targetCell = targetIndex >= 0 && targetIndex < (NSInteger)visibleCells.count ? visibleCells[targetIndex] : nil;
                NSString *resolvedGoodsId = targetCell ? task_resolvedGoodsIdForSearchCell(targetCell) : @"";
                if (resolvedGoodsId.length > 0 && [seenGoodsIdsSnapshot containsObject:resolvedGoodsId]) {
                    taskDuplicateSkipCount++;
                    taskConsecutiveUnknownOrClickFailures = 0;
                    debug_writeTaskDump(@"search_duplicate_skip", [NSString stringWithFormat:
                        @"index=%ld\ngoods_id=%@\nvisible_count=%ld",
                        (long)targetIndex,
                        resolvedGoodsId,
                        (long)visibleCells.count]);
                    taskCurrentProductIndex++;
                    task_writeCurrentTaskStatus();
                    taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                        task_runAutoSearchStep();
                    }] selector:@selector(main) userInfo:nil repeats:NO];
                    return;
                }

                if (resolvedGoodsId.length == 0) {
                    NSString *skipReason = nil;
                    if (targetCell && task_shouldSkipUnresolvedSearchCell(targetCell, &skipReason)) {
                        taskDuplicateSkipCount++;
                        taskConsecutiveUnknownOrClickFailures = 0;
                        debug_writeTaskDump(@"search_non_goods_skip", [NSString stringWithFormat:
                            @"index=%ld\nreason=%@\nvisible_count=%ld\ncell_text=%@",
                            (long)targetIndex,
                            skipReason ?: @"",
                            (long)visibleCells.count,
                            ui_visibleTextSampleFromView(targetCell) ?: @""]);
                        taskCurrentProductIndex++;
                        task_writeCurrentTaskStatus();
                        taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:0.8 target:[NSBlockOperation blockOperationWithBlock:^{
                            task_runAutoSearchStep();
                        }] selector:@selector(main) userInfo:nil repeats:NO];
                        return;
                    }

                    taskUnknownEntryCount++;
                    taskConsecutiveUnknownOrClickFailures++;
                    debug_writeTaskDump(@"search_unresolved_enter", [NSString stringWithFormat:
                        @"index=%ld\nvisible_count=%ld\ncell_text=%@\nimage_urls=%@",
                        (long)targetIndex,
                        (long)visibleCells.count,
                        targetCell ? (ui_visibleTextSampleFromView(targetCell) ?: @"") : @"",
                        targetCell ? ([ui_collectImageURLs(targetCell) componentsJoinedByString:@"\n"] ?: @"") : @""]);
                } else {
                    taskConsecutiveUnknownOrClickFailures = 0;
                }

                BOOL didTrigger = ui_clickProductAtIndex(targetIndex);
                if (!didTrigger) {
                    taskConsecutiveUnknownOrClickFailures++;
                }
                taskCurrentProductIndex++;
                task_writeCurrentTaskStatus();
                NSTimeInterval nextDelay = didTrigger ? 2.0 : 1.0;
                taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:nextDelay target:[NSBlockOperation blockOperationWithBlock:^{
                    task_runAutoSearchStep();
                }] selector:@selector(main) userInfo:nil repeats:NO];
                return;
            }

            if (taskSavedCount == taskSavedCountAtLastScroll) {
                taskConsecutiveNoNewRounds++;
            } else {
                taskConsecutiveNoNewRounds = 0;
            }
            NSString *pageSignature = task_currentSearchPageSignature(currentRootView);
            if (pageSignature.length > 0 && [pageSignature isEqualToString:taskLastSearchPageSignature ?: @""]) {
                taskConsecutiveSameSearchPageCount++;
                if (taskConsecutiveSameSearchPageCount >= 1) {
                    debug_writeTaskDump(@"search_stuck_same_page", [NSString stringWithFormat:
                        @"signature=%@\nconsecutive_same_page_count=%ld\nvisible_count=%ld",
                        pageSignature,
                        (long)taskConsecutiveSameSearchPageCount,
                        (long)visibleCells.count]);
                }
            } else {
                taskConsecutiveSameSearchPageCount = 0;
            }
            taskLastSearchPageSignature = [pageSignature copy];
            taskSavedCountAtLastScroll = taskSavedCount;
            taskCurrentProductIndex = 1;
            BOOL didScrollSearchPage = ui_scrollSearchResultsPage();
            if (!didScrollSearchPage) {
                taskConsecutiveUnknownOrClickFailures++;
            }
            debug_writeTaskDump(@"search_scroll_round", [NSString stringWithFormat:
                @"visible_count=%ld\nsaved_count=%ld\nno_new_scroll_rounds=%ld\nduplicate_skip_count=%ld\nsignature=%@\ndid_scroll=%@",
                (long)visibleCells.count,
                (long)taskSavedCount,
                (long)taskConsecutiveNoNewRounds,
                (long)taskDuplicateSkipCount,
                pageSignature ?: @"",
                didScrollSearchPage ? @"true" : @"false"]);
            task_writeCurrentTaskStatus();
            taskAutoSearchTimer = [NSTimer scheduledTimerWithTimeInterval:3.0 target:[NSBlockOperation blockOperationWithBlock:^{
                task_runAutoSearchStep();
            }] selector:@selector(main) userInfo:nil repeats:NO];
            return;
        }
        
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 流程控制出错: %@", exception);
        if (task_hasActiveTask()) {
            taskFailedCount++;
            taskConsecutiveFailureCount++;
            task_writeCurrentTaskStatus();
        }
    }
}
