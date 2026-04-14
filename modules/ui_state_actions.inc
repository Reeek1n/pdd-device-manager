#pragma mark - 自动搜索功能

// 查找所有指定类型的视图
static NSArray* ui_findAllViewsOfClass(id view, NSString *className) {
    NSMutableArray *results = [NSMutableArray array];
    @try {
        if (!view) return results;
        
        NSString *viewClassName = NSStringFromClass([view class]);
        if ([viewClassName isEqualToString:className] || [viewClassName containsString:className]) {
            [results addObject:view];
        }
        
        SEL subviewsSel = NSSelectorFromString(@"subviews");
        NSArray *subviews = ((NSArray * (*)(id, SEL))objc_msgSend)(view, subviewsSel);
        for (id subview in subviews) {
            NSArray *found = ui_findAllViewsOfClass(subview, className);
            [results addObjectsFromArray:found];
        }
    } @catch (NSException *exception) {
    }
    return results;
}

static void ui_collectDescendantViews(id view, NSMutableArray *results) {
    if (!view || !results) {
        return;
    }

    @try {
        SEL subviewsSel = NSSelectorFromString(@"subviews");
        if (![view respondsToSelector:subviewsSel]) {
            return;
        }

        NSArray *subviews = ((NSArray * (*)(id, SEL))objc_msgSend)(view, subviewsSel);
        for (id subview in subviews) {
            if (!subview) {
                continue;
            }
            [results addObject:subview];
            ui_collectDescendantViews(subview, results);
        }
    } @catch (NSException *exception) {
    }
}

static NSArray *ui_allDescendantViews(id view) {
    NSMutableArray *results = [NSMutableArray array];
    ui_collectDescendantViews(view, results);
    return results;
}

static BOOL ui_viewIsVisible(id view) {
    if (!view) {
        return NO;
    }

    @try {
        SEL isHiddenSel = NSSelectorFromString(@"isHidden");
        if ([view respondsToSelector:isHiddenSel] &&
            ((BOOL (*)(id, SEL))objc_msgSend)(view, isHiddenSel)) {
            return NO;
        }

        SEL alphaSel = NSSelectorFromString(@"alpha");
        if ([view respondsToSelector:alphaSel] &&
            ((CGFloat (*)(id, SEL))objc_msgSend)(view, alphaSel) < 0.01) {
            return NO;
        }

        SEL windowSel = NSSelectorFromString(@"window");
        if ([view respondsToSelector:windowSel] &&
            ((id (*)(id, SEL))objc_msgSend)(view, windowSel) == nil) {
            return NO;
        }

        CGRect frame = ui_viewFrameInScreen(view);
        if (frame.size.width < 4.0 || frame.size.height < 4.0) {
            return NO;
        }

        Class UIScreenClass = NSClassFromString(@"UIScreen");
        id screen = ((id (*)(Class, SEL))objc_msgSend)(UIScreenClass, NSSelectorFromString(@"mainScreen"));
        CGRect screenBounds = screen ? ((CGRect (*)(id, SEL))objc_msgSend)(screen, NSSelectorFromString(@"bounds")) : CGRectZero;
        if (!CGRectIsEmpty(screenBounds) && !CGRectIntersectsRect(frame, screenBounds)) {
            return NO;
        }
        return YES;
    } @catch (NSException *exception) {
        return NO;
    }
}

static CGRect ui_viewFrameInScreen(id view) {
    if (!view) {
        return CGRectZero;
    }

    @try {
        SEL boundsSel = NSSelectorFromString(@"bounds");
        SEL convertRectSel = NSSelectorFromString(@"convertRect:toView:");
        if ([view respondsToSelector:boundsSel] && [view respondsToSelector:convertRectSel]) {
            CGRect bounds = ((CGRect (*)(id, SEL))objc_msgSend)(view, boundsSel);
            return ((CGRect (*)(id, SEL, CGRect, id))objc_msgSend)(view, convertRectSel, bounds, nil);
        }

        SEL frameSel = NSSelectorFromString(@"frame");
        if ([view respondsToSelector:frameSel]) {
            return ((CGRect (*)(id, SEL))objc_msgSend)(view, frameSel);
        }
    } @catch (NSException *exception) {
    }

    return CGRectZero;
}

static NSString *ui_viewMetadataText(id view) {
    if (!view) {
        return @"";
    }

    @try {
        NSMutableArray *parts = [NSMutableArray array];
        NSArray *selectorNames = @[
            @"currentTitle",
            @"title",
            @"text",
            @"placeholder",
            @"accessibilityLabel",
            @"accessibilityValue",
            @"accessibilityIdentifier"
        ];
        for (NSString *selectorName in selectorNames) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![view respondsToSelector:selector]) {
                continue;
            }
            id value = ((id (*)(id, SEL))objc_msgSend)(view, selector);
            if ([value isKindOfClass:[NSString class]]) {
                NSString *text = [(NSString *)value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
                if (text.length > 0) {
                    [parts addObject:text];
                }
            }
        }
        return [parts componentsJoinedByString:@" "];
    } @catch (NSException *exception) {
        return @"";
    }
}

static BOOL ui_stringLooksLikeSearch(NSString *value) {
    if (value.length == 0) {
        return NO;
    }
    return ([value containsString:@"搜索"] ||
            [value localizedCaseInsensitiveContainsString:@"search"] ||
            [value containsString:@"查找"] ||
            [value containsString:@"商品"] ||
            [value containsString:@"关键词"]);
}

static BOOL ui_stringLooksLikeRiskControl(NSString *value) {
    if (value.length == 0) {
        return NO;
    }

    NSString *text = [value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (text.length == 0) {
        return NO;
    }

    return ([text containsString:@"系统繁忙"] ||
            [text containsString:@"请稍后重试"] ||
            [text containsString:@"访问受限"] ||
            [text containsString:@"操作过于频繁"] ||
            [text containsString:@"安全验证"] ||
            [text containsString:@"请完成安全验证"] ||
            [text containsString:@"请完成验证"]);
}

static NSString *ui_findRiskControlMessageInView(id rootView) {
    if (!rootView) {
        return @"";
    }

    @try {
        NSMutableArray *candidates = [NSMutableArray arrayWithObject:rootView];
        [candidates addObjectsFromArray:ui_allDescendantViews(rootView)];

        NSMutableArray *pageTexts = [NSMutableArray array];
        for (id candidate in candidates) {
            if (!candidate) {
                continue;
            }
            if (candidate != rootView && !ui_viewIsVisible(candidate)) {
                continue;
            }

            NSString *metadata = ui_viewMetadataText(candidate);
            if (metadata.length == 0) {
                continue;
            }
            if (ui_stringLooksLikeRiskControl(metadata)) {
                return metadata;
            }
            if (pageTexts.count < 80) {
                [pageTexts addObject:metadata];
            }
        }

        NSString *joinedText = [pageTexts componentsJoinedByString:@" | "];
        if (ui_stringLooksLikeRiskControl(joinedText)) {
            return joinedText;
        }
    } @catch (NSException *exception) {
    }

    return @"";
}

static BOOL task_failIfRiskControlDetected(NSString *context) {
    if (!task_hasActiveTask()) {
        return NO;
    }

    NSString *riskMessage = ui_findRiskControlMessageInView(ui_currentRootView());
    if (riskMessage.length == 0) {
        return NO;
    }

    NSLog(@"[AutoSearch] 检测到拼多多风控页: %@", riskMessage);
    debug_writeTaskDump(@"risk_control_blocked", [NSString stringWithFormat:@"context=%@\nmessage=%@",
        context ?: @"auto_search",
        riskMessage]);
    task_finishActiveTask(@"failed", @"risk_control_blocked");
    return YES;
}

static id ui_currentRootView(void) {
    id rootVC = ui_currentRootViewController();
    if (!rootVC) {
        return nil;
    }
    return ((id (*)(id, SEL))objc_msgSend)(rootVC, NSSelectorFromString(@"view"));
}

static id ui_bestEditableSearchView(id searchView) {
    if (!searchView) {
        return nil;
    }

    @try {
        NSArray *selectorNames = @[
            @"searchTextField",
            @"textField",
            @"inputTextField",
            @"keywordTextField"
        ];
        for (NSString *selectorName in selectorNames) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![searchView respondsToSelector:selector]) {
                continue;
            }
            id candidate = ((id (*)(id, SEL))objc_msgSend)(searchView, selector);
            if (candidate) {
                return candidate;
            }
        }

        NSArray *kvcKeys = @[
            @"_searchField",
            @"searchField",
            @"_textField",
            @"textField",
            @"inputView"
        ];
        for (NSString *key in kvcKeys) {
            @try {
                id candidate = [searchView valueForKey:key];
                if (candidate) {
                    return candidate;
                }
            } @catch (NSException *exception) {
            }
        }
    } @catch (NSException *exception) {
    }

    NSMutableArray *candidates = [NSMutableArray arrayWithObject:searchView];
    [candidates addObjectsFromArray:ui_allDescendantViews(searchView)];

    CGRect containerFrame = ui_viewFrameInScreen(searchView);
    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;

    for (id candidate in candidates) {
        if (!candidate) {
            continue;
        }

        NSString *className = NSStringFromClass([candidate class]) ?: @"";
        CGRect frame = ui_viewFrameInScreen(candidate);
        NSString *metadata = ui_viewMetadataText(candidate);
        BOOL isVisible = ui_viewIsVisible(candidate);
        BOOL respondsSetText = [candidate respondsToSelector:NSSelectorFromString(@"setText:")];
        BOOL respondsInsertText = [candidate respondsToSelector:NSSelectorFromString(@"insertText:")];
        BOOL respondsBecomeFirstResponder = [candidate respondsToSelector:NSSelectorFromString(@"becomeFirstResponder")];

        NSInteger score = 0;
        if (candidate == searchView) {
            score += 20;
        }
        if (respondsSetText) {
            score += 220;
        }
        if (respondsInsertText) {
            score += 160;
        }
        if (respondsBecomeFirstResponder) {
            score += 50;
        }
        if (isVisible) {
            score += 70;
        }
        if ([className containsString:@"TextField"] ||
            [className containsString:@"Search"] ||
            [className containsString:@"Input"] ||
            [className containsString:@"Edit"]) {
            score += 80;
        }
        if (metadata.length > 0) {
            score += 10;
        }
        if (!CGRectIsEmpty(frame) && !CGRectIsEmpty(containerFrame) && CGRectIntersectsRect(frame, containerFrame)) {
            score += 20;
        }
        if (frame.size.width > 30.0 && frame.size.height > 16.0) {
            score += 10;
        }

        if (score > bestScore) {
            bestScore = score;
            bestView = candidate;
        }
    }

    return bestView;
}

static id ui_resolveDirectTextInputView(id view) {
    if (!view) {
        return nil;
    }

    id current = view;
    for (NSInteger depth = 0; depth < 4; depth++) {
        id next = nil;
        NSArray *selectorNames = @[
            @"textField",
            @"searchTextField",
            @"inputField"
        ];
        for (NSString *selectorName in selectorNames) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![current respondsToSelector:selector]) {
                continue;
            }
            @try {
                next = ((id (*)(id, SEL))objc_msgSend)(current, selector);
            } @catch (NSException *exception) {
                next = nil;
            }
            if (next && next != current) {
                break;
            }
        }

        if (!next) {
            NSArray *kvcKeys = @[
                @"_textField",
                @"textField",
                @"_inputField",
                @"inputField"
            ];
            for (NSString *key in kvcKeys) {
                @try {
                    next = [current valueForKey:key];
                } @catch (NSException *exception) {
                    next = nil;
                }
                if (next && next != current) {
                    break;
                }
            }
        }

        if (!next || next == current) {
            break;
        }

        current = next;
    }

    return current;
}

static NSString *ui_currentTextValue(id view) {
    if (!view) {
        return @"";
    }

    @try {
        NSArray *selectorNames = @[
            @"text",
            @"currentText",
            @"searchText"
        ];
        for (NSString *selectorName in selectorNames) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![view respondsToSelector:selector]) {
                continue;
            }
            id value = ((id (*)(id, SEL))objc_msgSend)(view, selector);
            if ([value isKindOfClass:[NSString class]]) {
                return value;
            }
            if ([value respondsToSelector:NSSelectorFromString(@"string")]) {
                return ((NSString * (*)(id, SEL))objc_msgSend)(value, NSSelectorFromString(@"string")) ?: @"";
            }
        }

        NSArray *kvcKeys = @[
            @"text",
            @"_text",
            @"searchText"
        ];
        for (NSString *key in kvcKeys) {
            @try {
                id value = [view valueForKey:key];
                if ([value isKindOfClass:[NSString class]]) {
                    return value;
                }
            } @catch (NSException *exception) {
            }
        }
    } @catch (NSException *exception) {
    }

    return @"";
}

static NSString *ui_viewSummaryText(id view) {
    if (!view) {
        return @"<nil>";
    }

    CGRect frame = ui_viewFrameInScreen(view);
    return [NSString stringWithFormat:@"%@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\"",
        NSStringFromClass([view class]) ?: @"<unknown>",
        frame.origin.x,
        frame.origin.y,
        frame.size.width,
        frame.size.height,
        ui_viewMetadataText(view) ?: @""];
}

static BOOL ui_rootViewLooksLikeSearchPage(id rootView) {
    if (!rootView) {
        return NO;
    }

    @try {
        NSArray *searchPageClasses = @[
            @"PDDSearchPageView",
            @"PDDSearchDetailCollectionView",
            @"PDDSearchDetailCollectionViewContentCollectionView",
            @"PDDSearchDominoLegoView",
            @"PDDSearchPlaceHolderView",
            @"PDDSearchDetailSectionHeadCell"
        ];
        for (NSString *className in searchPageClasses) {
            if (ui_findAllViewsOfClass(rootView, className).count > 0) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_rootViewHasReadySearchResults(id rootView) {
    return ui_visibleSearchResultCount(rootView) > 0;
}

static void task_clearPendingDetailNavigation(void) {
    taskNeedsBackNavigation = NO;
    taskDetailNavigationStartedAt = 0;
    taskObservedDetailSignalAt = 0;
    taskSavedCountBeforeDetailNavigation = taskSavedCount;
    taskDetailCollectionStage = 0;
    taskDetailCollectionLastActionAt = 0;
    taskDetailAttemptCounted = NO;
    taskLastDetailCandidateWasDuplicate = NO;
}

static NSInteger ui_visibleSearchResultCount(id rootView) {
    if (!rootView || !ui_rootViewLooksLikeSearchPage(rootView)) {
        return 0;
    }

    @try {
        return (NSInteger)ui_visibleSearchCells(rootView).count;
    } @catch (NSException *exception) {
        return 0;
    }
}

static BOOL ui_rootViewLooksLikeGoodsDetailPage(id rootView) {
    if (!rootView) {
        return NO;
    }

    @try {
        NSArray *detailPageClasses = @[
            @"PDDGoodsDetailContentView",
            @"PDDGoodsDetailCollectionView",
            @"PDDGoodsDetailBottomBar",
            @"PDDGoodsDetailNavigationBar"
        ];
        for (NSString *className in detailPageClasses) {
            if (ui_findAllViewsOfClass(rootView, className).count > 0) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_tryScrollDetailPage(id rootView) {
    if (!rootView) {
        return NO;
    }

    @try {
        NSArray *preferredViews = ui_findAllViewsOfClass(rootView, @"PDDGoodsDetailCollectionView");
        for (id candidate in preferredViews) {
            if (!ui_viewIsVisible(candidate)) {
                continue;
            }
            if (ui_scrollVisibleScrollableView(candidate)) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return ui_scrollVisibleScrollableView(rootView);
}

static BOOL ui_tryTapDetailPurchaseButton(id rootView) {
    if (!rootView) {
        return NO;
    }

    NSMutableArray *candidates = [NSMutableArray array];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"BottomButtonView")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIButton")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIControl")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UILabel")];

    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;
    for (id candidate in candidates) {
        if (!ui_viewIsVisible(candidate)) {
            continue;
        }
        NSString *metadata = ui_viewMetadataText(candidate) ?: @"";
        if (![metadata containsString:@"购买"]) {
            continue;
        }
        CGRect frame = ui_viewFrameInScreen(candidate);
        if (frame.origin.y < 560.0) {
            continue;
        }

        NSInteger score = 0;
        NSString *className = NSStringFromClass([candidate class]) ?: @"";
        if ([metadata containsString:@"单独购买"]) score += 220;
        if ([metadata containsString:@"免拼购买"]) score += 180;
        if ([className containsString:@"BottomButtonView"]) score += 120;
        if (ui_viewHasAncestorClassFragment(candidate, @"BottomButtonView")) score += 80;
        if ([metadata containsString:@"¥"]) score += 20;

        if (score > bestScore) {
            bestScore = score;
            bestView = candidate;
        }
    }

    if (!bestView) {
        return NO;
    }
    return ui_sendTapActionToView(bestView);
}

static NSArray * __attribute__((unused)) ui_collectImageURLs(id rootView) {
    if (!rootView) {
        return @[];
    }

    NSMutableArray *results = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    NSMutableArray *views = [NSMutableArray arrayWithObject:rootView];
    [views addObjectsFromArray:ui_allDescendantViews(rootView)];

    for (id candidate in views) {
        if (!candidate || (candidate != rootView && !ui_viewIsVisible(candidate))) {
            continue;
        }
        NSString *className = NSStringFromClass([candidate class]) ?: @"";
        if (![className containsString:@"Image"]) {
            continue;
        }

        NSArray *selectorNames = @[@"imageURL", @"sd_imageURL", @"webImageURL", @"url", @"URL"];
        for (NSString *selectorName in selectorNames) {
            @try {
                SEL selector = NSSelectorFromString(selectorName);
                if (![candidate respondsToSelector:selector]) {
                    continue;
                }
                id value = ((id (*)(id, SEL))objc_msgSend)(candidate, selector);
                NSString *urlText = nil;
                if ([value isKindOfClass:[NSString class]]) {
                    urlText = (NSString *)value;
                } else if ([value respondsToSelector:NSSelectorFromString(@"absoluteString")]) {
                    urlText = ((NSString * (*)(id, SEL))objc_msgSend)(value, NSSelectorFromString(@"absoluteString"));
                }
                if (urlText.length > 0 && ([urlText hasPrefix:@"http://"] || [urlText hasPrefix:@"https://"]) && ![seen containsObject:urlText]) {
                    [seen addObject:urlText];
                    [results addObject:urlText];
                }
            } @catch (NSException *exception) {
            }
        }
    }

    return results;
}

static void capture_cacheSearchSnapshot(id jsonObject, NSString *goodsId) {
    if (!task_hasActiveTask() || goodsId.length == 0 || [goodsId hasPrefix:@"unknown"] || ![jsonObject isKindOfClass:[NSDictionary class]]) {
        return;
    }

    @try {
        NSDictionary *dict = (NSDictionary *)jsonObject;
        NSString *pageName = [dict[@"page_name"] isKindOfClass:[NSString class]] ? dict[@"page_name"] : @"";
        if (![pageName isEqualToString:@"search_result"]) {
            return;
        }

        NSMutableDictionary *snapshot = [NSMutableDictionary dictionary];
        NSString *title = [dict[@"title_impr"] isKindOfClass:[NSString class]] ? dict[@"title_impr"] : @"";
        NSString *itemImg = [dict[@"goods_img_url"] isKindOfClass:[NSString class]] ? dict[@"goods_img_url"] : @"";
        NSString *priceText = [dict[@"price"] isKindOfClass:[NSString class]] ? dict[@"price"] : ([dict[@"price"] respondsToSelector:@selector(stringValue)] ? [dict[@"price"] stringValue] : @"");
        NSString *priceDesc = [dict[@"price_desc"] isKindOfClass:[NSString class]] ? dict[@"price_desc"] : @"";
        NSString *longPricePrefix = [dict[@"long_price_prefix"] isKindOfClass:[NSString class]] ? dict[@"long_price_prefix"] : @"";

        if (title.length > 0) snapshot[@"title"] = title;
        if (itemImg.length > 0) snapshot[@"itemImg"] = itemImg;
        if (priceText.length > 0) snapshot[@"price"] = priceText;
        if (priceDesc.length > 0) snapshot[@"priceDesc"] = priceDesc;
        if (longPricePrefix.length > 0) snapshot[@"longPricePrefix"] = longPricePrefix;

        id pSearch = dict[@"p_search"];
        NSString *pSearchText = [pSearch isKindOfClass:[NSString class]] ? (NSString *)pSearch : @"";
        if (pSearchText.length > 0) {
            NSData *jsonData = [pSearchText dataUsingEncoding:NSUTF8StringEncoding];
            if (jsonData) {
                NSError *error = nil;
                id pSearchObj = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&error];
                if (!error && [pSearchObj isKindOfClass:[NSDictionary class]]) {
                    NSDictionary *pSearchDict = (NSDictionary *)pSearchObj;
                    if (pSearchDict[@"cat_id"]) {
                        snapshot[@"categoryId"] = [NSString stringWithFormat:@"%@", pSearchDict[@"cat_id"]];
                    }
                }
            }
        }

        @synchronized(task_sharedStateLock()) {
            if (!taskSearchPayloadByGoodsId) {
                taskSearchPayloadByGoodsId = [NSMutableDictionary dictionary];
            }
            taskSearchPayloadByGoodsId[goodsId] = [snapshot copy];
        }
    } @catch (NSException *exception) {
    }
}

static BOOL capture_trySaveGoodsDetailFromView(id rootView) {
    if (!task_hasActiveTask() || !rootView || !ui_rootViewLooksLikeGoodsDetailPage(rootView)) {
        return NO;
    }

    NSString *visibleText = ui_visibleTextSampleFromView(rootView) ?: @"";
    NSString *visibleTextSnippet = [[visibleText stringByReplacingOccurrencesOfString:@"\n" withString:@" "]
        stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
    if (visibleTextSnippet.length > 220) {
        visibleTextSnippet = [[visibleTextSnippet substringToIndex:220] stringByAppendingString:@"..."];
    }

    NSDictionary *matchedCandidate = capture_bestRecentCandidateForVisibleText(visibleText);
    if ([matchedCandidate isKindOfClass:[NSDictionary class]]) {
        NSString *goodsId = [matchedCandidate[@"goodsId"] isKindOfClass:[NSString class]] ? matchedCandidate[@"goodsId"] : @"";
        NSString *source = [matchedCandidate[@"source"] isKindOfClass:[NSString class]] ? matchedCandidate[@"source"] : @"";
        NSString *url = [matchedCandidate[@"url"] isKindOfClass:[NSString class]] ? matchedCandidate[@"url"] : @"";
        debug_writeTaskDump(@"detail_candidate_match", [NSString stringWithFormat:
            @"goods_id=%@\nsource=%@\nscore=%@\nurl=%@\nvisible_text=%@",
            goodsId,
            source,
            matchedCandidate[@"score"] ?: @0,
            url,
            visibleTextSnippet]);
        task_finalizeCurrentDetailAttemptIfNeeded();
        return capture_trySaveTaskCandidate(matchedCandidate);
    }

    NSDictionary *runtimeCandidate = capture_runtimeFallbackCandidateForCurrentPage();
    if ([runtimeCandidate isKindOfClass:[NSDictionary class]]) {
        NSString *goodsId = [runtimeCandidate[@"goodsId"] isKindOfClass:[NSString class]] ? runtimeCandidate[@"goodsId"] : @"";
        NSString *source = [runtimeCandidate[@"source"] isKindOfClass:[NSString class]] ? runtimeCandidate[@"source"] : @"RuntimeGraph";
        NSString *url = [runtimeCandidate[@"url"] isKindOfClass:[NSString class]] ? runtimeCandidate[@"url"] : @"";
        debug_writeTaskDump(@"detail_runtime_fallback", [NSString stringWithFormat:
            @"goods_id=%@\nsource=%@\nurl=%@\nvisible_text=%@\nroot_is_detail=%@",
            goodsId,
            source,
            url,
            visibleTextSnippet,
            ui_rootViewLooksLikeGoodsDetailPage(rootView) ? @"true" : @"false"]);
        task_finalizeCurrentDetailAttemptIfNeeded();
        return capture_trySaveTaskCandidate(runtimeCandidate);
    }

    NSUInteger cachedCandidateCount = 0;
    if ([taskRecentCandidates isKindOfClass:[NSMutableArray class]]) {
        @synchronized (taskRecentCandidates) {
            capture_pruneRecentCandidatesLocked();
            cachedCandidateCount = taskRecentCandidates.count;
        }
    }

    debug_writeTaskDump(@"detail_candidate_miss", [NSString stringWithFormat:
        @"root_class=%@\nroot_is_detail=%@\nlast_goods_id=%@\nvisible_text=%@\ncached_candidate_count=%lu",
        NSStringFromClass([rootView class]) ?: @"<nil>",
        ui_rootViewLooksLikeGoodsDetailPage(rootView) ? @"true" : @"false",
        taskLastGoodsId ?: @"",
        visibleTextSnippet,
        (unsigned long)cachedCandidateCount]);
    return NO;
}

static BOOL ui_rootViewLooksLikeHomePage(id rootView) {
    if (!rootView) {
        return NO;
    }

    @try {
        if (ui_rootViewLooksLikeSearchPage(rootView)) {
            return NO;
        }

        if (ui_findAllViewsOfClass(rootView, @"MenuPageScrollView").count > 0 &&
            ui_findAllViewsOfClass(rootView, @"PDDCommonSearchBar").count > 0) {
            return YES;
        }

        id rootVC = ui_currentRootViewController();
        NSString *rootVCClassName = rootVC ? (NSStringFromClass([rootVC class]) ?: @"") : @"";
        if ([rootVCClassName containsString:@"TabBar"] &&
            ui_findAllViewsOfClass(rootView, @"PDDCommonSearchBar").count > 0) {
            return YES;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_viewHasAncestorClassFragment(id view, NSString *classFragment) {
    if (!view || classFragment.length == 0) {
        return NO;
    }

    @try {
        SEL superviewSel = NSSelectorFromString(@"superview");
        id current = view;
        NSInteger depth = 0;
        while (current && depth < 6) {
            NSString *className = NSStringFromClass([current class]) ?: @"";
            if ([className containsString:classFragment]) {
                return YES;
            }
            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_appendUniqueActivationTarget(NSMutableArray *targets, NSMutableSet *seen, id candidate) {
    if (!targets || !seen || !candidate) {
        return NO;
    }

    @try {
        NSValue *key = [NSValue valueWithNonretainedObject:candidate];
        if ([seen containsObject:key]) {
            return NO;
        }
        [seen addObject:key];
        [targets addObject:candidate];
        return YES;
    } @catch (NSException *exception) {
        return NO;
    }
}

static BOOL ui_isViewLikeActivationTarget(id target) {
    if (!target) {
        return NO;
    }

    @try {
        Class UIViewClass = NSClassFromString(@"UIView");
        if (UIViewClass && [target isKindOfClass:UIViewClass]) {
            return YES;
        }

        NSArray *selectorNames = @[@"superview", @"window", @"gestureRecognizers", @"subviews"];
        for (NSString *selectorName in selectorNames) {
            if ([target respondsToSelector:NSSelectorFromString(selectorName)]) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_viewLooksSearchActionable(id target) {
    if (!target) {
        return NO;
    }

    if ([target respondsToSelector:NSSelectorFromString(@"becomeFirstResponder")] ||
        [target respondsToSelector:NSSelectorFromString(@"sendActionsForControlEvents:")]) {
        return YES;
    }

    if (ui_isViewLikeActivationTarget(target)) {
        @try {
            SEL gestureRecognizersSel = NSSelectorFromString(@"gestureRecognizers");
            if ([target respondsToSelector:gestureRecognizersSel]) {
                NSArray *gestureRecognizers = ((NSArray * (*)(id, SEL))objc_msgSend)(target, gestureRecognizersSel);
                if (gestureRecognizers.count > 0) {
                    return YES;
                }
            }
        } @catch (NSException *exception) {
        }
    }

    NSArray *selectorNames = @[
        @"onInputFieldTapped",
        @"clickedSearchBar",
        @"clickSearchBar",
        @"searchBarClicked",
        @"activate",
        @"focus",
        @"focusInput",
        @"openSearch",
        @"showSearch",
        @"presentSearch",
        @"gotoSearch",
        @"jumpToSearch",
        @"enterSearch",
        @"enterSearchPage",
        @"tapSearchBar",
        @"didTapSearchBar",
        @"handleSearchBarTap",
        @"showKeyboard",
        @"activate:",
        @"focusInput:",
        @"openSearch:",
        @"showSearch:",
        @"presentSearch:"
    ];
    for (NSString *selectorName in selectorNames) {
        if ([target respondsToSelector:NSSelectorFromString(selectorName)]) {
            return YES;
        }
    }

    return NO;
}

static id ui_nearestActionableAncestor(id source) {
    if (!source) {
        return nil;
    }

    @try {
        SEL superviewSel = NSSelectorFromString(@"superview");
        id current = [source respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(source, superviewSel) : nil;
        NSInteger depth = 0;
        while (current && depth < 4) {
            if (ui_viewLooksSearchActionable(current)) {
                return current;
            }
            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        }
    } @catch (NSException *exception) {
    }

    return nil;
}

static void ui_collectSearchActivationTargets(id source, id searchEntry, NSMutableArray *targets, NSMutableSet *seen) {
    if (!source || !targets || !seen || targets.count >= 12) {
        return;
    }

    ui_appendUniqueActivationTarget(targets, seen, source);

    @try {
        if (source == searchEntry) {
            id directEditable = ui_resolveDirectTextInputView(source);
            if (directEditable && directEditable != source) {
                ui_appendUniqueActivationTarget(targets, seen, directEditable);
            }
        }

        id ancestor = ui_nearestActionableAncestor(source);
        if (ancestor) {
            ui_appendUniqueActivationTarget(targets, seen, ancestor);
        }
    } @catch (NSException *exception) {
    }
}

static BOOL ui_tryInvokeZeroArgumentSelectors(id target, NSArray *selectorNames, NSString **usedSelectorNameOut) {
    for (NSString *selectorName in selectorNames) {
        SEL selector = NSSelectorFromString(selectorName);
        if (![target respondsToSelector:selector]) {
            continue;
        }
        ((void (*)(id, SEL))objc_msgSend)(target, selector);
        if (usedSelectorNameOut) {
            *usedSelectorNameOut = selectorName;
        }
        return YES;
    }
    return NO;
}

static BOOL ui_tryInvokeObjectArgumentSelectors(id target, NSArray *selectorNames, id argument, NSString **usedSelectorNameOut) {
    for (NSString *selectorName in selectorNames) {
        SEL selector = NSSelectorFromString(selectorName);
        if (![target respondsToSelector:selector]) {
            continue;
        }
        ((void (*)(id, SEL, id))objc_msgSend)(target, selector, argument);
        if (usedSelectorNameOut) {
            *usedSelectorNameOut = selectorName;
        }
        return YES;
    }
    return NO;
}

static BOOL ui_tryInvokeSearchActivationOnTarget(id target, id searchEntry) {
    if (!target) {
        return NO;
    }

    @try {
        NSString *usedMethod = nil;
        if ([target respondsToSelector:NSSelectorFromString(@"becomeFirstResponder")] &&
            ((BOOL (*)(id, SEL))objc_msgSend)(target, NSSelectorFromString(@"becomeFirstResponder"))) {
            usedMethod = @"becomeFirstResponder";
        } else if (ui_isViewLikeActivationTarget(target) && ui_sendTapActionToView(target)) {
            usedMethod = @"tapAction";
        } else {
            id objectArg = searchEntry ?: target;
            NSArray *zeroArgSelectors = @[
                @"onInputFieldTapped",
                @"clickedSearchBar",
                @"clickSearchBar",
                @"searchBarClicked",
                @"activate",
                @"focus",
                @"focusInput",
                @"openSearch",
                @"showSearch",
                @"presentSearch",
                @"gotoSearch",
                @"jumpToSearch",
                @"enterSearch",
                @"enterSearchPage",
                @"tapSearchBar",
                @"didTapSearchBar",
                @"handleSearchBarTap",
                @"showKeyboard"
            ];
            if (!ui_tryInvokeZeroArgumentSelectors(target, zeroArgSelectors, &usedMethod)) {
                NSArray *viewArgSelectors = @[
                    @"activate:",
                    @"focusInput:",
                    @"searchBarClicked:",
                    @"clickedSearchBar:",
                    @"clickSearchBar:",
                    @"didTapSearchBar:",
                    @"handleSearchBarTap:",
                    @"tapSearchBar:",
                    @"openSearch:",
                    @"showSearch:",
                    @"presentSearch:"
                ];
                ui_tryInvokeObjectArgumentSelectors(target, viewArgSelectors, objectArg, &usedMethod);
            }
        }

        if (usedMethod.length > 0) {
            NSLog(@"[AutoSearch] 搜索入口激活成功: method=%@ target=%@", usedMethod, ui_viewSummaryText(target));
            return YES;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_submitSearchInView(id searchView, NSString *keyword) {
    if (!searchView || keyword.length == 0) {
        return NO;
    }

    @try {
        id editableCandidate = ui_bestEditableSearchView(searchView) ?: searchView;
        id editableView = ui_resolveDirectTextInputView(editableCandidate) ?: editableCandidate;
        id writeTarget = editableView ?: editableCandidate ?: searchView;
        NSString *submitMode = @"none";
        NSString *writeMode = @"none";
        BOOL keywordApplied = NO;
        BOOL didWriteText = NO;
        SEL becomeFirstResponderSel = NSSelectorFromString(@"becomeFirstResponder");
        SEL setTextSel = NSSelectorFromString(@"setText:");
        SEL insertTextSel = NSSelectorFromString(@"insertText:");
        SEL deleteBackwardSel = NSSelectorFromString(@"deleteBackward");
        if ([writeTarget respondsToSelector:becomeFirstResponderSel]) {
            ((BOOL (*)(id, SEL))objc_msgSend)(writeTarget, becomeFirstResponderSel);
        }

        NSArray *clearSelectors = @[@"clear", @"clearText", @"clearBtnClicked"];
        for (NSString *selectorName in clearSelectors) {
            SEL selector = NSSelectorFromString(selectorName);
            if ([writeTarget respondsToSelector:selector]) {
                ((void (*)(id, SEL))objc_msgSend)(writeTarget, selector);
                break;
            }
        }

        if ([writeTarget respondsToSelector:setTextSel]) {
            ((void (*)(id, SEL, NSString *))objc_msgSend)(writeTarget, setTextSel, @"");
            ((void (*)(id, SEL, NSString *))objc_msgSend)(writeTarget, setTextSel, keyword);
            didWriteText = YES;
            writeMode = @"setText:";
        } else {
            if ([writeTarget respondsToSelector:deleteBackwardSel]) {
                NSString *existingText = ui_currentTextValue(writeTarget);
                NSInteger deleteCount = MIN((NSInteger)existingText.length + 4, 64);
                for (NSInteger idx = 0; idx < deleteCount; idx++) {
                    ((void (*)(id, SEL))objc_msgSend)(writeTarget, deleteBackwardSel);
                }
            }
            if ([writeTarget respondsToSelector:insertTextSel]) {
                ((void (*)(id, SEL, NSString *))objc_msgSend)(writeTarget, insertTextSel, keyword);
                didWriteText = YES;
                writeMode = @"insertText:";
            }
        }

        if (didWriteText) {
            NSString *currentText = ui_currentTextValue(writeTarget);
            keywordApplied = currentText.length > 0 && [currentText containsString:keyword];
        }

        BOOL didSubmit = NO;
        SEL delegateSel = NSSelectorFromString(@"delegate");
        id editableDelegate = [editableView respondsToSelector:delegateSel] ? ((id (*)(id, SEL))objc_msgSend)(editableView, delegateSel) : nil;
        SEL textFieldShouldReturnSel = NSSelectorFromString(@"textFieldShouldReturn:");
        if (editableDelegate && [editableDelegate respondsToSelector:textFieldShouldReturnSel]) {
            ((BOOL (*)(id, SEL, id))objc_msgSend)(editableDelegate, textFieldShouldReturnSel, editableView);
            didSubmit = YES;
            submitMode = @"delegate.textFieldShouldReturn:";
        }

        if (!didSubmit) {
            id searchDelegate = [searchView respondsToSelector:delegateSel] ? ((id (*)(id, SEL))objc_msgSend)(searchView, delegateSel) : nil;
            SEL searchBarSearchButtonClickedSel = NSSelectorFromString(@"searchBarSearchButtonClicked:");
            if (searchDelegate && [searchDelegate respondsToSelector:searchBarSearchButtonClickedSel]) {
                ((void (*)(id, SEL, id))objc_msgSend)(searchDelegate, searchBarSearchButtonClickedSel, searchView);
                didSubmit = YES;
                submitMode = @"delegate.searchBarSearchButtonClicked:";
            }
        }

        if (!didSubmit && [writeTarget respondsToSelector:insertTextSel]) {
            ((void (*)(id, SEL, NSString *))objc_msgSend)(writeTarget, insertTextSel, @"\n");
            didSubmit = YES;
            submitMode = @"insertText:\\n";
        }

        if (!didSubmit) {
            NSMutableArray *submitTargets = [NSMutableArray array];
            NSMutableSet *seenSubmitTargets = [NSMutableSet set];
            ui_appendUniqueActivationTarget(submitTargets, seenSubmitTargets, writeTarget);
            ui_appendUniqueActivationTarget(submitTargets, seenSubmitTargets, editableCandidate);
            ui_appendUniqueActivationTarget(submitTargets, seenSubmitTargets, searchView);
            NSArray *privateSelectors = @[
                @"performSearch",
                @"search",
                @"confirmSearch",
                @"commitSearch",
                @"startSearch",
                @"rightButtonClicked",
                @"enhancedRightSearchBtnClicked"
            ];
            for (id target in submitTargets) {
                NSString *usedSelector = nil;
                if (ui_tryInvokeZeroArgumentSelectors(target, privateSelectors, &usedSelector)) {
                    didSubmit = YES;
                    submitMode = [NSString stringWithFormat:@"selector.%@", usedSelector];
                    break;
                }
            }
        }

        SEL sendActionSel = NSSelectorFromString(@"sendActionsForControlEvents:");
        if (!didSubmit && [searchView respondsToSelector:sendActionSel]) {
            ((void (*)(id, SEL, NSUInteger))objc_msgSend)(searchView, sendActionSel, 1 << 6);
            didSubmit = YES;
            submitMode = @"controlEvent.editingDidEndOnExit";
        }

        if (task_hasActiveTask()) {
            debug_writeTaskDump(@"search_submit_trace", [NSString stringWithFormat:
                @"keyword=%@\nsearch_view=%@\neditable_candidate=%@\neditable_view=%@\nwrite_target=%@\nwrite_mode=%@\nkeyword_applied=%@\nsubmit_mode=%@\ndid_write=%@\ndid_submit=%@",
                keyword ?: @"",
                ui_viewSummaryText(searchView),
                ui_viewSummaryText(editableCandidate),
                ui_viewSummaryText(editableView),
                ui_viewSummaryText(writeTarget),
                writeMode ?: @"none",
                keywordApplied ? @"true" : @"false",
                submitMode ?: @"none",
                didWriteText ? @"true" : @"false",
                didSubmit ? @"true" : @"false"]);
        }

        NSLog(@"[AutoSearch] 搜索提交结果: keyword=%@ write=%@ submit=%@ applied=%@",
            keyword,
            writeMode,
            submitMode,
            keywordApplied ? @"true" : @"false");
        return didSubmit;
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 提交搜索失败: %@", exception);
        if (task_hasActiveTask()) {
            debug_writeTaskDump(@"search_submit_trace", [NSString stringWithFormat:@"keyword=%@\nexception=%@", keyword ?: @"", exception]);
        }
        return NO;
    }
}

static BOOL ui_trySubmitSearchFromRootView(id rootView, NSString *keyword) {
    if (!rootView) {
        return NO;
    }

    BOOL rootLooksLikeHomePage = ui_rootViewLooksLikeHomePage(rootView);

    NSMutableArray *candidates = [NSMutableArray array];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UISearchBar")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"SearchBar")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UISearchTextField")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UITextField")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"TextField")];

    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;
    for (id candidate in candidates) {
        if (!ui_viewIsVisible(candidate)) {
            continue;
        }

        CGRect frame = ui_viewFrameInScreen(candidate);
        if (frame.size.width < 100.0 || frame.origin.y > 260.0) {
            continue;
        }

        NSString *className = NSStringFromClass([candidate class]) ?: @"";
        NSString *metadata = ui_viewMetadataText(candidate);
        BOOL isHomeEntryCandidate = rootLooksLikeHomePage &&
            ([className containsString:@"PDDCommonSearchBar"] ||
             [className containsString:@"PDDSearchBarInputFieldNew"] ||
             ui_viewHasAncestorClassFragment(candidate, @"PDDCommonSearchBar"));
        if (isHomeEntryCandidate) {
            continue;
        }
        BOOL isSearchBarClass = [className containsString:@"SearchBar"];
        NSInteger score = 0;
        if ([className containsString:@"Search"]) {
            score += 100;
        }
        if (isSearchBarClass) {
            score += 120;
        }
        if ([className containsString:@"TextField"]) {
            score += 30;
        }
        if (ui_stringLooksLikeSearch(metadata)) {
            score += 120;
        }
        if (frame.origin.y < 140.0) {
            score += 40;
        }
        if (frame.size.width > 180.0) {
            score += 20;
        }

        if (score > bestScore) {
            bestScore = score;
            bestView = candidate;
        }
    }

    if (!bestView) {
        return NO;
    }

    NSLog(@"[AutoSearch] 使用搜索输入控件: %@ %@", NSStringFromClass([bestView class]), ui_viewMetadataText(bestView));
    return ui_submitSearchInView(bestView, keyword);
}

static BOOL ui_invokeGestureTap(id view) {
    if (!view) {
        return NO;
    }

    @try {
        SEL gestureRecognizersSel = NSSelectorFromString(@"gestureRecognizers");
        if (![view respondsToSelector:gestureRecognizersSel]) {
            return NO;
        }

        NSArray *gestureRecognizers = ((NSArray * (*)(id, SEL))objc_msgSend)(view, gestureRecognizersSel);
        for (id gesture in gestureRecognizers) {
            SEL isEnabledSel = NSSelectorFromString(@"isEnabled");
            if ([gesture respondsToSelector:isEnabledSel] &&
                !((BOOL (*)(id, SEL))objc_msgSend)(gesture, isEnabledSel)) {
                continue;
            }

            NSArray *targets = [gesture valueForKey:@"_targets"];
            for (id gestureTarget in targets) {
                id target = [gestureTarget valueForKey:@"target"];
                id actionValue = [gestureTarget valueForKey:@"action"];
                NSString *actionName = [NSString stringWithFormat:@"%@", actionValue ?: @""];
                SEL action = NSSelectorFromString(actionName);
                if (!target || !actionName.length || ![target respondsToSelector:action]) {
                    continue;
                }

                if ([actionName hasSuffix:@":"]) {
                    ((void (*)(id, SEL, id))objc_msgSend)(target, action, gesture);
                } else {
                    ((void (*)(id, SEL))objc_msgSend)(target, action);
                }
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_sendTapActionToView(id view) {
    SEL superviewSel = NSSelectorFromString(@"superview");
    id current = view;
    NSInteger depth = 0;

    while (current && depth < 6) {
        @try {
            SEL sendActionsSel = NSSelectorFromString(@"sendActionsForControlEvents:");
            if ([current respondsToSelector:sendActionsSel]) {
                ((void (*)(id, SEL, NSUInteger))objc_msgSend)(current, sendActionsSel, 64);
                return YES;
            }

            if (ui_invokeGestureTap(current)) {
                return YES;
            }

            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        } @catch (NSException *exception) {
            break;
        }
    }

    return NO;
}

static id ui_bestSearchEntryView(id rootView) {
    if (!rootView) {
        return nil;
    }

    NSMutableArray *candidates = [NSMutableArray array];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"SearchBar")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"SearchView")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIButton")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIControl")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UILabel")];

    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;
    for (id candidate in candidates) {
        if (!ui_viewIsVisible(candidate)) {
            continue;
        }

        CGRect frame = ui_viewFrameInScreen(candidate);
        if (frame.origin.y > 220.0 || frame.size.width < 40.0 || frame.size.height < 20.0) {
            continue;
        }

        NSString *className = NSStringFromClass([candidate class]) ?: @"";
        NSString *metadata = ui_viewMetadataText(candidate);
        BOOL looksLikeSearchContainer = [className containsString:@"SearchBar"] || [className containsString:@"SearchView"];
        if (!looksLikeSearchContainer && !ui_stringLooksLikeSearch(metadata)) {
            continue;
        }

        NSInteger score = 100;
        if (looksLikeSearchContainer) {
            score += 160;
        }
        if ([className containsString:@"PDDCommonSearchBar"]) {
            score += 220;
        }
        if ([className containsString:@"PDDSearchBarInputFieldNew"]) {
            score += 180;
        }
        if (ui_viewHasAncestorClassFragment(candidate, @"PDDCommonSearchBar")) {
            score += 120;
        }
        if (frame.origin.y < 120.0) {
            score += 40;
        }
        if (frame.size.width > 120.0) {
            score += 20;
        }
        if ([className containsString:@"Button"]) {
            score += 20;
        }

        if (score > bestScore) {
            bestScore = score;
            bestView = candidate;
        }
    }

    return bestView;
}

static BOOL ui_tryActivateSearchEntry(id searchEntry) {
    if (!searchEntry) {
        return NO;
    }

    NSMutableArray *targets = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    ui_collectSearchActivationTargets(searchEntry, searchEntry, targets, seen);

    id editableView = ui_bestEditableSearchView(searchEntry);
    if (editableView && editableView != searchEntry) {
        ui_collectSearchActivationTargets(editableView, searchEntry, targets, seen);
    }

    id directEditableView = ui_resolveDirectTextInputView(editableView ?: searchEntry);
    if (directEditableView && directEditableView != editableView && directEditableView != searchEntry) {
        ui_collectSearchActivationTargets(directEditableView, searchEntry, targets, seen);
    }

    for (id candidate in targets) {
        if (ui_tryInvokeSearchActivationOnTarget(candidate, searchEntry)) {
            NSLog(@"[AutoSearch] 使用搜索入口视图激活: %@ %@", NSStringFromClass([candidate class]), ui_viewMetadataText(candidate));
            if (task_hasActiveTask()) {
                debug_writeTaskDump(@"search_activation_trace", [NSString stringWithFormat:
                    @"mode=entry_view\nkeyword=%@\nentry=%@\nactivated_target=%@",
                    taskCurrentKeyword ?: @"",
                    ui_viewSummaryText(searchEntry),
                    ui_viewSummaryText(candidate)]);
            }
            return YES;
        }
    }

    return NO;
}

static BOOL ui_stringContainsAnyKeyword(NSString *value, NSArray<NSString *> *keywords) {
    NSString *text = [[NSString stringWithFormat:@"%@", value ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (text.length == 0 || keywords.count == 0) {
        return NO;
    }

    for (NSString *keyword in keywords) {
        NSString *needle = [[NSString stringWithFormat:@"%@", keyword ?: @""]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (needle.length == 0) {
            continue;
        }
        if ([text containsString:needle] || [text localizedCaseInsensitiveContainsString:needle]) {
            return YES;
        }
    }
    return NO;
}

static id ui_bestSearchToolbarButtonWithKeywords(id rootView, NSArray<NSString *> *keywords, BOOL preferRightSide) {
    if (!rootView || keywords.count == 0) {
        return nil;
    }

    NSMutableArray *candidates = [NSMutableArray arrayWithObject:rootView];
    [candidates addObjectsFromArray:ui_allDescendantViews(rootView)];

    NSMutableSet *seen = [NSMutableSet set];
    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;

    for (id candidate in candidates) {
        if (!candidate || !ui_viewIsVisible(candidate)) {
            continue;
        }

        @try {
            NSValue *candidateKey = [NSValue valueWithNonretainedObject:candidate];
            if ([seen containsObject:candidateKey]) {
                continue;
            }
            [seen addObject:candidateKey];

            CGRect frame = ui_viewFrameInScreen(candidate);
            if (frame.origin.y < 60.0 || frame.origin.y > 190.0 || frame.size.width < 10.0 || frame.size.height < 10.0) {
                continue;
            }

            NSString *metadata = ui_viewMetadataText(candidate);
            if (!ui_stringContainsAnyKeyword(metadata, keywords)) {
                continue;
            }

            NSString *className = NSStringFromClass([candidate class]) ?: @"";
            NSInteger score = 300;
            if ([className containsString:@"PDDSRSortFilter"] ||
                [className containsString:@"SearchOutsideFilter"] ||
                [className containsString:@"Filter"]) {
                score += 220;
            }
            if ([className containsString:@"Cell"]) {
                score += 140;
            }
            if ([className containsString:@"Button"]) {
                score += 100;
            }
            if ([className containsString:@"Control"]) {
                score += 60;
            }
            if ([className containsString:@"Label"]) {
                score += 40;
            }
            if (frame.origin.y < 120.0) {
                score += 30;
            }
            if (preferRightSide) {
                score += (NSInteger)CGRectGetMidX(frame);
            }
            for (NSString *keyword in keywords) {
                if (keyword.length > 0 && [metadata isEqualToString:keyword]) {
                    score += 120;
                    break;
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestView = candidate;
            }
        } @catch (NSException *exception) {
        }
    }

    return bestView;
}

static id ui_nearestAncestorWithClassFragment(id view, NSString *classFragment) {
    if (!view || classFragment.length == 0) {
        return nil;
    }

    @try {
        SEL superviewSel = NSSelectorFromString(@"superview");
        id current = view;
        NSInteger depth = 0;
        while (current && depth < 8) {
            NSString *className = NSStringFromClass([current class]) ?: @"";
            if ([className containsString:classFragment]) {
                return current;
            }
            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        }
    } @catch (NSException *exception) {
    }

    return nil;
}

static NSString *debug_toolbarActivationProbeText(id view) {
    NSMutableString *output = [NSMutableString string];
    if (!view) {
        [output appendString:@"toolbar_probe=<nil>"];
        return output;
    }

    id cell = ui_nearestAncestorWithClassFragment(view, @"Cell");
    id collection = ui_nearestAncestorWithClassFragment(view, @"Collection");
    id control = ui_nearestAncestorWithClassFragment(view, @"Control");
    id delegate = nil;
    @try {
        SEL delegateSel = NSSelectorFromString(@"delegate");
        if (collection && [collection respondsToSelector:delegateSel]) {
            delegate = ((id (*)(id, SEL))objc_msgSend)(collection, delegateSel);
        }
    } @catch (NSException *exception) {
    }

    [output appendString:@"[toolbar_probe]\n"];
    debug_appendInterestingView(output, view, @"target");
    if (cell && cell != view) {
        debug_appendInterestingView(output, cell, @"cell");
    }
    if (collection && collection != cell && collection != view) {
        debug_appendInterestingView(output, collection, @"collection");
    }
    if (control && control != collection && control != cell && control != view) {
        debug_appendInterestingView(output, control, @"control");
    }
    if (delegate && delegate != control && delegate != collection && delegate != cell && delegate != view) {
        debug_appendInterestingView(output, delegate, @"delegate");
    }

    return output;
}

static BOOL ui_boolValueForSelector(id target, NSString *selectorName, BOOL *valueOut) {
    if (!target || selectorName.length == 0 || !valueOut) {
        return NO;
    }

    @try {
        SEL selector = NSSelectorFromString(selectorName);
        if (![target respondsToSelector:selector]) {
            return NO;
        }
        *valueOut = ((BOOL (*)(id, SEL))objc_msgSend)(target, selector);
        return YES;
    } @catch (NSException *exception) {
        return NO;
    }
}

static BOOL ui_boolValueForKey(id target, NSString *key, BOOL *valueOut) {
    if (!target || key.length == 0 || !valueOut) {
        return NO;
    }

    @try {
        id value = [target valueForKey:key];
        if ([value respondsToSelector:@selector(boolValue)]) {
            *valueOut = [value boolValue];
            return YES;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_searchToolbarItemLooksSelected(id view, NSString **traceOut) {
    if (!view) {
        if (traceOut) {
            *traceOut = @"toolbar_state=<nil>";
        }
        return NO;
    }

    NSMutableArray *candidates = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    ui_appendUniqueActivationTarget(candidates, seen, view);
    ui_appendUniqueActivationTarget(candidates, seen, ui_nearestAncestorWithClassFragment(view, @"Cell"));
    ui_appendUniqueActivationTarget(candidates, seen, ui_nearestAncestorWithClassFragment(view, @"Control"));
    ui_appendUniqueActivationTarget(candidates, seen, ui_nearestAncestorWithClassFragment(view, @"Button"));

    NSMutableString *trace = [NSMutableString string];
    BOOL looksSelected = NO;
    NSArray *selectorNames = @[@"isSelected", @"isActivated", @"isChecked"];
    NSArray *keyNames = @[@"selected", @"activated", @"checked", @"_selected", @"_activated"];

    for (NSInteger index = 0; index < (NSInteger)candidates.count; index++) {
        id candidate = candidates[index];
        if (!candidate) {
            continue;
        }

        [trace appendFormat:@"candidate_%ld=%@\n", (long)(index + 1), ui_viewSummaryText(candidate)];

        for (NSString *selectorName in selectorNames) {
            BOOL value = NO;
            if (!ui_boolValueForSelector(candidate, selectorName, &value)) {
                continue;
            }
            [trace appendFormat:@"  %@=%@\n", selectorName, value ? @"true" : @"false"];
            if (value) {
                looksSelected = YES;
            }
        }

        for (NSString *keyName in keyNames) {
            BOOL value = NO;
            if (!ui_boolValueForKey(candidate, keyName, &value)) {
                continue;
            }
            [trace appendFormat:@"  key.%@=%@\n", keyName, value ? @"true" : @"false"];
            if (value) {
                looksSelected = YES;
            }
        }

        @try {
            SEL stateSel = NSSelectorFromString(@"state");
            if ([candidate respondsToSelector:stateSel]) {
                NSUInteger controlState = ((NSUInteger (*)(id, SEL))objc_msgSend)(candidate, stateSel);
                BOOL controlSelected = (controlState & (1 << 2)) != 0;
                [trace appendFormat:@"  state=%lu\n", (unsigned long)controlState];
                if (controlSelected) {
                    looksSelected = YES;
                }
            }
        } @catch (NSException *exception) {
        }
    }

    if (traceOut) {
        *traceOut = trace;
    }
    return looksSelected;
}

static BOOL ui_trySelectCollectionCellAncestor(id view, NSString **traceOut) {
    if (!view) {
        return NO;
    }

    @try {
        SEL superviewSel = NSSelectorFromString(@"superview");
        id current = view;
        id cell = nil;
        id collectionView = nil;
        NSInteger depth = 0;
        while (current && depth < 8) {
            NSString *className = NSStringFromClass([current class]) ?: @"";
            if (!cell && [className containsString:@"Cell"]) {
                cell = current;
            }
            if (!collectionView && [className containsString:@"CollectionView"]) {
                collectionView = current;
            }
            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        }

        if (!cell || !collectionView) {
            if (traceOut) {
                *traceOut = [NSString stringWithFormat:@"collection_activate_missing cell=%@ collection=%@",
                    ui_viewSummaryText(cell), ui_viewSummaryText(collectionView)];
            }
            return NO;
        }

        SEL indexPathForCellSel = NSSelectorFromString(@"indexPathForCell:");
        id indexPath = [collectionView respondsToSelector:indexPathForCellSel] ?
            ((id (*)(id, SEL, id))objc_msgSend)(collectionView, indexPathForCellSel, cell) :
            nil;
        if (!indexPath) {
            if (traceOut) {
                *traceOut = [NSString stringWithFormat:@"collection_activate_no_index_path cell=%@ collection=%@",
                    ui_viewSummaryText(cell), ui_viewSummaryText(collectionView)];
            }
            return NO;
        }

        SEL delegateSel = NSSelectorFromString(@"delegate");
        id delegate = [collectionView respondsToSelector:delegateSel] ? ((id (*)(id, SEL))objc_msgSend)(collectionView, delegateSel) : nil;
        BOOL didSelect = NO;
        NSString *usedSelector = @"";
        BOOL wasSelected = NO;
        SEL isSelectedSel = NSSelectorFromString(@"isSelected");
        if ([cell respondsToSelector:isSelectedSel]) {
            wasSelected = ((BOOL (*)(id, SEL))objc_msgSend)(cell, isSelectedSel);
        }

        if (delegate) {
            SEL directClickSel = NSSelectorFromString(@"didClickMixBarCell:atIndexPath:");
            if ([delegate respondsToSelector:directClickSel]) {
                ((void (*)(id, SEL, id, id))objc_msgSend)(delegate, directClickSel, cell, indexPath);
                didSelect = YES;
                usedSelector = @"didClickMixBarCell:atIndexPath:";
            }
        }

        if (!didSelect && delegate) {
            SEL privateSelectSel = NSSelectorFromString(@"p_didSelectItemAtIndexPath:currentIsSelected:");
            if ([delegate respondsToSelector:privateSelectSel]) {
                ((void (*)(id, SEL, id, BOOL))objc_msgSend)(delegate, privateSelectSel, indexPath, wasSelected);
                didSelect = YES;
                usedSelector = [NSString stringWithFormat:@"p_didSelectItemAtIndexPath:currentIsSelected:(was=%@)", wasSelected ? @"true" : @"false"];
            }
        }

        if (!didSelect) {
            NSArray *selectorNames = @[
                @"collectionView:didSelectItemAtIndexPath:",
                @"collectionView:didClickItemAtIndexPath:",
                @"collectionView:didHighlightItemAtIndexPath:"
            ];
            for (NSString *selectorName in selectorNames) {
                SEL selector = NSSelectorFromString(selectorName);
                if (!delegate || ![delegate respondsToSelector:selector]) {
                    continue;
                }
                ((void (*)(id, SEL, id, id))objc_msgSend)(delegate, selector, collectionView, indexPath);
                didSelect = YES;
                usedSelector = selectorName;
                break;
            }
        }

        SEL selectItemSel = NSSelectorFromString(@"selectItemAtIndexPath:animated:scrollPosition:");
        if (didSelect && [collectionView respondsToSelector:selectItemSel]) {
            ((void (*)(id, SEL, id, BOOL, NSInteger))objc_msgSend)(collectionView, selectItemSel, indexPath, YES, 0);
        }

        if (!didSelect) {
            didSelect = ui_sendTapActionToView(cell);
            usedSelector = didSelect ? @"tapAction" : @"none";
        }

        if (traceOut) {
            *traceOut = [NSString stringWithFormat:@"collection_activate=%@\ncell=%@\ncollection=%@\ndelegate=%@\nwas_selected=%@\nselector=%@",
                didSelect ? @"true" : @"false",
                ui_viewSummaryText(cell),
                ui_viewSummaryText(collectionView),
                delegate ? (NSStringFromClass([delegate class]) ?: @"<unknown>") : @"<nil>",
                wasSelected ? @"true" : @"false",
                usedSelector];
        }
        return didSelect;
    } @catch (NSException *exception) {
        if (traceOut) {
            *traceOut = [NSString stringWithFormat:@"collection_activate_exception=%@", exception];
        }
        return NO;
    }
}

static BOOL ui_tryActivateSearchToolbarItemView(id view, NSString **traceOut) {
    if (!view) {
        return NO;
    }

    NSString *collectionTrace = nil;
    if (ui_trySelectCollectionCellAncestor(view, &collectionTrace)) {
        if (traceOut) {
            *traceOut = collectionTrace;
        }
        return YES;
    }

    BOOL didTap = ui_tryTapViewWithFallback(view);
    if (traceOut) {
        *traceOut = [NSString stringWithFormat:@"fallback_view_tap=%@\nview=%@\n%@",
            didTap ? @"true" : @"false",
            ui_viewSummaryText(view),
            collectionTrace ?: @""];
    }
    return didTap;
}

static BOOL ui_tryTapViewWithFallback(id view) {
    if (!view) {
        return NO;
    }
    if (ui_sendTapActionToView(view)) {
        return YES;
    }
    CGRect frame = ui_viewFrameInScreen(view);
    if (CGRectIsEmpty(frame)) {
        return NO;
    }
    return ui_tapAtPoint(CGPointMake(CGRectGetMidX(frame), CGRectGetMidY(frame)));
}

static NSString *debug_searchFilterProbeText(id rootView) {
    NSMutableString *output = [NSMutableString string];
    if (!rootView) {
        [output appendString:@"filter_probe=<no-root>"];
        return output;
    }

    @try {
        NSMutableArray *interesting = [NSMutableArray array];
        NSMutableArray *allViews = [NSMutableArray arrayWithObject:rootView];
        [allViews addObjectsFromArray:ui_allDescendantViews(rootView)];

        for (id candidate in allViews) {
            if (!candidate || !ui_viewIsVisible(candidate)) {
                continue;
            }

            CGRect frame = ui_viewFrameInScreen(candidate);
            if (frame.origin.y < 60.0 || CGRectGetMaxY(frame) > 190.0 || frame.size.width < 12.0 || frame.size.height < 12.0) {
                continue;
            }

            NSString *className = NSStringFromClass([candidate class]) ?: @"";
            NSString *metadata = ui_viewMetadataText(candidate) ?: @"";
            BOOL looksInteresting = metadata.length > 0 ||
                [className containsString:@"Button"] ||
                [className containsString:@"Label"] ||
                [className containsString:@"Control"] ||
                [className containsString:@"Search"] ||
                [className containsString:@"Tab"] ||
                [className containsString:@"Sort"] ||
                [className containsString:@"Filter"] ||
                [candidate respondsToSelector:NSSelectorFromString(@"sendActionsForControlEvents:")];
            if (!looksInteresting) {
                continue;
            }

            [interesting addObject:candidate];
        }

        [interesting sortUsingComparator:^NSComparisonResult(id lhs, id rhs) {
            CGRect leftFrame = ui_viewFrameInScreen(lhs);
            CGRect rightFrame = ui_viewFrameInScreen(rhs);
            if (fabs(leftFrame.origin.y - rightFrame.origin.y) > 6.0) {
                return leftFrame.origin.y < rightFrame.origin.y ? NSOrderedAscending : NSOrderedDescending;
            }
            if (fabs(leftFrame.origin.x - rightFrame.origin.x) > 4.0) {
                return leftFrame.origin.x < rightFrame.origin.x ? NSOrderedAscending : NSOrderedDescending;
            }
            return NSOrderedSame;
        }];

        [output appendString:@"[top_band_candidates]\n"];
        NSInteger emitted = 0;
        for (id candidate in interesting) {
            if (emitted >= 40) {
                [output appendString:@"... truncated ...\n"];
                break;
            }
            CGRect frame = ui_viewFrameInScreen(candidate);
            [output appendFormat:@"%ld. %@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\" sendActions=%@ gestures=%@\n",
                (long)(emitted + 1),
                NSStringFromClass([candidate class]) ?: @"<unknown>",
                frame.origin.x,
                frame.origin.y,
                frame.size.width,
                frame.size.height,
                ui_viewMetadataText(candidate) ?: @"",
                [candidate respondsToSelector:NSSelectorFromString(@"sendActionsForControlEvents:")] ? @"true" : @"false",
                [candidate respondsToSelector:NSSelectorFromString(@"gestureRecognizers")] ? @"true" : @"false"];
            emitted++;
        }

        CGRect interactiveFrame = ui_currentInteractiveFrame();
        if (!CGRectIsEmpty(interactiveFrame)) {
            CGFloat minX = CGRectGetMinX(interactiveFrame);
            CGFloat width = CGRectGetWidth(interactiveFrame);
            CGFloat toolbarY = MIN(MAX(CGRectGetMinY(interactiveFrame) + 84.0, 78.0), CGRectGetMaxY(interactiveFrame) - 260.0);
            CGFloat slotXs[] = {
                minX + width * 0.14,
                minX + width * 0.38,
                minX + width * 0.62,
                minX + width * 0.86
            };
            [output appendString:@"\n[top_band_hit_tests]\n"];
            for (NSInteger slotIndex = 0; slotIndex < 4; slotIndex++) {
                CGPoint midPoint = CGPointMake(slotXs[slotIndex], toolbarY);
                CGPoint lowerPoint = CGPointMake(slotXs[slotIndex], toolbarY + 16.0);
                [output appendFormat:@"slot_%ld_mid %@\n", (long)slotIndex, debug_hitTestDescription(midPoint)];
                [output appendFormat:@"slot_%ld_low %@\n", (long)slotIndex, debug_hitTestDescription(lowerPoint)];
            }
        }
    } @catch (NSException *exception) {
        [output appendFormat:@"filter_probe_exception=%@\n", exception];
    }

    return output;
}

static NSArray *ui_candidateRootViewsForInspection(id primaryRootView) {
    NSMutableArray *roots = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];

    void (^appendRoot)(id) = ^(id candidate) {
        if (!candidate) {
            return;
        }
        @try {
            NSValue *key = [NSValue valueWithNonretainedObject:candidate];
            if ([seen containsObject:key]) {
                return;
            }
            [seen addObject:key];
            [roots addObject:candidate];
        } @catch (NSException *exception) {
        }
    };

    appendRoot(primaryRootView);
    appendRoot(ui_currentRootView());

    @try {
        Class UIApplicationClass = NSClassFromString(@"UIApplication");
        id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication"));
        SEL keyWindowSel = NSSelectorFromString(@"keyWindow");
        if (app && [app respondsToSelector:keyWindowSel]) {
            id keyWindow = ((id (*)(id, SEL))objc_msgSend)(app, keyWindowSel);
            appendRoot(keyWindow);
        }

        SEL windowsSel = NSSelectorFromString(@"windows");
        if (app && [app respondsToSelector:windowsSel]) {
            NSArray *windows = ((NSArray * (*)(id, SEL))objc_msgSend)(app, windowsSel);
            for (id window in windows) {
                appendRoot(window);
            }
        }
    } @catch (NSException *exception) {
    }

    return roots;
}

static NSString *debug_searchPriceFilterPanelProbeText(id rootView) {
    NSMutableString *output = [NSMutableString string];
    if (!rootView) {
        [output appendString:@"price_filter_panel_probe=<no-root>"];
        return output;
    }

    @try {
        NSMutableArray *interesting = [NSMutableArray array];
        NSArray *roots = ui_candidateRootViewsForInspection(rootView);
        [output appendString:@"[price_filter_panel_roots]\n"];
        NSInteger rootIndex = 0;
        for (id rootCandidate in roots) {
            CGRect rootFrame = ui_viewFrameInScreen(rootCandidate);
            [output appendFormat:@"%ld. %@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\"\n",
                (long)(rootIndex + 1),
                NSStringFromClass([rootCandidate class]) ?: @"<unknown>",
                rootFrame.origin.x,
                rootFrame.origin.y,
                rootFrame.size.width,
                rootFrame.size.height,
                ui_viewMetadataText(rootCandidate) ?: @""];
            rootIndex++;

            NSMutableArray *allViews = [NSMutableArray arrayWithObject:rootCandidate];
            [allViews addObjectsFromArray:ui_allDescendantViews(rootCandidate)];
            for (id candidate in allViews) {
                if (!candidate || !ui_viewIsVisible(candidate)) {
                    continue;
                }

                CGRect frame = ui_viewFrameInScreen(candidate);
                if (CGRectGetMaxY(frame) < 120.0 || frame.size.width < 12.0 || frame.size.height < 12.0) {
                    continue;
                }

                NSString *className = NSStringFromClass([candidate class]) ?: @"";
                NSString *metadata = ui_viewMetadataText(candidate) ?: @"";
                BOOL canWrite = [candidate respondsToSelector:NSSelectorFromString(@"setText:")] ||
                    [candidate respondsToSelector:NSSelectorFromString(@"insertText:")] ||
                    [candidate respondsToSelector:NSSelectorFromString(@"deleteBackward")];
                BOOL looksInteresting = canWrite ||
                    metadata.length > 0 ||
                    [className containsString:@"TextField"] ||
                    [className containsString:@"Input"] ||
                    [className containsString:@"Edit"] ||
                    [className containsString:@"Price"] ||
                    [className containsString:@"Filter"] ||
                    [className containsString:@"Button"] ||
                    [className containsString:@"Label"] ||
                    [className containsString:@"Collection"] ||
                    [className containsString:@"Table"];
                if (!looksInteresting) {
                    continue;
                }
                [interesting addObject:candidate];
            }
        }

        [interesting sortUsingComparator:^NSComparisonResult(id lhs, id rhs) {
            CGRect leftFrame = ui_viewFrameInScreen(lhs);
            CGRect rightFrame = ui_viewFrameInScreen(rhs);
            if (fabs(leftFrame.origin.y - rightFrame.origin.y) > 6.0) {
                return leftFrame.origin.y < rightFrame.origin.y ? NSOrderedAscending : NSOrderedDescending;
            }
            if (fabs(leftFrame.origin.x - rightFrame.origin.x) > 4.0) {
                return leftFrame.origin.x < rightFrame.origin.x ? NSOrderedAscending : NSOrderedDescending;
            }
            return NSOrderedSame;
        }];

        [output appendString:@"[price_filter_panel_candidates]\n"];
        NSInteger emitted = 0;
        for (id candidate in interesting) {
            if (emitted >= 80) {
                [output appendString:@"... truncated ...\n"];
                break;
            }
            CGRect frame = ui_viewFrameInScreen(candidate);
            [output appendFormat:@"%ld. %@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\" setText=%@ insertText=%@ sendActions=%@\n",
                (long)(emitted + 1),
                NSStringFromClass([candidate class]) ?: @"<unknown>",
                frame.origin.x,
                frame.origin.y,
                frame.size.width,
                frame.size.height,
                ui_viewMetadataText(candidate) ?: @"",
                [candidate respondsToSelector:NSSelectorFromString(@"setText:")] ? @"true" : @"false",
                [candidate respondsToSelector:NSSelectorFromString(@"insertText:")] ? @"true" : @"false",
                [candidate respondsToSelector:NSSelectorFromString(@"sendActionsForControlEvents:")] ? @"true" : @"false"];
            emitted++;
        }
    } @catch (NSException *exception) {
        [output appendFormat:@"price_filter_panel_probe_exception=%@\n", exception];
    }

    return output;
}

static BOOL ui_tryTapSearchToolbarFallbackAtSlot(id rootView, NSInteger slotIndex, NSString **traceOut) {
    CGRect interactiveFrame = ui_currentInteractiveFrame();
    if (CGRectIsEmpty(interactiveFrame) || slotIndex < 0 || slotIndex > 3) {
        if (traceOut) {
            *traceOut = [NSString stringWithFormat:@"fallback_slot=%ld unavailable\n%@", (long)slotIndex, debug_searchFilterProbeText(rootView)];
        }
        return NO;
    }

    CGFloat minX = CGRectGetMinX(interactiveFrame);
    CGFloat width = CGRectGetWidth(interactiveFrame);
    CGFloat toolbarY = MIN(MAX(CGRectGetMinY(interactiveFrame) + 84.0, 78.0), CGRectGetMaxY(interactiveFrame) - 260.0);
    CGFloat slotXs[] = {
        minX + width * 0.14,
        minX + width * 0.38,
        minX + width * 0.62,
        minX + width * 0.86
    };
    CGFloat baseX = slotXs[slotIndex];
    CGPoint points[] = {
        CGPointMake(baseX, toolbarY),
        CGPointMake(baseX, toolbarY + 16.0),
        CGPointMake(baseX - 14.0, toolbarY + 8.0),
        CGPointMake(baseX + 14.0, toolbarY + 8.0),
    };

    NSMutableArray *attempts = [NSMutableArray array];
    for (NSInteger idx = 0; idx < 4; idx++) {
        [attempts addObject:debug_hitTestDescription(points[idx])];
        if (ui_tapAtPoint(points[idx])) {
            if (traceOut) {
                NSMutableString *text = [NSMutableString stringWithFormat:@"fallback_slot=%ld tap_index=%ld\n", (long)slotIndex, (long)idx];
                for (NSString *line in attempts) {
                    [text appendFormat:@"%@\n", line];
                }
                [text appendFormat:@"\n%@", debug_searchFilterProbeText(rootView)];
                *traceOut = text;
            }
            return YES;
        }
    }

    if (traceOut) {
        NSMutableString *text = [NSMutableString stringWithFormat:@"fallback_slot=%ld not_tapped\n", (long)slotIndex];
        for (NSString *line in attempts) {
            [text appendFormat:@"%@\n", line];
        }
        [text appendFormat:@"\n%@", debug_searchFilterProbeText(rootView)];
        *traceOut = text;
    }
    return NO;
}

static BOOL ui_tryApplySalesSortOnSearchPage(id rootView, NSString **traceOut) {
    id salesButton = ui_bestSearchToolbarButtonWithKeywords(rootView, @[@"销量"], NO);
    BOOL didTap = NO;
    NSMutableString *trace = [NSMutableString stringWithFormat:@"sales_button=%@\n", ui_viewSummaryText(salesButton)];
    if (salesButton) {
        id directTapTarget = ui_nearestAncestorWithClassFragment(salesButton, @"Cell") ?: salesButton;
        BOOL didDirectTap = ui_tryTapViewWithFallback(directTapTarget);
        [trace appendFormat:@"sales_direct_tap=%@\ndirect_tap_target=%@\n",
            didDirectTap ? @"true" : @"false",
            ui_viewSummaryText(directTapTarget)];
        didTap = didDirectTap;
    }
    if (salesButton && !didTap) {
        NSString *activateTrace = nil;
        didTap = ui_tryActivateSearchToolbarItemView(salesButton, &activateTrace);
        [trace appendFormat:@"sales_button_tapped=%@\n%@\n", didTap ? @"true" : @"false", activateTrace ?: @""];
    }
    if (!didTap) {
        NSString *fallbackTrace = nil;
        didTap = ui_tryTapSearchToolbarFallbackAtSlot(rootView, 1, &fallbackTrace);
        [trace appendFormat:@"fallback_tapped=%@\n", didTap ? @"true" : @"false"];
        if (fallbackTrace.length > 0) {
            [trace appendFormat:@"%@\n", fallbackTrace];
        }
    }
    if (traceOut) {
        if ([trace rangeOfString:@"[top_band_candidates]"].location == NSNotFound) {
            [trace appendString:debug_searchFilterProbeText(rootView)];
        }
        [trace appendFormat:@"\n%@\n", debug_toolbarActivationProbeText(salesButton)];
        *traceOut = trace;
    }
    return didTap;
}

static BOOL ui_tryOpenPriceFilterPanelOnSearchPage(id rootView, NSString **traceOut) {
    id filterButton = ui_bestSearchToolbarButtonWithKeywords(rootView, @[@"筛选"], YES);
    BOOL didTap = NO;
    NSMutableString *trace = [NSMutableString stringWithFormat:@"filter_button=%@\n", ui_viewSummaryText(filterButton)];
    if (filterButton) {
        id directTapTarget = ui_nearestAncestorWithClassFragment(filterButton, @"Cell") ?: filterButton;
        BOOL didDirectTap = ui_tryTapViewWithFallback(directTapTarget);
        [trace appendFormat:@"filter_direct_tap=%@\ndirect_tap_target=%@\n",
            didDirectTap ? @"true" : @"false",
            ui_viewSummaryText(directTapTarget)];
        didTap = didDirectTap;
    }
    if (filterButton && !didTap) {
        NSString *activateTrace = nil;
        didTap = ui_tryActivateSearchToolbarItemView(filterButton, &activateTrace);
        [trace appendFormat:@"filter_button_tapped=%@\n%@\n", didTap ? @"true" : @"false", activateTrace ?: @""];
    }
    if (!didTap) {
        NSString *fallbackTrace = nil;
        didTap = ui_tryTapSearchToolbarFallbackAtSlot(rootView, 3, &fallbackTrace);
        [trace appendFormat:@"fallback_tapped=%@\n", didTap ? @"true" : @"false"];
        if (fallbackTrace.length > 0) {
            [trace appendFormat:@"%@\n", fallbackTrace];
        }
    }
    if (traceOut) {
        if ([trace rangeOfString:@"[top_band_candidates]"].location == NSNotFound) {
            [trace appendString:debug_searchFilterProbeText(rootView)];
        }
        *traceOut = trace;
    }
    return didTap;
}

static BOOL ui_writeTextValueToInputView(id inputView, NSString *value, NSString **writeModeOut) {
    id directInput = ui_resolveDirectTextInputView(inputView) ?: inputView;
    if (!directInput) {
        if (writeModeOut) {
            *writeModeOut = @"no_input";
        }
        return NO;
    }

    NSString *desiredValue = [NSString stringWithFormat:@"%@", value ?: @""];
    NSString *writeMode = @"none";
    BOOL didWrite = NO;

    @try {
        SEL becomeFirstResponderSel = NSSelectorFromString(@"becomeFirstResponder");
        SEL setTextSel = NSSelectorFromString(@"setText:");
        SEL insertTextSel = NSSelectorFromString(@"insertText:");
        SEL deleteBackwardSel = NSSelectorFromString(@"deleteBackward");
        SEL sendActionsSel = NSSelectorFromString(@"sendActionsForControlEvents:");
        if ([directInput respondsToSelector:becomeFirstResponderSel]) {
            ((BOOL (*)(id, SEL))objc_msgSend)(directInput, becomeFirstResponderSel);
        }

        NSArray *clearSelectors = @[@"clear", @"clearText", @"clearBtnClicked"];
        for (NSString *selectorName in clearSelectors) {
            SEL selector = NSSelectorFromString(selectorName);
            if (![directInput respondsToSelector:selector]) {
                continue;
            }
            ((void (*)(id, SEL))objc_msgSend)(directInput, selector);
            break;
        }

        BOOL canDelete = [directInput respondsToSelector:deleteBackwardSel];
        BOOL canInsert = [directInput respondsToSelector:insertTextSel];
        if (canDelete || canInsert) {
            NSString *existingText = ui_currentTextValue(directInput);
            NSInteger deleteCount = MIN((NSInteger)existingText.length + 6, 64);
            if (canDelete) {
                for (NSInteger idx = 0; idx < deleteCount; idx++) {
                    ((void (*)(id, SEL))objc_msgSend)(directInput, deleteBackwardSel);
                }
            } else if ([directInput respondsToSelector:setTextSel]) {
                ((void (*)(id, SEL, NSString *))objc_msgSend)(directInput, setTextSel, @"");
            }

            if (desiredValue.length == 0) {
                didWrite = YES;
                writeMode = canDelete ? @"deleteBackward" : @"setText:";
            } else if (canInsert) {
                for (NSUInteger idx = 0; idx < desiredValue.length; idx++) {
                    NSString *character = [desiredValue substringWithRange:NSMakeRange(idx, 1)];
                    ((void (*)(id, SEL, NSString *))objc_msgSend)(directInput, insertTextSel, character);
                }
                didWrite = YES;
                writeMode = canDelete ? @"deleteBackward+insertText:" : @"insertText:";
            } else if ([directInput respondsToSelector:setTextSel]) {
                ((void (*)(id, SEL, NSString *))objc_msgSend)(directInput, setTextSel, desiredValue);
                didWrite = YES;
                writeMode = @"setText:";
            }
        } else if ([directInput respondsToSelector:setTextSel]) {
            ((void (*)(id, SEL, NSString *))objc_msgSend)(directInput, setTextSel, @"");
            if (desiredValue.length > 0) {
                ((void (*)(id, SEL, NSString *))objc_msgSend)(directInput, setTextSel, desiredValue);
            }
            didWrite = YES;
            writeMode = @"setText:";
        }

        if (didWrite && [directInput respondsToSelector:sendActionsSel]) {
            ((void (*)(id, SEL, NSUInteger))objc_msgSend)(directInput, sendActionsSel, 1 << 17);
            ((void (*)(id, SEL, NSUInteger))objc_msgSend)(directInput, sendActionsSel, 1 << 18);
            writeMode = [writeMode stringByAppendingString:@"+editingEvents"];
        }

        NSString *currentText = ui_currentTextValue(directInput);
        BOOL matched = desiredValue.length == 0 ? currentText.length == 0 : [currentText containsString:desiredValue];
        if (writeModeOut) {
            *writeModeOut = writeMode;
        }
        return didWrite && matched;
    } @catch (NSException *exception) {
        if (writeModeOut) {
            *writeModeOut = [NSString stringWithFormat:@"exception=%@", exception];
        }
        return NO;
    }
}

static NSArray *ui_bestVisiblePriceInputViews(id rootView) {
    if (!rootView) {
        return @[];
    }

    NSMutableArray *views = [NSMutableArray array];
    NSArray *roots = ui_candidateRootViewsForInspection(rootView);
    for (id rootCandidate in roots) {
        [views addObject:rootCandidate];
        [views addObjectsFromArray:ui_allDescendantViews(rootCandidate)];
    }

    NSMutableArray *candidates = [NSMutableArray array];
    NSMutableSet *seen = [NSMutableSet set];
    for (id candidate in views) {
        if (!candidate || !ui_viewIsVisible(candidate)) {
            continue;
        }

        @try {
            id directInput = ui_resolveDirectTextInputView(candidate) ?: candidate;
            if (!directInput) {
                continue;
            }

            NSValue *key = [NSValue valueWithNonretainedObject:directInput];
            if ([seen containsObject:key]) {
                continue;
            }

            CGRect frame = ui_viewFrameInScreen(directInput);
            if (frame.origin.y < 140.0 || frame.size.width < 40.0 || frame.size.width > 260.0 || frame.size.height < 24.0) {
                continue;
            }

            BOOL canWrite = [directInput respondsToSelector:NSSelectorFromString(@"setText:")] ||
                [directInput respondsToSelector:NSSelectorFromString(@"insertText:")] ||
                [directInput respondsToSelector:NSSelectorFromString(@"deleteBackward")];
            if (!canWrite) {
                continue;
            }

            NSString *className = NSStringFromClass([directInput class]) ?: @"";
            NSString *metadata = ui_viewMetadataText(directInput);
            if (ui_stringLooksLikeSearch(metadata)) {
                continue;
            }

            NSInteger score = 0;
            if ([className containsString:@"TextField"] || [className containsString:@"Input"] || [className containsString:@"Edit"]) {
                score += 180;
            }
            if (ui_stringContainsAnyKeyword(metadata, @[@"价格", @"最低", @"最高", @"最小", @"最大", @"min", @"max"])) {
                score += 200;
            }
            if (frame.origin.y >= 180.0 && frame.origin.y <= 460.0) {
                score += 60;
            }
            if (frame.size.width >= 60.0 && frame.size.width <= 180.0) {
                score += 30;
            }
            if (score < 150) {
                continue;
            }

            [seen addObject:key];
            [candidates addObject:@{
                @"view": directInput,
                @"score": @(score),
                @"x": @(frame.origin.x),
                @"mid_y": @(CGRectGetMidY(frame)),
            }];
        } @catch (NSException *exception) {
        }
    }

    if (candidates.count == 0) {
        return @[];
    }

    NSArray *sorted = [candidates sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *lhs, NSDictionary *rhs) {
        CGFloat leftY = [lhs[@"mid_y"] doubleValue];
        CGFloat rightY = [rhs[@"mid_y"] doubleValue];
        if (fabs(leftY - rightY) > 10.0) {
            return leftY < rightY ? NSOrderedAscending : NSOrderedDescending;
        }
        CGFloat leftX = [lhs[@"x"] doubleValue];
        CGFloat rightX = [rhs[@"x"] doubleValue];
        if (fabs(leftX - rightX) > 4.0) {
            return leftX < rightX ? NSOrderedAscending : NSOrderedDescending;
        }
        NSInteger leftScore = [lhs[@"score"] integerValue];
        NSInteger rightScore = [rhs[@"score"] integerValue];
        return leftScore > rightScore ? NSOrderedAscending : NSOrderedDescending;
    }];

    NSDictionary *bestLeft = nil;
    NSDictionary *bestRight = nil;
    NSInteger bestPairScore = NSIntegerMin;
    for (NSInteger leftIndex = 0; leftIndex < (NSInteger)sorted.count; leftIndex++) {
        NSDictionary *leftCandidate = sorted[leftIndex];
        for (NSInteger rightIndex = leftIndex + 1; rightIndex < (NSInteger)sorted.count; rightIndex++) {
            NSDictionary *rightCandidate = sorted[rightIndex];
            CGFloat deltaY = fabs([leftCandidate[@"mid_y"] doubleValue] - [rightCandidate[@"mid_y"] doubleValue]);
            if (deltaY > 64.0) {
                if ([rightCandidate[@"mid_y"] doubleValue] > [leftCandidate[@"mid_y"] doubleValue]) {
                    break;
                }
                continue;
            }
            NSInteger pairScore = [leftCandidate[@"score"] integerValue] + [rightCandidate[@"score"] integerValue] - (NSInteger)deltaY;
            if (pairScore > bestPairScore) {
                bestPairScore = pairScore;
                if ([leftCandidate[@"x"] doubleValue] <= [rightCandidate[@"x"] doubleValue]) {
                    bestLeft = leftCandidate;
                    bestRight = rightCandidate;
                } else {
                    bestLeft = rightCandidate;
                    bestRight = leftCandidate;
                }
            }
        }
    }

    if (bestLeft && bestRight) {
        return @[bestLeft[@"view"], bestRight[@"view"]];
    }

    NSDictionary *firstCandidate = sorted.firstObject;
    return firstCandidate ? @[firstCandidate[@"view"]] : @[];
}

static id ui_bestPriceFilterConfirmButton(id rootView) {
    if (!rootView) {
        return nil;
    }

    NSArray *keywords = @[@"确定", @"确认", @"完成", @"查看商品", @"查看结果"];
    NSMutableArray *candidates = [NSMutableArray array];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIButton")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIControl")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UILabel")];

    NSMutableSet *seen = [NSMutableSet set];
    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;
    for (id candidate in candidates) {
        if (!candidate || !ui_viewIsVisible(candidate)) {
            continue;
        }

        @try {
            NSValue *key = [NSValue valueWithNonretainedObject:candidate];
            if ([seen containsObject:key]) {
                continue;
            }
            [seen addObject:key];

            CGRect frame = ui_viewFrameInScreen(candidate);
            if (frame.origin.y < 180.0 || frame.size.width < 48.0 || frame.size.height < 24.0) {
                continue;
            }

            NSString *metadata = ui_viewMetadataText(candidate);
            if (!ui_stringContainsAnyKeyword(metadata, keywords)) {
                continue;
            }

            NSString *className = NSStringFromClass([candidate class]) ?: @"";
            NSInteger score = 260;
            if ([className containsString:@"Button"]) {
                score += 120;
            }
            if ([className containsString:@"Control"]) {
                score += 80;
            }
            score += (NSInteger)CGRectGetMidY(frame);
            score += (NSInteger)(CGRectGetMidX(frame) / 2.0);
            for (NSString *keyword in keywords) {
                if ([metadata isEqualToString:keyword]) {
                    score += 120;
                    break;
                }
            }

            if (score > bestScore) {
                bestScore = score;
                bestView = candidate;
            }
        } @catch (NSException *exception) {
        }
    }

    return bestView;
}

static BOOL ui_searchPageHasOpenPriceFilterPanel(id rootView) {
    if (!rootView) {
        return NO;
    }

    @try {
        NSArray *roots = ui_candidateRootViewsForInspection(rootView);
        for (id rootCandidate in roots) {
            NSArray *panelViews = ui_findAllViewsOfClass(rootCandidate, @"PDDSRInnerFilterView");
            for (id panelView in panelViews) {
                if (ui_viewIsVisible(panelView)) {
                    return YES;
                }
            }
        }
    } @catch (NSException *exception) {
    }

    return ui_bestPriceFilterConfirmButton(rootView) != nil;
}

static BOOL ui_viewTreeHasFirstResponder(id view) {
    if (!view) {
        return NO;
    }

    @try {
        SEL isFirstResponderSel = NSSelectorFromString(@"isFirstResponder");
        if ([view respondsToSelector:isFirstResponderSel] &&
            ((BOOL (*)(id, SEL))objc_msgSend)(view, isFirstResponderSel)) {
            return YES;
        }

        SEL subviewsSel = NSSelectorFromString(@"subviews");
        if (![view respondsToSelector:subviewsSel]) {
            return NO;
        }

        NSArray *subviews = ((NSArray * (*)(id, SEL))objc_msgSend)(view, subviewsSel);
        for (id subview in subviews) {
            if (ui_viewTreeHasFirstResponder(subview)) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL ui_tryDismissKeyboardFromViewTree(id rootView, NSString **traceOut) {
    if (!rootView) {
        if (traceOut) {
            *traceOut = @"dismiss_keyboard_root=nil";
        }
        return YES;
    }

    BOOL beforeHasFirstResponder = ui_viewTreeHasFirstResponder(rootView);
    BOOL didRequestEndEditing = NO;
    BOOL didResignAnyResponder = NO;

    @try {
        NSArray *roots = ui_candidateRootViewsForInspection(rootView);
        for (id rootCandidate in roots) {
            if (!rootCandidate) {
                continue;
            }

            SEL endEditingSel = NSSelectorFromString(@"endEditing:");
            if ([rootCandidate respondsToSelector:endEditingSel]) {
                ((BOOL (*)(id, SEL, BOOL))objc_msgSend)(rootCandidate, endEditingSel, YES);
                didRequestEndEditing = YES;
            }

            NSArray *inputViews = ui_findAllViewsOfClass(rootCandidate, @"UITextField");
            for (id inputView in inputViews) {
                SEL isFirstResponderSel = NSSelectorFromString(@"isFirstResponder");
                SEL resignFirstResponderSel = NSSelectorFromString(@"resignFirstResponder");
                if (![inputView respondsToSelector:isFirstResponderSel] ||
                    ![inputView respondsToSelector:resignFirstResponderSel]) {
                    continue;
                }
                if (((BOOL (*)(id, SEL))objc_msgSend)(inputView, isFirstResponderSel)) {
                    ((BOOL (*)(id, SEL))objc_msgSend)(inputView, resignFirstResponderSel);
                    didResignAnyResponder = YES;
                }
            }
        }
    } @catch (NSException *exception) {
    }

    BOOL afterHasFirstResponder = ui_viewTreeHasFirstResponder(rootView);
    if (traceOut) {
        *traceOut = [NSString stringWithFormat:
            @"keyboard_before=%@\nrequested_end_editing=%@\nresigned_first_responder=%@\nkeyboard_after=%@",
            beforeHasFirstResponder ? @"true" : @"false",
            didRequestEndEditing ? @"true" : @"false",
            didResignAnyResponder ? @"true" : @"false",
            afterHasFirstResponder ? @"true" : @"false"];
    }
    return !afterHasFirstResponder;
}

static NSString *ui_controlActionSummary(id control) {
    if (!control) {
        return @"control_actions=<nil>";
    }

    @try {
        SEL allTargetsSel = NSSelectorFromString(@"allTargets");
        SEL actionsForTargetSel = NSSelectorFromString(@"actionsForTarget:forControlEvent:");
        if (![control respondsToSelector:allTargetsSel] || ![control respondsToSelector:actionsForTargetSel]) {
            return [NSString stringWithFormat:@"control_actions=unsupported %@", ui_viewSummaryText(control)];
        }

        NSSet *targets = ((NSSet * (*)(id, SEL))objc_msgSend)(control, allTargetsSel);
        NSMutableArray *parts = [NSMutableArray array];
        NSArray<NSNumber *> *events = @[@64];
        for (id target in targets) {
            NSString *targetClass = target ? (NSStringFromClass([target class]) ?: @"<unknown>") : @"<nil>";
            for (NSNumber *eventValue in events) {
                NSArray *actions = ((NSArray * (*)(id, SEL, id, NSUInteger))objc_msgSend)(control, actionsForTargetSel, target, [eventValue unsignedIntegerValue]);
                if ([actions isKindOfClass:[NSArray class]] && actions.count > 0) {
                    [parts addObject:[NSString stringWithFormat:@"target=%@ event=%@ actions=%@",
                        targetClass,
                        eventValue,
                        [actions componentsJoinedByString:@","]]];
                }
            }
        }
        return parts.count > 0 ?
            [parts componentsJoinedByString:@"\n"] :
            [NSString stringWithFormat:@"control_actions=none %@", ui_viewSummaryText(control)];
    } @catch (NSException *exception) {
        return [NSString stringWithFormat:@"control_actions_exception=%@", exception];
    }
}

static BOOL ui_tryActivatePriceFilterConfirmControl(id view, NSString **traceOut) {
    if (!view) {
        if (traceOut) {
            *traceOut = @"confirm_control=nil";
        }
        return NO;
    }

    SEL superviewSel = NSSelectorFromString(@"superview");
    SEL sendActionsSel = NSSelectorFromString(@"sendActionsForControlEvents:");
    NSArray<NSNumber *> *events = @[@64];
    id current = view;
    NSInteger depth = 0;

    while (current && depth < 4) {
        @try {
            if ([current respondsToSelector:sendActionsSel]) {
                NSMutableArray *sentEvents = [NSMutableArray array];
                for (NSNumber *eventValue in events) {
                    ((void (*)(id, SEL, NSUInteger))objc_msgSend)(current, sendActionsSel, [eventValue unsignedIntegerValue]);
                    [sentEvents addObject:[eventValue stringValue]];
                }
                if (traceOut) {
                    *traceOut = [NSString stringWithFormat:
                        @"confirm_control=%@\nconfirm_control_depth=%ld\nsent_control_events=%@\n%@",
                        ui_viewSummaryText(current),
                        (long)depth,
                        [sentEvents componentsJoinedByString:@","],
                        ui_controlActionSummary(current)];
                }
                return YES;
            }

            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        } @catch (NSException *exception) {
            if (traceOut) {
                *traceOut = [NSString stringWithFormat:@"confirm_control_exception=%@", exception];
            }
            return NO;
        }
    }

    if (traceOut) {
        *traceOut = [NSString stringWithFormat:@"confirm_control_not_found view=%@", ui_viewSummaryText(view)];
    }
    return NO;
}

static BOOL ui_tryFillPriceFilterInputsOnSearchPage(id rootView, NSString *priceMin, NSString *priceMax, NSString **traceOut, NSString **warningOut) {
    NSArray *inputViews = ui_bestVisiblePriceInputViews(rootView);
    if (inputViews.count < 2) {
        if (warningOut) {
            *warningOut = @"price_filter_inputs_not_found";
        }
        if (traceOut) {
            *traceOut = [NSString stringWithFormat:@"price_input_count=%ld\n%@",
                (long)inputViews.count,
                debug_searchPriceFilterPanelProbeText(rootView)];
        }
        return NO;
    }

    id minInput = inputViews[0];
    id maxInput = inputViews[1];
    NSString *minWriteMode = nil;
    NSString *maxWriteMode = nil;
    NSString *dismissTrace = nil;
    BOOL didWriteMin = ui_writeTextValueToInputView(minInput, priceMin ?: @"", &minWriteMode);
    BOOL didWriteMax = ui_writeTextValueToInputView(maxInput, priceMax ?: @"", &maxWriteMode);
    if (!didWriteMin || !didWriteMax) {
        if (warningOut) {
            *warningOut = @"price_filter_input_write_failed";
        }
        if (traceOut) {
            *traceOut = [NSString stringWithFormat:
                @"min_input=%@\nmax_input=%@\nmin_write_mode=%@\nmax_write_mode=%@\ndid_write_min=%@\ndid_write_max=%@",
                ui_viewSummaryText(minInput),
                ui_viewSummaryText(maxInput),
                minWriteMode ?: @"none",
                maxWriteMode ?: @"none",
                didWriteMin ? @"true" : @"false",
                didWriteMax ? @"true" : @"false"];
        }
        return NO;
    }

    ui_tryDismissKeyboardFromViewTree(rootView, &dismissTrace);

    if (traceOut) {
        *traceOut = [NSString stringWithFormat:
            @"min_input=%@\nmax_input=%@\nmin_write_mode=%@\nmax_write_mode=%@\ndid_write_min=%@\ndid_write_max=%@\n%@",
            ui_viewSummaryText(minInput),
            ui_viewSummaryText(maxInput),
            minWriteMode ?: @"none",
            maxWriteMode ?: @"none",
            didWriteMin ? @"true" : @"false",
            didWriteMax ? @"true" : @"false",
            dismissTrace ?: @""];
    }
    return YES;
}

static BOOL ui_tryConfirmPriceFilterOnSearchPage(id rootView, NSString **traceOut, NSString **warningOut) {
    NSString *dismissTrace = nil;
    BOOL keyboardDismissed = ui_tryDismissKeyboardFromViewTree(rootView, &dismissTrace);
    id confirmButton = ui_bestPriceFilterConfirmButton(rootView);
    if (!keyboardDismissed) {
        if (traceOut) {
            *traceOut = [NSString stringWithFormat:
                @"confirm_button=%@\ndid_tap_confirm=false\npanel_open=%@\n%@",
                ui_viewSummaryText(confirmButton),
                ui_searchPageHasOpenPriceFilterPanel(rootView) ? @"true" : @"false",
                dismissTrace ?: @""];
        }
        if (warningOut) {
            *warningOut = @"price_filter_keyboard_not_dismissed";
        }
        return NO;
    }

    NSString *controlTrace = nil;
    BOOL didTapConfirm = ui_tryActivatePriceFilterConfirmControl(confirmButton, &controlTrace);
    if (!didTapConfirm) {
        didTapConfirm = ui_tryTapViewWithFallback(confirmButton);
    }
    if (traceOut) {
        *traceOut = [NSString stringWithFormat:
            @"confirm_button=%@\ndid_tap_confirm=%@\npanel_open=%@\n%@\n%@",
            ui_viewSummaryText(confirmButton),
            didTapConfirm ? @"true" : @"false",
            ui_searchPageHasOpenPriceFilterPanel(rootView) ? @"true" : @"false",
            dismissTrace ?: @"",
            controlTrace ?: @""];
    }
    if (!didTapConfirm && warningOut) {
        *warningOut = @"price_filter_confirm_not_found";
    }
    return didTapConfirm;
}

static NSArray *ui_sortedVisibleCellsInListView(id listView) {
    if (!listView) {
        return @[];
    }

    @try {
        SEL visibleCellsSel = NSSelectorFromString(@"visibleCells");
        if (![listView respondsToSelector:visibleCellsSel]) {
            return @[];
        }

        NSArray *visibleCells = ((NSArray * (*)(id, SEL))objc_msgSend)(listView, visibleCellsSel);
        NSMutableArray *filteredCells = [NSMutableArray array];
        for (id cell in visibleCells) {
            if (!ui_viewIsVisible(cell)) {
                continue;
            }
            CGRect frame = ui_viewFrameInScreen(cell);
            if (frame.origin.y < 120.0 || frame.size.width < 80.0 || frame.size.height < 80.0) {
                continue;
            }
            [filteredCells addObject:cell];
        }

        [filteredCells sortUsingComparator:^NSComparisonResult(id lhs, id rhs) {
            CGRect leftFrame = ui_viewFrameInScreen(lhs);
            CGRect rightFrame = ui_viewFrameInScreen(rhs);
            if (fabs(leftFrame.origin.y - rightFrame.origin.y) > 8.0) {
                return leftFrame.origin.y < rightFrame.origin.y ? NSOrderedAscending : NSOrderedDescending;
            }
            if (fabs(leftFrame.origin.x - rightFrame.origin.x) > 8.0) {
                return leftFrame.origin.x < rightFrame.origin.x ? NSOrderedAscending : NSOrderedDescending;
            }
            return NSOrderedSame;
        }];
        return filteredCells;
    } @catch (NSException *exception) {
        return @[];
    }
}

static NSArray *ui_sortedVisibleCellLikeViews(id rootView) {
    if (!rootView) {
        return @[];
    }

    NSMutableArray *cellCandidates = [NSMutableArray array];
    NSArray *allCells = ui_findAllViewsOfClass(rootView, @"Cell");
    for (id candidate in allCells) {
        if (!ui_viewIsVisible(candidate)) {
            continue;
        }

        CGRect frame = ui_viewFrameInScreen(candidate);
        if (frame.origin.y < 120.0 || frame.size.width < 80.0 || frame.size.height < 80.0) {
            continue;
        }
        [cellCandidates addObject:candidate];
    }

    [cellCandidates sortUsingComparator:^NSComparisonResult(id lhs, id rhs) {
        CGRect leftFrame = ui_viewFrameInScreen(lhs);
        CGRect rightFrame = ui_viewFrameInScreen(rhs);
        if (fabs(leftFrame.origin.y - rightFrame.origin.y) > 8.0) {
            return leftFrame.origin.y < rightFrame.origin.y ? NSOrderedAscending : NSOrderedDescending;
        }
        if (fabs(leftFrame.origin.x - rightFrame.origin.x) > 8.0) {
            return leftFrame.origin.x < rightFrame.origin.x ? NSOrderedAscending : NSOrderedDescending;
        }
        return NSOrderedSame;
    }];

    return cellCandidates;
}

static NSArray *ui_visibleSearchCells(id rootView) {
    if (!rootView || !ui_rootViewLooksLikeSearchPage(rootView)) {
        return @[];
    }

    @try {
        id resultsListView = ui_bestResultsListView(rootView);
        if (resultsListView) {
            NSArray *cells = ui_sortedVisibleCellsInListView(resultsListView);
            if (cells.count > 0) {
                return cells;
            }
        }
    } @catch (NSException *exception) {
    }

    return ui_sortedVisibleCellLikeViews(rootView);
}

static NSString *task_resolvedGoodsIdForSearchCell(id cell) {
    NSDictionary *searchPayloadSnapshot = task_searchPayloadSnapshot();
    if (!task_hasActiveTask() || !cell || ![searchPayloadSnapshot isKindOfClass:[NSDictionary class]] || searchPayloadSnapshot.count == 0) {
        return @"";
    }

    NSString *visibleText = ui_visibleTextSampleFromView(cell) ?: @"";
    NSString *normalizedVisibleText = capture_normalizedTextForMatching(visibleText);
    NSArray *imageURLs = ui_collectImageURLs(cell);
    NSMutableSet *visibleImageSet = [NSMutableSet set];
    for (id value in imageURLs) {
        if ([value isKindOfClass:[NSString class]] && [(NSString *)value length] > 0) {
            [visibleImageSet addObject:value];
        }
    }

    NSString *matchedGoodsId = @"";
    NSInteger matchedCount = 0;
    NSEnumerator *goodsIdEnumerator = [searchPayloadSnapshot keyEnumerator];
    NSString *goodsId = nil;
    while ((goodsId = [goodsIdEnumerator nextObject])) {
        NSDictionary *snapshot = [searchPayloadSnapshot[goodsId] isKindOfClass:[NSDictionary class]] ? searchPayloadSnapshot[goodsId] : nil;
        if (!snapshot) {
            continue;
        }

        NSString *itemImg = [snapshot[@"itemImg"] isKindOfClass:[NSString class]] ? snapshot[@"itemImg"] : @"";
        BOOL imageMatched = (itemImg.length > 0 && [visibleImageSet containsObject:itemImg]);
        BOOL titleMatched = task_normalizedTextContainsNeedlePrefix(normalizedVisibleText, [snapshot[@"title"] isKindOfClass:[NSString class]] ? snapshot[@"title"] : @"");

        BOOL priceMatched = NO;
        NSArray *priceTexts = @[
            [snapshot[@"price"] isKindOfClass:[NSString class]] ? snapshot[@"price"] : @"",
            [snapshot[@"priceDesc"] isKindOfClass:[NSString class]] ? snapshot[@"priceDesc"] : @"",
            [snapshot[@"longPricePrefix"] isKindOfClass:[NSString class]] ? snapshot[@"longPricePrefix"] : @""
        ];
        for (NSString *priceText in priceTexts) {
            NSString *normalizedPrice = capture_normalizedTextForMatching(priceText);
            if (normalizedPrice.length >= 2 && [normalizedVisibleText containsString:normalizedPrice]) {
                priceMatched = YES;
                break;
            }
        }

        if (!imageMatched && !(titleMatched && priceMatched)) {
            continue;
        }

        matchedGoodsId = goodsId;
        matchedCount++;
        if (matchedCount > 1) {
            return @"";
        }
    }

    return matchedCount == 1 ? matchedGoodsId : @"";
}

static NSString *task_currentSearchPageSignature(id rootView) {
    NSArray *visibleCells = ui_visibleSearchCells(rootView);
    if (visibleCells.count == 0) {
        return @"";
    }

    NSMutableArray<NSString *> *components = [NSMutableArray array];
    NSInteger maxCount = MIN((NSInteger)visibleCells.count, 6);
    for (NSInteger index = 0; index < maxCount; index++) {
        id cell = visibleCells[index];
        NSString *goodsId = task_resolvedGoodsIdForSearchCell(cell);
        if (goodsId.length > 0) {
            [components addObject:[NSString stringWithFormat:@"id:%@", goodsId]];
            continue;
        }

        NSString *normalizedText = capture_normalizedTextForMatching(ui_visibleTextSampleFromView(cell) ?: @"");
        if (normalizedText.length == 0) {
            normalizedText = [NSString stringWithFormat:@"cell_%ld", (long)index];
        }
        if (normalizedText.length > 24) {
            normalizedText = [normalizedText substringToIndex:24];
        }
        [components addObject:[NSString stringWithFormat:@"txt:%@", normalizedText]];
    }

    return [components componentsJoinedByString:@"|"];
}

static id ui_bestResultsListView(id rootView) {
    if (!rootView) {
        return nil;
    }

    NSMutableArray *lists = [NSMutableArray array];
    [lists addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UICollectionView")];
    [lists addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UITableView")];

    id bestView = nil;
    NSInteger bestScore = NSIntegerMin;
    for (id listView in lists) {
        if (!ui_viewIsVisible(listView)) {
            continue;
        }

        NSArray *cells = ui_sortedVisibleCellsInListView(listView);
        if (cells.count == 0) {
            continue;
        }

        CGRect frame = ui_viewFrameInScreen(listView);
        NSInteger score = (NSInteger)cells.count * 1000;
        if (frame.origin.y > 100.0) {
            score += 200;
        }
        if (frame.size.height > 240.0) {
            score += 100;
        }
        score += (NSInteger)MIN(frame.size.height, 800.0);

        if (score > bestScore) {
            bestScore = score;
            bestView = listView;
        }
    }

    return bestView;
}

static BOOL ui_selectProductFromListView(id listView, NSInteger index) {
    if (!listView || index < 0) {
        return NO;
    }

    @try {
        NSArray *cells = ui_sortedVisibleCellsInListView(listView);
        if (index >= (NSInteger)cells.count) {
            return NO;
        }

        id cell = cells[index];
        NSString *listClassName = NSStringFromClass([listView class]) ?: @"";
        NSLog(@"[AutoSearch] 使用列表视图点击商品: %@ index=%ld cell=%@", listClassName, (long)index, NSStringFromClass([cell class]));

        if ([listClassName containsString:@"CollectionView"]) {
            SEL indexPathForCellSel = NSSelectorFromString(@"indexPathForCell:");
            id indexPath = [listView respondsToSelector:indexPathForCellSel] ? ((id (*)(id, SEL, id))objc_msgSend)(listView, indexPathForCellSel, cell) : nil;
            if (indexPath) {
                SEL selectItemSel = NSSelectorFromString(@"selectItemAtIndexPath:animated:scrollPosition:");
                if ([listView respondsToSelector:selectItemSel]) {
                    ((void (*)(id, SEL, id, BOOL, NSInteger))objc_msgSend)(listView, selectItemSel, indexPath, YES, 0);
                }

                SEL delegateSel = NSSelectorFromString(@"delegate");
                id delegate = [listView respondsToSelector:delegateSel] ? ((id (*)(id, SEL))objc_msgSend)(listView, delegateSel) : nil;
                SEL didSelectSel = NSSelectorFromString(@"collectionView:didSelectItemAtIndexPath:");
                if (delegate && [delegate respondsToSelector:didSelectSel]) {
                    ((void (*)(id, SEL, id, id))objc_msgSend)(delegate, didSelectSel, listView, indexPath);
                    return YES;
                }
            }
        }

        if ([listClassName containsString:@"TableView"]) {
            SEL indexPathForCellSel = NSSelectorFromString(@"indexPathForCell:");
            id indexPath = [listView respondsToSelector:indexPathForCellSel] ? ((id (*)(id, SEL, id))objc_msgSend)(listView, indexPathForCellSel, cell) : nil;
            if (indexPath) {
                SEL selectRowSel = NSSelectorFromString(@"selectRowAtIndexPath:animated:scrollPosition:");
                if ([listView respondsToSelector:selectRowSel]) {
                    ((void (*)(id, SEL, id, BOOL, NSInteger))objc_msgSend)(listView, selectRowSel, indexPath, YES, 0);
                }

                SEL delegateSel = NSSelectorFromString(@"delegate");
                id delegate = [listView respondsToSelector:delegateSel] ? ((id (*)(id, SEL))objc_msgSend)(listView, delegateSel) : nil;
                SEL didSelectSel = NSSelectorFromString(@"tableView:didSelectRowAtIndexPath:");
                if (delegate && [delegate respondsToSelector:didSelectSel]) {
                    ((void (*)(id, SEL, id, id))objc_msgSend)(delegate, didSelectSel, listView, indexPath);
                    return YES;
                }
            }
        }

        return ui_sendTapActionToView(cell);
    } @catch (NSException *exception) {
        NSLog(@"[AutoSearch] 列表点击失败: %@", exception);
        return NO;
    }
}

static BOOL ui_tapVisibleCellLikeView(id rootView, NSInteger index) {
    if (!rootView || index < 0) {
        return NO;
    }

    NSArray *cellCandidates = ui_sortedVisibleCellLikeViews(rootView);

    if (index >= (NSInteger)cellCandidates.count) {
        return NO;
    }

    id candidate = cellCandidates[index];
    NSLog(@"[AutoSearch] 使用通用 Cell 点击商品: %@ index=%ld", NSStringFromClass([candidate class]), (long)index);
    return ui_sendTapActionToView(candidate);
}

static BOOL ui_tryTapBackButton(id rootView) {
    if (!rootView) {
        return NO;
    }

    NSMutableArray *candidates = [NSMutableArray array];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIButton")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UIControl")];
    [candidates addObjectsFromArray:ui_findAllViewsOfClass(rootView, @"UILabel")];

    for (id candidate in candidates) {
        if (!ui_viewIsVisible(candidate)) {
            continue;
        }

        CGRect frame = ui_viewFrameInScreen(candidate);
        if (frame.origin.x > 90.0 || frame.origin.y > 120.0 || frame.size.width > 120.0 || frame.size.height > 80.0) {
            continue;
        }

        NSString *metadata = ui_viewMetadataText(candidate);
        BOOL looksLikeBack = ([metadata containsString:@"返回"] ||
                              [metadata localizedCaseInsensitiveContainsString:@"back"] ||
                              [metadata containsString:@"关闭"]);
        if (!looksLikeBack && frame.origin.x > 50.0) {
            continue;
        }

        if (ui_sendTapActionToView(candidate)) {
            NSLog(@"[AutoSearch] 使用按钮返回: %@ %@", NSStringFromClass([candidate class]), metadata);
            return YES;
        }
    }

    return NO;
}

static BOOL ui_tapAtPoint(CGPoint point) {
    @try {
        id rootView = ui_currentRootView();
        id window = nil;
        SEL windowSel = NSSelectorFromString(@"window");
        if (rootView && [rootView respondsToSelector:windowSel]) {
            window = ((id (*)(id, SEL))objc_msgSend)(rootView, windowSel);
        }

        if (!window) {
            Class UIApplicationClass = NSClassFromString(@"UIApplication");
            id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication"));
            SEL keyWindowSel = NSSelectorFromString(@"keyWindow");
            if (app && [app respondsToSelector:keyWindowSel]) {
                window = ((id (*)(id, SEL))objc_msgSend)(app, keyWindowSel);
            }
        }

        if (!window) {
            return NO;
        }

        SEL hitTestSel = NSSelectorFromString(@"hitTest:withEvent:");
        if (![window respondsToSelector:hitTestSel]) {
            return NO;
        }

        id targetView = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(window, hitTestSel, point, nil);
        if (!targetView) {
            return NO;
        }

        NSLog(@"[AutoSearch] 命中坐标视图: %@ point=(%.0f, %.0f)", NSStringFromClass([targetView class]), point.x, point.y);
        return ui_sendTapActionToView(targetView);
    } @catch (NSException *exception) {
        return NO;
    }
}

static CGRect ui_currentInteractiveFrame(void) {
    id rootView = ui_currentRootView();
    CGRect rootFrame = ui_viewFrameInScreen(rootView);
    if (!CGRectIsEmpty(rootFrame) && rootFrame.size.width > 10.0 && rootFrame.size.height > 10.0) {
        return rootFrame;
    }

    Class UIScreenClass = NSClassFromString(@"UIScreen");
    id screen = ((id (*)(Class, SEL))objc_msgSend)(UIScreenClass, NSSelectorFromString(@"mainScreen"));
    CGRect screenBounds = screen ? ((CGRect (*)(id, SEL))objc_msgSend)(screen, NSSelectorFromString(@"bounds")) : CGRectZero;
    return screenBounds;
}

static BOOL ui_tryTapProductAtIndexByPoint(NSInteger index) {
    CGRect interactiveFrame = ui_currentInteractiveFrame();
    if (CGRectIsEmpty(interactiveFrame)) {
        return NO;
    }

    CGFloat minX = CGRectGetMinX(interactiveFrame);
    CGFloat minY = CGRectGetMinY(interactiveFrame);
    CGFloat width = CGRectGetWidth(interactiveFrame);
    CGFloat height = CGRectGetHeight(interactiveFrame);
    NSInteger row = MAX(index / 2, 0);
    CGFloat gridStartY = MIN(MAX(240.0, height * 0.27), 320.0);
    CGFloat gridRowHeight = MIN(MAX(220.0, height * 0.22), 300.0);
    CGFloat leftX = minX + width * 0.28;
    CGFloat rightX = minX + width * 0.72;
    CGFloat centerX = minX + width * 0.50;
    CGFloat rowCenterY = minY + gridStartY + row * gridRowHeight;

    CGPoint candidatePoints[] = {
        CGPointMake((index % 2 == 0) ? leftX : rightX, rowCenterY),
        CGPointMake(centerX, rowCenterY),
        CGPointMake((index % 2 == 0) ? rightX : leftX, rowCenterY),
        CGPointMake(centerX, rowCenterY + 60.0)
    };
    for (NSInteger idx = 0; idx < 4; idx++) {
        if (ui_tapAtPoint(candidatePoints[idx])) {
            return YES;
        }
    }
    return NO;
}

static void debug_writeTaskDump(NSString *category, NSString *detail) {
    if (!task_hasActiveTask()) {
        return;
    }

    @try {
        NSMutableString *output = [NSMutableString string];
        [output appendFormat:@"timestamp=%lld\n", (long long)[[NSDate date] timeIntervalSince1970]];
        [output appendFormat:@"task_id=%@\n", taskCurrentId ?: @""];
        [output appendFormat:@"keyword=%@\n", taskCurrentKeyword ?: @""];
        [output appendFormat:@"category=%@\n", category ?: @"ui"];
        [output appendFormat:@"state=%@\n", taskCurrentState ?: @"idle"];
        [output appendFormat:@"saved_count=%ld\n", (long)taskSavedCount];
        [output appendFormat:@"attempted_count=%ld\n", (long)taskAttemptedCount];
        [output appendFormat:@"failed_count=%ld\n", (long)taskFailedCount];
        [output appendFormat:@"consecutive_failure_count=%ld\n", (long)taskConsecutiveFailureCount];
        [output appendFormat:@"duplicate_skip_count=%ld\n", (long)taskDuplicateSkipCount];
        [output appendFormat:@"unknown_entry_count=%ld\n", (long)taskUnknownEntryCount];
        [output appendFormat:@"no_new_scroll_rounds=%ld\n", (long)taskConsecutiveNoNewRounds];
        [output appendFormat:@"consecutive_unknown_or_click_failures=%ld\n", (long)taskConsecutiveUnknownOrClickFailures];
        [output appendFormat:@"needs_back_navigation=%@\n", taskNeedsBackNavigation ? @"true" : @"false"];
        if (taskStopReasonDetail.length > 0) {
            [output appendFormat:@"stop_reason_detail=%@\n", taskStopReasonDetail];
        }
        if (detail.length > 0) {
            [output appendFormat:@"\n[detail]\n%@\n", detail];
        }

        id rootVC = ui_currentRootViewController();
        id rootView = ui_currentRootView();
        [output appendFormat:@"\n[root]\nroot_vc=%@\nroot_view=%@\n",
         rootVC ? NSStringFromClass([rootVC class]) : @"<nil>",
         rootView ? NSStringFromClass([rootView class]) : @"<nil>"];

        [output appendString:@"\n[visible_view_hierarchy]\n"];
        debug_appendViewHierarchy(output, rootView, 0, 6);

        NSString *safeCategory = [[category ?: @"ui" stringByReplacingOccurrencesOfString:@"/" withString:@"_"]
            stringByReplacingOccurrencesOfString:@" " withString:@"_"];
        NSString *fileName = [NSString stringWithFormat:@"debug_%02ld_%@.txt", (long)++taskDebugSequence, safeCategory];
        NSString *filePath = task_filePath(fileName);
        if (filePath.length > 0) {
            [output writeToFile:filePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
        }
    } @catch (NSException *exception) {
    }
}

static void debug_appendViewHierarchy(NSMutableString *output, id view, NSInteger depth, NSInteger maxDepth) {
    if (!output) {
        return;
    }

    NSString *indent = [@"" stringByPaddingToLength:(NSUInteger)MAX(depth * 2, 0) withString:@" " startingAtIndex:0];
    if (!view) {
        [output appendFormat:@"%@<nil>\n", indent];
        return;
    }

    @try {
        NSString *className = NSStringFromClass([view class]) ?: @"<unknown>";
        CGRect frame = ui_viewFrameInScreen(view);
        NSString *metadata = [[ui_viewMetadataText(view) ?: @"" stringByReplacingOccurrencesOfString:@"\n" withString:@" "]
            stringByReplacingOccurrencesOfString:@"\r" withString:@" "];
        SEL userInteractionSel = NSSelectorFromString(@"isUserInteractionEnabled");
        SEL enabledSel = NSSelectorFromString(@"isEnabled");
        BOOL userInteractionEnabled = [view respondsToSelector:userInteractionSel] ? ((BOOL (*)(id, SEL))objc_msgSend)(view, userInteractionSel) : YES;
        NSString *enabledText = @"n/a";
        if ([view respondsToSelector:enabledSel]) {
            enabledText = ((BOOL (*)(id, SEL))objc_msgSend)(view, enabledSel) ? @"true" : @"false";
        }

        NSArray *subviews = [view respondsToSelector:NSSelectorFromString(@"subviews")] ?
            ((NSArray * (*)(id, SEL))objc_msgSend)(view, NSSelectorFromString(@"subviews")) :
            nil;
        [output appendFormat:@"%@%@ frame=(%.1f,%.1f,%.1f,%.1f) visible=%@ enabled=%@ interaction=%@ subviews=%lu text=\"%@\"\n",
         indent,
         className,
         frame.origin.x,
         frame.origin.y,
         frame.size.width,
         frame.size.height,
         ui_viewIsVisible(view) ? @"true" : @"false",
         enabledText,
         userInteractionEnabled ? @"true" : @"false",
         (unsigned long)subviews.count,
         metadata];

        if (depth >= maxDepth || subviews.count == 0) {
            return;
        }

        NSInteger emitted = 0;
        for (id subview in subviews) {
            NSString *subviewClassName = NSStringFromClass([subview class]) ?: @"";
            NSString *subviewMetadata = ui_viewMetadataText(subview) ?: @"";
            BOOL shouldInclude = ui_viewIsVisible(subview) ||
                subviewMetadata.length > 0 ||
                [subviewClassName containsString:@"Search"] ||
                [subviewClassName containsString:@"TextField"] ||
                [subviewClassName containsString:@"Button"] ||
                [subviewClassName containsString:@"Label"] ||
                [subviewClassName containsString:@"Collection"] ||
                [subviewClassName containsString:@"Table"] ||
                [subviewClassName containsString:@"Cell"] ||
                [subviewClassName containsString:@"Scroll"];
            if (!shouldInclude) {
                continue;
            }

            debug_appendViewHierarchy(output, subview, depth + 1, maxDepth);
            emitted++;
            if (emitted >= 60) {
                [output appendFormat:@"%@  ... truncated ...\n", indent];
                break;
            }
        }
    } @catch (NSException *exception) {
        [output appendFormat:@"%@<exception %@>\n", indent, exception];
    }
}

static NSString *debug_hitTestDescription(CGPoint point) {
    @try {
        id rootView = ui_currentRootView();
        id window = nil;
        SEL windowSel = NSSelectorFromString(@"window");
        if (rootView && [rootView respondsToSelector:windowSel]) {
            window = ((id (*)(id, SEL))objc_msgSend)(rootView, windowSel);
        }

        if (!window) {
            Class UIApplicationClass = NSClassFromString(@"UIApplication");
            id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication"));
            SEL keyWindowSel = NSSelectorFromString(@"keyWindow");
            if (app && [app respondsToSelector:keyWindowSel]) {
                window = ((id (*)(id, SEL))objc_msgSend)(app, keyWindowSel);
            }
        }

        SEL hitTestSel = NSSelectorFromString(@"hitTest:withEvent:");
        if (!window || ![window respondsToSelector:hitTestSel]) {
            return [NSString stringWithFormat:@"point=(%.0f,%.0f) target=<no-window>", point.x, point.y];
        }

        id targetView = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(window, hitTestSel, point, nil);
        if (!targetView) {
            return [NSString stringWithFormat:@"point=(%.0f,%.0f) target=<nil>", point.x, point.y];
        }

        NSMutableArray *chain = [NSMutableArray array];
        id current = targetView;
        NSInteger depth = 0;
        while (current && depth < 6) {
            NSString *className = NSStringFromClass([current class]) ?: @"<unknown>";
            NSString *metadata = ui_viewMetadataText(current) ?: @"";
            CGRect frame = ui_viewFrameInScreen(current);
            [chain addObject:[NSString stringWithFormat:@"%@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\"",
                className, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height, metadata]];
            SEL superviewSel = NSSelectorFromString(@"superview");
            current = [current respondsToSelector:superviewSel] ? ((id (*)(id, SEL))objc_msgSend)(current, superviewSel) : nil;
            depth++;
        }

        return [NSString stringWithFormat:@"point=(%.0f,%.0f) chain=%@", point.x, point.y, [chain componentsJoinedByString:@" -> "]];
    } @catch (NSException *exception) {
        return [NSString stringWithFormat:@"point=(%.0f,%.0f) exception=%@", point.x, point.y, exception];
    }
}

static NSString *debug_searchHitTestText(void) {
    Class UIScreenClass = NSClassFromString(@"UIScreen");
    id screen = ((id (*)(Class, SEL))objc_msgSend)(UIScreenClass, NSSelectorFromString(@"mainScreen"));
    CGRect screenBounds = screen ? ((CGRect (*)(id, SEL))objc_msgSend)(screen, NSSelectorFromString(@"bounds")) : CGRectZero;
    if (CGRectIsEmpty(screenBounds)) {
        return @"search_points=<no-screen>";
    }

    CGFloat width = screenBounds.size.width;
    CGFloat searchBarY = MIN(MAX(92.0, screenBounds.size.height * 0.12), 150.0);
    CGPoint candidatePoints[] = {
        CGPointMake(width * 0.50, searchBarY),
        CGPointMake(width * 0.50, searchBarY + 36.0),
        CGPointMake(width - 36.0, searchBarY),
        CGPointMake(width * 0.18, searchBarY)
    };

    NSMutableArray *lines = [NSMutableArray array];
    for (NSInteger idx = 0; idx < 4; idx++) {
        [lines addObject:debug_hitTestDescription(candidatePoints[idx])];
    }
    return [lines componentsJoinedByString:@"\n"];
}

static NSString *debug_productHitTestText(NSInteger index) {
    Class UIScreenClass = NSClassFromString(@"UIScreen");
    id screen = ((id (*)(Class, SEL))objc_msgSend)(UIScreenClass, NSSelectorFromString(@"mainScreen"));
    CGRect screenBounds = screen ? ((CGRect (*)(id, SEL))objc_msgSend)(screen, NSSelectorFromString(@"bounds")) : CGRectZero;
    if (CGRectIsEmpty(screenBounds)) {
        return @"product_points=<no-screen>";
    }

    CGFloat width = screenBounds.size.width;
    CGFloat height = screenBounds.size.height;
    NSInteger row = MAX(index / 2, 0);
    CGFloat gridStartY = MIN(MAX(240.0, height * 0.27), 320.0);
    CGFloat gridRowHeight = MIN(MAX(220.0, height * 0.22), 300.0);
    CGFloat leftX = width * 0.28;
    CGFloat rightX = width * 0.72;
    CGFloat centerX = width * 0.50;
    CGFloat rowCenterY = gridStartY + row * gridRowHeight;

    CGPoint candidatePoints[] = {
        CGPointMake((index % 2 == 0) ? leftX : rightX, rowCenterY),
        CGPointMake(centerX, rowCenterY),
        CGPointMake((index % 2 == 0) ? rightX : leftX, rowCenterY),
        CGPointMake(centerX, rowCenterY + 60.0)
    };

    NSMutableArray *lines = [NSMutableArray array];
    for (NSInteger idx = 0; idx < 4; idx++) {
        [lines addObject:debug_hitTestDescription(candidatePoints[idx])];
    }
    return [lines componentsJoinedByString:@"\n"];
}

static NSString *debug_searchEntryText(id searchEntry) {
    if (!searchEntry) {
        return @"entry=<nil>";
    }

    NSMutableString *output = [NSMutableString string];
    CGRect frame = ui_viewFrameInScreen(searchEntry);
    [output appendFormat:@"entry=%@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\"\n",
        NSStringFromClass([searchEntry class]) ?: @"<unknown>",
        frame.origin.x,
        frame.origin.y,
        frame.size.width,
        frame.size.height,
        ui_viewMetadataText(searchEntry) ?: @""];
    [output appendFormat:@"entry_setText=%@\n", [searchEntry respondsToSelector:NSSelectorFromString(@"setText:")] ? @"true" : @"false"];
    [output appendFormat:@"entry_insertText=%@\n", [searchEntry respondsToSelector:NSSelectorFromString(@"insertText:")] ? @"true" : @"false"];
    [output appendFormat:@"entry_becomeFirstResponder=%@\n", [searchEntry respondsToSelector:NSSelectorFromString(@"becomeFirstResponder")] ? @"true" : @"false"];

    id editableView = ui_bestEditableSearchView(searchEntry);
    id directEditableView = ui_resolveDirectTextInputView(editableView ?: searchEntry);
    if (editableView) {
        CGRect editableFrame = ui_viewFrameInScreen(editableView);
        [output appendFormat:@"editable=%@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\" setText=%@ insertText=%@\n",
            NSStringFromClass([editableView class]) ?: @"<unknown>",
            editableFrame.origin.x,
            editableFrame.origin.y,
            editableFrame.size.width,
            editableFrame.size.height,
            ui_viewMetadataText(editableView) ?: @"",
            [editableView respondsToSelector:NSSelectorFromString(@"setText:")] ? @"true" : @"false",
            [editableView respondsToSelector:NSSelectorFromString(@"insertText:")] ? @"true" : @"false"];
    }
    if (directEditableView && directEditableView != editableView) {
        CGRect directFrame = ui_viewFrameInScreen(directEditableView);
        [output appendFormat:@"direct_editable=%@ frame=(%.0f,%.0f,%.0f,%.0f) text=\"%@\" setText=%@ insertText=%@\n",
            NSStringFromClass([directEditableView class]) ?: @"<unknown>",
            directFrame.origin.x,
            directFrame.origin.y,
            directFrame.size.width,
            directFrame.size.height,
            ui_viewMetadataText(directEditableView) ?: @"",
            [directEditableView respondsToSelector:NSSelectorFromString(@"setText:")] ? @"true" : @"false",
            [directEditableView respondsToSelector:NSSelectorFromString(@"insertText:")] ? @"true" : @"false"];
    }

    [output appendString:@"\n[entry_hierarchy]\n"];
    debug_appendViewHierarchy(output, searchEntry, 0, 4);
    [output appendString:@"\n[entry_debug]\n"];
    debug_appendInterestingView(output, searchEntry, @"entry");
    if (editableView && editableView != searchEntry) {
        debug_appendInterestingView(output, editableView, @"editable");
    }
    if (directEditableView && directEditableView != editableView) {
        debug_appendInterestingView(output, directEditableView, @"direct_editable");
    }

    if (!CGRectIsEmpty(frame)) {
        CGFloat midY = CGRectGetMidY(frame);
        NSArray *pointTexts = @[
            debug_hitTestDescription(CGPointMake(CGRectGetMidX(frame), midY)),
            debug_hitTestDescription(CGPointMake(CGRectGetMinX(frame) + MIN(36.0, frame.size.width * 0.18), midY)),
            debug_hitTestDescription(CGPointMake(CGRectGetMaxX(frame) - MIN(36.0, frame.size.width * 0.18), midY))
        ];
        [output appendString:@"\n[entry_points]\n"];
        for (NSString *pointText in pointTexts) {
            [output appendFormat:@"%@\n", pointText];
        }
    }

    return output;
}

static void debug_appendInterestingView(NSMutableString *output, id view, NSString *label) {
    if (!output || !view) {
        return;
    }

    @try {
        NSString *className = NSStringFromClass([view class]) ?: @"<unknown>";
        [output appendFormat:@"%@ class=%@ text=\"%@\"\n", label ?: @"view", className, ui_viewMetadataText(view) ?: @""];
        [output appendFormat:@"%@ responds setText=%@ insertText=%@ becomeFirstResponder=%@ sendActions=%@\n",
            label ?: @"view",
            [view respondsToSelector:NSSelectorFromString(@"setText:")] ? @"true" : @"false",
            [view respondsToSelector:NSSelectorFromString(@"insertText:")] ? @"true" : @"false",
            [view respondsToSelector:NSSelectorFromString(@"becomeFirstResponder")] ? @"true" : @"false",
            [view respondsToSelector:NSSelectorFromString(@"sendActionsForControlEvents:")] ? @"true" : @"false"];

        SEL gestureRecognizersSel = NSSelectorFromString(@"gestureRecognizers");
        if ([view respondsToSelector:gestureRecognizersSel]) {
            NSArray *gestureRecognizers = ((NSArray * (*)(id, SEL))objc_msgSend)(view, gestureRecognizersSel);
            [output appendFormat:@"%@ gestures=%lu\n", label ?: @"view", (unsigned long)gestureRecognizers.count];
            NSInteger gestureIndex = 0;
            for (id gesture in gestureRecognizers) {
                if (gestureIndex >= 6) {
                    [output appendFormat:@"%@ gestures=... truncated ...\n", label ?: @"view"];
                    break;
                }
                NSString *gestureClass = NSStringFromClass([gesture class]) ?: @"<unknown>";
                NSMutableArray *gestureTargetsText = [NSMutableArray array];
                @try {
                    NSArray *targets = [gesture valueForKey:@"_targets"];
                    for (id gestureTarget in targets) {
                        id target = [gestureTarget valueForKey:@"target"];
                        id actionValue = [gestureTarget valueForKey:@"action"];
                        NSString *targetClass = target ? (NSStringFromClass([target class]) ?: @"<unknown>") : @"<nil>";
                        NSString *actionName = [NSString stringWithFormat:@"%@", actionValue ?: @""];
                        if (actionName.length > 0) {
                            [gestureTargetsText addObject:[NSString stringWithFormat:@"%@.%@", targetClass, actionName]];
                        }
                    }
                } @catch (NSException *exception) {
                }
                [output appendFormat:@"%@ gesture[%ld]=%@ targets=%@\n",
                    label ?: @"view",
                    (long)gestureIndex,
                    gestureClass,
                    gestureTargetsText.count > 0 ? [gestureTargetsText componentsJoinedByString:@", "] : @"<none>"];
                gestureIndex++;
            }
        }

        unsigned int methodCount = 0;
        Method *methods = class_copyMethodList([view class], &methodCount);
        if (methods) {
            NSArray *keywords = @[
                @"search",
                @"tap",
                @"click",
                @"select",
                @"sort",
                @"filter",
                @"order",
                @"text",
                @"edit",
                @"input",
                @"focus",
                @"begin",
                @"open",
                @"show",
                @"keyword",
                @"return",
                @"item"
            ];
            NSMutableArray *matchedMethods = [NSMutableArray array];
            for (unsigned int idx = 0; idx < methodCount; idx++) {
                NSString *methodName = NSStringFromSelector(method_getName(methods[idx])) ?: @"";
                NSString *lowerName = methodName.lowercaseString;
                for (NSString *keyword in keywords) {
                    if ([lowerName containsString:keyword]) {
                        [matchedMethods addObject:methodName];
                        break;
                    }
                }
                if (matchedMethods.count >= 80) {
                    break;
                }
            }
            [output appendFormat:@"%@ methods=%@\n",
                label ?: @"view",
                matchedMethods.count > 0 ? [matchedMethods componentsJoinedByString:@", "] : @"<none>"];
            free(methods);
        }
    } @catch (NSException *exception) {
        [output appendFormat:@"%@ debug_exception=%@\n", label ?: @"view", exception];
    }
}

// 执行搜索
