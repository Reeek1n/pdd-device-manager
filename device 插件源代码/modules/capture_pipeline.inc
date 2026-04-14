static id json_findNestedValueForKey(id jsonObject, NSString *searchKey, NSInteger depth) {
    if (!jsonObject || searchKey.length == 0 || depth <= 0) {
        return nil;
    }

    @try {
        if ([jsonObject isKindOfClass:[NSDictionary class]]) {
            NSDictionary *jsonDictionary = (NSDictionary *)jsonObject;
            id directValue = jsonDictionary[searchKey];
            if (directValue) {
                return directValue;
            }
            for (id childValue in jsonDictionary.allValues) {
                id nestedValue = json_findNestedValueForKey(childValue, searchKey, depth - 1);
                if (nestedValue) {
                    return nestedValue;
                }
            }
        } else if ([jsonObject isKindOfClass:[NSArray class]]) {
            for (id arrayItem in (NSArray *)jsonObject) {
                id nestedValue = json_findNestedValueForKey(arrayItem, searchKey, depth - 1);
                if (nestedValue) {
                    return nestedValue;
                }
            }
        }
    } @catch (NSException *exception) {
    }

    return nil;
}

static BOOL capture_urlLooksLikeDetail(NSString *url) {
    if (url.length == 0) {
        return NO;
    }
    return ([url containsString:@"goods_detail"] ||
            [url containsString:@"goods/detail"] ||
            [url containsString:@"goodsDetail"] ||
            [url containsString:@"detail"]);
}

static BOOL capture_payloadLooksLikeSearchResultEvent(id jsonObject) {
    if (![jsonObject isKindOfClass:[NSDictionary class]]) {
        return NO;
    }

    @try {
        NSDictionary *payloadDictionary = (NSDictionary *)jsonObject;
        NSString *pageName = [payloadDictionary[@"page_name"] isKindOfClass:[NSString class]] ? payloadDictionary[@"page_name"] : @"";
        NSString *operationName = [payloadDictionary[@"op"] isKindOfClass:[NSString class]] ? payloadDictionary[@"op"] : @"";
        BOOL hasSearchContext = payloadDictionary[@"p_search"] != nil || payloadDictionary[@"query"] != nil || payloadDictionary[@"true_query"] != nil;
        BOOL hasStructuredDetailPayload = [payloadDictionary[@"goods"] isKindOfClass:[NSDictionary class]] ||
            [payloadDictionary[@"sku"] isKindOfClass:[NSArray class]] ||
            [payloadDictionary[@"price"] isKindOfClass:[NSDictionary class]];

        if ([pageName isEqualToString:@"search_result"] && !hasStructuredDetailPayload) {
            return YES;
        }
        if (hasSearchContext && ([operationName isEqualToString:@"impr"] || [operationName isEqualToString:@"click"]) && !hasStructuredDetailPayload) {
            return YES;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL capture_payloadLooksLikeTrackingEvent(id jsonObject) {
    if (![jsonObject isKindOfClass:[NSDictionary class]]) {
        return NO;
    }

    @try {
        NSDictionary *payloadDictionary = (NSDictionary *)jsonObject;
        NSString *pageName = [payloadDictionary[@"page_name"] isKindOfClass:[NSString class]] ? payloadDictionary[@"page_name"] : @"";
        NSString *operationName = [payloadDictionary[@"op"] isKindOfClass:[NSString class]] ? payloadDictionary[@"op"] : @"";
        BOOL hasEventType = (payloadDictionary[@"event_type"] != nil || payloadDictionary[@"type"] != nil);
        BOOL hasStructuredDetailPayload = [payloadDictionary[@"goods"] isKindOfClass:[NSDictionary class]] ||
            [payloadDictionary[@"sku"] isKindOfClass:[NSArray class]] ||
            [payloadDictionary[@"price"] isKindOfClass:[NSDictionary class]];

        if (!hasStructuredDetailPayload &&
            hasEventType &&
            ([operationName isEqualToString:@"impr"] || [operationName isEqualToString:@"click"])) {
            if ([pageName isEqualToString:@"goods_detail"] ||
                [pageName isEqualToString:@"search_result"] ||
                [pageName containsString:@"detail"]) {
                return YES;
            }
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static BOOL task_isWithinRecentDetailWindow(void) {
    if (taskDetailNavigationStartedAt <= 0) {
        return NO;
    }
    NSTimeInterval secondsSinceDetailNavigation = [[NSDate date] timeIntervalSince1970] - taskDetailNavigationStartedAt;
    return (secondsSinceDetailNavigation >= 0 && secondsSinceDetailNavigation <= 15.0);
}

static NSString *json_topLevelKeysText(id jsonObject) {
    if (![jsonObject isKindOfClass:[NSDictionary class]]) {
        return @"";
    }

    @try {
        NSArray *keys = [[(NSDictionary *)jsonObject allKeys] sortedArrayUsingSelector:@selector(compare:)];
        if (keys.count == 0) {
            return @"";
        }
        NSUInteger limit = MIN((NSUInteger)40, keys.count);
        return [[keys subarrayWithRange:NSMakeRange(0, limit)] componentsJoinedByString:@","];
    } @catch (NSException *exception) {
    }

    return @"";
}

static void debug_writeCandidatePayload(id jsonObject, NSString *source, NSString *url, NSString *goodsId) {
    if (!task_hasActiveTask() || !(taskNeedsBackNavigation || task_isWithinRecentDetailWindow())) {
        return;
    }
    if (taskCandidateDebugCount >= 20) {
        return;
    }

    @try {
        NSString *pageName = @"";
        NSString *operationName = @"";
        NSString *detailType = @"";
        NSString *goodsName = @"";
        NSString *nestedGoodsId = @"";
        NSString *mallPriceText = @"";
        BOOL hasGoods = NO;
        BOOL hasSku = NO;
        BOOL hasPriceDict = NO;

        if ([jsonObject isKindOfClass:[NSDictionary class]]) {
            NSDictionary *payloadDictionary = (NSDictionary *)jsonObject;
            pageName = [payloadDictionary[@"page_name"] isKindOfClass:[NSString class]] ? payloadDictionary[@"page_name"] : @"";
            operationName = [payloadDictionary[@"op"] isKindOfClass:[NSString class]] ? payloadDictionary[@"op"] : @"";
            detailType = [payloadDictionary[@"detail_type"] isKindOfClass:[NSString class]] ? payloadDictionary[@"detail_type"] : @"";
            hasGoods = [payloadDictionary[@"goods"] isKindOfClass:[NSDictionary class]];
            hasSku = [payloadDictionary[@"sku"] isKindOfClass:[NSArray class]];
            hasPriceDict = [payloadDictionary[@"price"] isKindOfClass:[NSDictionary class]];
        }
        id nestedGoodsIdValue = json_findNestedValueForKey(jsonObject, @"goods_id", 6);
        if (nestedGoodsIdValue) {
            nestedGoodsId = [NSString stringWithFormat:@"%@", nestedGoodsIdValue];
        }
        id goodsNameValue = json_findNestedValueForKey(jsonObject, @"goods_name", 6);
        if ([goodsNameValue isKindOfClass:[NSString class]]) {
            goodsName = (NSString *)goodsNameValue;
        }
        id mallPriceValue = json_findNestedValueForKey(jsonObject, @"min_on_sale_group_price", 6);
        if (mallPriceValue) {
            mallPriceText = [NSString stringWithFormat:@"%@", mallPriceValue];
        }

        if (pageName.length == 0 && operationName.length == 0 && detailType.length == 0 &&
            (goodsId.length == 0 || [goodsId hasPrefix:@"unknown"]) &&
            nestedGoodsId.length == 0 &&
            goodsName.length == 0 &&
            mallPriceText.length == 0 &&
            url.length == 0) {
            return;
        }

        NSString *rawPreviewText = json_stringFromObject(jsonObject);
        if (rawPreviewText.length > 4000) {
            rawPreviewText = [[rawPreviewText substringToIndex:4000] stringByAppendingString:@"\n...[truncated]"];
        }

        NSMutableString *debugOutput = [NSMutableString string];
        [debugOutput appendFormat:@"timestamp=%lld\n", (long long)[[NSDate date] timeIntervalSince1970]];
        [debugOutput appendFormat:@"task_id=%@\n", taskCurrentId ?: @""];
        [debugOutput appendFormat:@"keyword=%@\n", taskCurrentKeyword ?: @""];
        [debugOutput appendFormat:@"source=%@\n", source ?: @""];
        [debugOutput appendFormat:@"url=%@\n", url ?: @""];
        [debugOutput appendFormat:@"goods_id=%@\n", goodsId ?: @""];
        [debugOutput appendFormat:@"nested_goods_id=%@\n", nestedGoodsId];
        [debugOutput appendFormat:@"goods_name=%@\n", goodsName];
        [debugOutput appendFormat:@"mall_price=%@\n", mallPriceText];
        [debugOutput appendFormat:@"page_name=%@\n", pageName];
        [debugOutput appendFormat:@"op=%@\n", operationName];
        [debugOutput appendFormat:@"detail_type=%@\n", detailType];
        [debugOutput appendFormat:@"has_goods=%@\n", hasGoods ? @"true" : @"false"];
        [debugOutput appendFormat:@"has_sku=%@\n", hasSku ? @"true" : @"false"];
        [debugOutput appendFormat:@"has_price_dict=%@\n", hasPriceDict ? @"true" : @"false"];
        [debugOutput appendFormat:@"top_level_keys=%@\n", json_topLevelKeysText(jsonObject)];
        id rootVC = ui_currentRootViewController();
        id rootView = ui_currentRootView();
        [debugOutput appendFormat:@"root_vc=%@\n", rootVC ? NSStringFromClass([rootVC class]) : @"<nil>"];
        [debugOutput appendFormat:@"root_view=%@\n", rootView ? NSStringFromClass([rootView class]) : @"<nil>"];
        if (rootView) {
            [debugOutput appendString:@"\n[visible_view_hierarchy]\n"];
            debug_appendViewHierarchy(debugOutput, rootView, 0, 10);
        }
        if (rawPreviewText.length > 0) {
            [debugOutput appendFormat:@"\n[raw_preview]\n%@\n", rawPreviewText];
        }

        NSString *debugFileName = [NSString stringWithFormat:@"debug_%02ld_candidate_payload.txt", (long)++taskDebugSequence];
        NSString *debugFilePath = task_filePath(debugFileName);
        if (debugFilePath.length > 0) {
            [debugOutput writeToFile:debugFilePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            taskCandidateDebugCount++;
        }
    } @catch (NSException *exception) {
    }
}

static void debug_writeNetworkResponse(NSURLRequest *request, NSURLResponse *response, NSData *data) {
    if (!task_hasActiveTask()) {
        return;
    }
    if (taskNetworkDebugCount >= 20 || !data || data.length == 0) {
        return;
    }

    @try {
        NSString *requestURL = request.URL.absoluteString ?: @"";
        NSString *responseURL = @"";
        NSString *mimeType = @"";
        NSInteger statusCode = 0;

        if ([response respondsToSelector:NSSelectorFromString(@"URL")]) {
            NSURL *responseNSURL = ((NSURL * (*)(id, SEL))objc_msgSend)(response, NSSelectorFromString(@"URL"));
            responseURL = responseNSURL.absoluteString ?: @"";
        }
        if ([response respondsToSelector:NSSelectorFromString(@"MIMEType")]) {
            mimeType = ((NSString * (*)(id, SEL))objc_msgSend)(response, NSSelectorFromString(@"MIMEType")) ?: @"";
        }
        if ([response respondsToSelector:NSSelectorFromString(@"statusCode")]) {
            statusCode = ((NSInteger (*)(id, SEL))objc_msgSend)(response, NSSelectorFromString(@"statusCode"));
        }

        NSString *effectiveResponseURL = responseURL.length > 0 ? responseURL : requestURL;
        NSString *rawPreviewText = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (rawPreviewText.length == 0) {
            rawPreviewText = [NSString stringWithFormat:@"<non-utf8 length=%lu>", (unsigned long)data.length];
        }

        BOOL urlLooksRelevant = ([effectiveResponseURL containsString:@"goods"] ||
                                 [effectiveResponseURL containsString:@"detail"] ||
                                 [effectiveResponseURL containsString:@"product"]);
        BOOL bodyLooksRelevant = ([rawPreviewText containsString:@"goods_id"] ||
                                  [rawPreviewText containsString:@"page_name"] ||
                                  [rawPreviewText containsString:@"sku"] ||
                                  [rawPreviewText containsString:@"goods_detail"]);

        if (!urlLooksRelevant && !bodyLooksRelevant &&
            !(taskNeedsBackNavigation || task_isWithinRecentDetailWindow())) {
            return;
        }

        if (rawPreviewText.length > 5000) {
            rawPreviewText = [[rawPreviewText substringToIndex:5000] stringByAppendingString:@"\n...[truncated]"];
        }

        NSMutableString *debugOutput = [NSMutableString string];
        [debugOutput appendFormat:@"timestamp=%lld\n", (long long)[[NSDate date] timeIntervalSince1970]];
        [debugOutput appendFormat:@"task_id=%@\n", taskCurrentId ?: @""];
        [debugOutput appendFormat:@"keyword=%@\n", taskCurrentKeyword ?: @""];
        [debugOutput appendFormat:@"request_url=%@\n", requestURL];
        [debugOutput appendFormat:@"response_url=%@\n", responseURL];
        [debugOutput appendFormat:@"status_code=%ld\n", (long)statusCode];
        [debugOutput appendFormat:@"mime_type=%@\n", mimeType];
        [debugOutput appendFormat:@"body_length=%lu\n", (unsigned long)data.length];
        [debugOutput appendFormat:@"url_looks_relevant=%@\n", urlLooksRelevant ? @"true" : @"false"];
        [debugOutput appendFormat:@"body_looks_relevant=%@\n", bodyLooksRelevant ? @"true" : @"false"];
        [debugOutput appendFormat:@"\n[raw_preview]\n%@\n", rawPreviewText];

        NSString *debugFileName = [NSString stringWithFormat:@"debug_%02ld_network_response.txt", (long)++taskDebugSequence];
        NSString *debugFilePath = task_filePath(debugFileName);
        if (debugFilePath.length > 0) {
            [debugOutput writeToFile:debugFilePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            taskNetworkDebugCount++;
        }
    } @catch (NSException *exception) {
    }
}

static void debug_writeRequest(NSString *source, NSURLRequest *request, NSData *bodyData) {
    if (!task_hasActiveTask()) {
        return;
    }
    if (taskRequestDebugCount >= 20 || !request) {
        return;
    }

    @try {
        NSString *requestURL = request.URL.absoluteString ?: @"";
        NSString *httpMethod = [request respondsToSelector:NSSelectorFromString(@"HTTPMethod")] ?
            ((NSString * (*)(id, SEL))objc_msgSend)(request, NSSelectorFromString(@"HTTPMethod")) :
            @"";
        NSString *rawText = [[NSString alloc] initWithData:bodyData encoding:NSUTF8StringEncoding];
        if (rawText.length == 0 && bodyData.length > 0) {
            rawText = [NSString stringWithFormat:@"<non-utf8 length=%lu>", (unsigned long)bodyData.length];
        }

        BOOL urlLooksRelevant = ([requestURL containsString:@"goods"] ||
                                 [requestURL containsString:@"detail"] ||
                                 [requestURL containsString:@"product"]);
        BOOL bodyLooksRelevant = ([rawText containsString:@"goods_id"] ||
                                  [rawText containsString:@"page_name"] ||
                                  [rawText containsString:@"sku"] ||
                                  [rawText containsString:@"goods_detail"] ||
                                  [rawText containsString:@"detail_type"]);
        if (!urlLooksRelevant && !bodyLooksRelevant &&
            !(taskNeedsBackNavigation || task_isWithinRecentDetailWindow())) {
            return;
        }

        if (rawText.length > 4000) {
            rawText = [[rawText substringToIndex:4000] stringByAppendingString:@"\n...[truncated]"];
        }

        NSMutableString *output = [NSMutableString string];
        [output appendFormat:@"timestamp=%lld\n", (long long)[[NSDate date] timeIntervalSince1970]];
        [output appendFormat:@"task_id=%@\n", taskCurrentId ?: @""];
        [output appendFormat:@"keyword=%@\n", taskCurrentKeyword ?: @""];
        [output appendFormat:@"source=%@\n", source ?: @""];
        [output appendFormat:@"request_url=%@\n", requestURL];
        [output appendFormat:@"method=%@\n", httpMethod ?: @""];
        [output appendFormat:@"body_length=%lu\n", (unsigned long)bodyData.length];
        [output appendFormat:@"url_looks_relevant=%@\n", urlLooksRelevant ? @"true" : @"false"];
        [output appendFormat:@"body_looks_relevant=%@\n", bodyLooksRelevant ? @"true" : @"false"];
        if (rawText.length > 0) {
            [output appendFormat:@"\n[body_preview]\n%@\n", rawText];
        }

        NSString *fileName = [NSString stringWithFormat:@"debug_%02ld_request.txt", (long)++taskDebugSequence];
        NSString *filePath = task_filePath(fileName);
        if (filePath.length > 0) {
            [output writeToFile:filePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            taskRequestDebugCount++;
        }
    } @catch (NSException *exception) {
    }
}

static void debug_writeTaskLifecycle(NSString *event, NSURLSessionTask *task, NSURLRequest *request, NSURLResponse *response, NSData *bodyData, id session, id delegateObject) {
    if (!task_hasActiveTask()) {
        return;
    }
    if (taskLifecycleDebugCount >= 40) {
        return;
    }

    @try {
        NSURLRequest *effectiveRequest = request;
        if (!effectiveRequest && task && [task respondsToSelector:NSSelectorFromString(@"currentRequest")]) {
            effectiveRequest = ((NSURLRequest * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"currentRequest"));
        }
        if (!effectiveRequest && task && [task respondsToSelector:NSSelectorFromString(@"originalRequest")]) {
            effectiveRequest = ((NSURLRequest * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"originalRequest"));
        }

        NSURLResponse *effectiveResponse = response;
        if (!effectiveResponse && task && [task respondsToSelector:NSSelectorFromString(@"response")]) {
            effectiveResponse = ((NSURLResponse * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"response"));
        }

        id effectiveSession = session;
        if (!effectiveSession && task && [task respondsToSelector:NSSelectorFromString(@"session")]) {
            effectiveSession = ((id (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"session"));
        }

        id effectiveDelegateObject = delegateObject;
        if (!effectiveDelegateObject && effectiveSession) {
            @try {
                effectiveDelegateObject = [effectiveSession valueForKey:@"delegate"];
            } @catch (NSException *exception) {
            }
        }

        NSString *requestURL = effectiveRequest.URL.absoluteString ?: @"";
        NSString *responseURL = @"";
        NSString *mimeType = @"";
        NSInteger statusCode = 0;
        if ([effectiveResponse respondsToSelector:NSSelectorFromString(@"URL")]) {
            NSURL *responseNSURL = ((NSURL * (*)(id, SEL))objc_msgSend)(effectiveResponse, NSSelectorFromString(@"URL"));
            responseURL = responseNSURL.absoluteString ?: @"";
        }
        if ([effectiveResponse respondsToSelector:NSSelectorFromString(@"MIMEType")]) {
            mimeType = ((NSString * (*)(id, SEL))objc_msgSend)(effectiveResponse, NSSelectorFromString(@"MIMEType")) ?: @"";
        }
        if ([effectiveResponse respondsToSelector:NSSelectorFromString(@"statusCode")]) {
            statusCode = ((NSInteger (*)(id, SEL))objc_msgSend)(effectiveResponse, NSSelectorFromString(@"statusCode"));
        }

        NSString *rawText = [[NSString alloc] initWithData:bodyData encoding:NSUTF8StringEncoding];
        if (rawText.length == 0 && bodyData.length > 0) {
            rawText = [NSString stringWithFormat:@"<non-utf8 length=%lu>", (unsigned long)bodyData.length];
        }

        BOOL urlLooksRelevant = ([requestURL containsString:@"goods"] ||
                                 [requestURL containsString:@"detail"] ||
                                 [requestURL containsString:@"product"] ||
                                 [responseURL containsString:@"goods"] ||
                                 [responseURL containsString:@"detail"] ||
                                 [responseURL containsString:@"product"]);
        BOOL bodyLooksRelevant = ([rawText containsString:@"goods_id"] ||
                                  [rawText containsString:@"page_name"] ||
                                  [rawText containsString:@"sku"] ||
                                  [rawText containsString:@"goods_detail"]);
        BOOL forceCapture = ([event isEqualToString:@"set_http_body"]);
        if (!forceCapture && !urlLooksRelevant && !bodyLooksRelevant &&
            !(taskNeedsBackNavigation || task_isWithinRecentDetailWindow())) {
            return;
        }

        if (rawText.length > 3000) {
            rawText = [[rawText substringToIndex:3000] stringByAppendingString:@"\n...[truncated]"];
        }

        NSString *taskClassName = task ? NSStringFromClass([task class]) : @"";
        NSString *sessionClassName = effectiveSession ? NSStringFromClass([effectiveSession class]) : @"";
        NSString *delegateClassName = effectiveDelegateObject ? NSStringFromClass([effectiveDelegateObject class]) : @"";
        NSString *originalRequestURL = @"";
        if (task && [task respondsToSelector:NSSelectorFromString(@"originalRequest")]) {
            NSURLRequest *originalRequest = ((NSURLRequest * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"originalRequest"));
            originalRequestURL = originalRequest.URL.absoluteString ?: @"";
        }

        NSMutableString *output = [NSMutableString string];
        [output appendFormat:@"timestamp=%lld\n", (long long)[[NSDate date] timeIntervalSince1970]];
        [output appendFormat:@"task_id=%@\n", taskCurrentId ?: @""];
        [output appendFormat:@"keyword=%@\n", taskCurrentKeyword ?: @""];
        [output appendFormat:@"event=%@\n", event ?: @""];
        [output appendFormat:@"task_class=%@\n", taskClassName];
        [output appendFormat:@"session_class=%@\n", sessionClassName];
        [output appendFormat:@"delegate_class=%@\n", delegateClassName];
        [output appendFormat:@"original_request_url=%@\n", originalRequestURL];
        [output appendFormat:@"request_url=%@\n", requestURL];
        [output appendFormat:@"response_url=%@\n", responseURL];
        [output appendFormat:@"status_code=%ld\n", (long)statusCode];
        [output appendFormat:@"mime_type=%@\n", mimeType];
        [output appendFormat:@"body_length=%lu\n", (unsigned long)bodyData.length];
        [output appendFormat:@"url_looks_relevant=%@\n", urlLooksRelevant ? @"true" : @"false"];
        [output appendFormat:@"body_looks_relevant=%@\n", bodyLooksRelevant ? @"true" : @"false"];
        if (rawText.length > 0) {
            [output appendFormat:@"\n[body_preview]\n%@\n", rawText];
        }

        NSString *fileName = [NSString stringWithFormat:@"debug_%02ld_task_lifecycle.txt", (long)++taskDebugSequence];
        NSString *filePath = task_filePath(fileName);
        if (filePath.length > 0) {
            [output writeToFile:filePath atomically:YES encoding:NSUTF8StringEncoding error:nil];
            taskLifecycleDebugCount++;
        }
    } @catch (NSException *exception) {
    }
}

static BOOL capture_payloadLooksLikeDetailPageSignal(id jsonObject, NSString *url) {
    if (capture_urlLooksLikeDetail(url)) {
        return YES;
    }
    if (![jsonObject isKindOfClass:[NSDictionary class]]) {
        return NO;
    }

    @try {
        NSDictionary *dict = (NSDictionary *)jsonObject;
        NSString *pageName = [dict[@"page_name"] isKindOfClass:[NSString class]] ? dict[@"page_name"] : @"";
        NSString *detailType = [dict[@"detail_type"] isKindOfClass:[NSString class]] ? dict[@"detail_type"] : @"";
        if ([pageName isEqualToString:@"goods_detail"] ||
            [pageName containsString:@"detail"] ||
            [detailType containsString:@"detail"]) {
            return YES;
        }
    } @catch (NSException *exception) {
    }

    return NO;
}

static NSString *json_stringFromObject(id jsonObject) {
    if (!jsonObject) {
        return nil;
    }
    @try {
        NSError *error = nil;
        NSData *jsonData = [NSJSONSerialization dataWithJSONObject:jsonObject options:0 error:&error];
        if (!jsonData || error) {
            return nil;
        }
        return [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
    } @catch (NSException *exception) {
    }
    return nil;
}

static NSString *json_stringFromValue(id value) {
    if ([value isKindOfClass:[NSString class]]) {
        return (NSString *)value;
    }
    if ([value respondsToSelector:@selector(stringValue)]) {
        return [value stringValue];
    }
    return nil;
}

static NSArray<NSString *> * __attribute__((unused)) json_topStringKeysFromDictionary(NSDictionary *dict, NSUInteger limit) {
    if (![dict isKindOfClass:[NSDictionary class]] || limit == 0) {
        return @[];
    }

    NSMutableArray<NSString *> *keys = [NSMutableArray array];
    for (id key in dict.allKeys) {
        if (![key isKindOfClass:[NSString class]]) {
            continue;
        }
        [keys addObject:(NSString *)key];
        if (keys.count >= limit) {
            break;
        }
    }
    return keys;
}

static id capture_decodedJSONObjectFromInspectableObject(id object) {
    NSData *jsonData = nil;
    if ([object isKindOfClass:[NSData class]]) {
        jsonData = (NSData *)object;
    } else if ([object isKindOfClass:[NSString class]]) {
        NSString *trimmed = [[(NSString *)object stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] copy];
        if (trimmed.length == 0) {
            return nil;
        }
        unichar firstCharacter = [trimmed characterAtIndex:0];
        if (firstCharacter != '{' && firstCharacter != '[') {
            return nil;
        }
        jsonData = [trimmed dataUsingEncoding:NSUTF8StringEncoding];
    } else {
        return nil;
    }

    if (jsonData.length == 0) {
        return nil;
    }

    NSError *error = nil;
    id jsonObject = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&error];
    if (error || !jsonObject) {
        return nil;
    }

    if ([jsonObject isKindOfClass:[NSDictionary class]] || [jsonObject isKindOfClass:[NSArray class]]) {
        return jsonObject;
    }
    return nil;
}

static NSDictionary *capture_detailPayloadContainerFromObject(id object, NSInteger depth) {
    if (depth < 0 || !object) {
        return nil;
    }

    id decodedObject = capture_decodedJSONObjectFromInspectableObject(object);
    if (decodedObject && decodedObject != object) {
        NSDictionary *decodedPayload = capture_detailPayloadContainerFromObject(decodedObject, depth);
        if (decodedPayload) {
            return decodedPayload;
        }
    }

    if ([object isKindOfClass:[NSDictionary class]]) {
        NSDictionary *dict = (NSDictionary *)object;
        if (capture_detailPayloadScore(dict, nil) >= 220) {
            return dict;
        }

        NSMutableOrderedSet<NSString *> *keys = [NSMutableOrderedSet orderedSetWithArray:@[
            @"responseData", @"data", @"result", @"content", @"detail", @"detailData", @"goodsData", @"payload", @"response", @"goods"
        ]];
        [keys addObjectsFromArray:dict.allKeys];

        NSUInteger inspected = 0;
        for (id key in keys) {
            if (![key isKindOfClass:[NSString class]]) {
                continue;
            }
            NSDictionary *nested = capture_detailPayloadContainerFromObject(dict[(NSString *)key], depth - 1);
            if (nested) {
                return nested;
            }
            if (++inspected >= 18) {
                break;
            }
        }
        return nil;
    }

    if ([object isKindOfClass:[NSArray class]]) {
        NSArray *array = (NSArray *)object;
        NSUInteger limit = MIN((NSUInteger)6, array.count);
        for (NSUInteger index = 0; index < limit; index++) {
            NSDictionary *nested = capture_detailPayloadContainerFromObject(array[index], depth - 1);
            if (nested) {
                return nested;
            }
        }
    }

    return nil;
}

static NSString *capture_goodsIdFromDetailPayload(NSDictionary *payload) {
    if (![payload isKindOfClass:[NSDictionary class]]) {
        return nil;
    }

    id goodsId = payload[@"goods_id"];
    if (!goodsId && [payload[@"goods"] isKindOfClass:[NSDictionary class]]) {
        NSDictionary *goods = (NSDictionary *)payload[@"goods"];
        goodsId = goods[@"goods_id"] ?: goods[@"goodsId"];
    }
    return json_stringFromValue(goodsId);
}

static NSString *capture_goodsNameFromDetailPayload(NSDictionary *payload) {
    if (![payload isKindOfClass:[NSDictionary class]]) {
        return nil;
    }

    id goodsName = payload[@"goods_name"];
    if (!goodsName && [payload[@"goods"] isKindOfClass:[NSDictionary class]]) {
        NSDictionary *goods = (NSDictionary *)payload[@"goods"];
        goodsName = goods[@"goods_name"] ?: goods[@"goodsName"];
    }
    return json_stringFromValue(goodsName);
}

static NSInteger capture_detailPayloadScore(NSDictionary *payload, NSString *urlString) {
    if (![payload isKindOfClass:[NSDictionary class]]) {
        return 0;
    }

    NSDictionary *goods = [payload[@"goods"] isKindOfClass:[NSDictionary class]] ? payload[@"goods"] : nil;
    id sku = payload[@"sku"];
    NSDictionary *price = [payload[@"price"] isKindOfClass:[NSDictionary class]] ? payload[@"price"] : nil;
    NSDictionary *ui = [payload[@"ui"] isKindOfClass:[NSDictionary class]] ? payload[@"ui"] : nil;
    NSDictionary *control = [payload[@"control"] isKindOfClass:[NSDictionary class]] ? payload[@"control"] : nil;
    id gallery = goods[@"gallery"];
    id goodsProperty = goods[@"goods_property"];
    id servicePromise = payload[@"service_promise"];
    id activityCollection = payload[@"activity_collection"];
    id promotion = payload[@"promotion"];
    id transmission = payload[@"transmission"];

    NSInteger value = 0;
    if (goods) value += 120;
    if ([sku isKindOfClass:[NSArray class]] || [sku isKindOfClass:[NSDictionary class]]) value += 90;
    if (price) value += 90;
    if (ui) value += 35;
    if (control) value += 25;
    if ([gallery isKindOfClass:[NSArray class]]) value += 20;
    if ([goodsProperty isKindOfClass:[NSArray class]]) value += 20;
    if ([servicePromise isKindOfClass:[NSDictionary class]] || [servicePromise isKindOfClass:[NSArray class]]) value += 20;
    if ([activityCollection isKindOfClass:[NSDictionary class]] || [activityCollection isKindOfClass:[NSArray class]]) value += 15;
    if ([promotion isKindOfClass:[NSDictionary class]] || [promotion isKindOfClass:[NSArray class]]) value += 10;
    if ([transmission isKindOfClass:[NSDictionary class]] || [transmission isKindOfClass:[NSArray class]]) value += 10;
    if ([payload[@"destination_url"] isKindOfClass:[NSString class]]) value += 15;
    if ([payload[@"pre_render_url"] isKindOfClass:[NSString class]]) value += 10;
    if (capture_goodsIdFromDetailPayload(payload).length > 0) value += 25;
    if (capture_goodsNameFromDetailPayload(payload).length > 0) value += 20;
    if (capture_urlLooksLikeDetail(urlString)) value += 20;

    if ([payload[@"dy_template"] isKindOfClass:[NSDictionary class]]) value -= 220;
    if ([payload[@"template_ext"] isKindOfClass:[NSDictionary class]]) value -= 180;
    if ([payload[@"goods_list"] isKindOfClass:[NSArray class]]) value -= 140;
    if ([payload[@"link_url"] isKindOfClass:[NSString class]]) value -= 40;
    if ([payload[@"module_sn"] isKindOfClass:[NSString class]] || [payload[@"template_sn"] isKindOfClass:[NSString class]]) value -= 120;

    return value;
}

static NSString *capture_normalizedJSONString(NSString *rawText, id jsonObject) {
    NSString *trimmed = [[rawText ?: @"" stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] copy];
    if (trimmed.length > 0 && ([trimmed hasPrefix:@"{"] || [trimmed hasPrefix:@"["])) {
        return trimmed;
    }
    if (!jsonObject) {
        return nil;
    }
    NSError *error = nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:jsonObject options:0 error:&error];
    if (!data || error) {
        return nil;
    }
    return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
}

static NSString *capture_normalizedTextForMatching(NSString *text) {
    if (text.length == 0) {
        return @"";
    }

    NSMutableCharacterSet *ignoredCharacters = [[NSCharacterSet whitespaceAndNewlineCharacterSet] mutableCopy];
    [ignoredCharacters formUnionWithCharacterSet:[NSCharacterSet punctuationCharacterSet]];
    [ignoredCharacters formUnionWithCharacterSet:[NSCharacterSet symbolCharacterSet]];

    NSMutableString *normalized = [NSMutableString stringWithCapacity:text.length];
    for (NSUInteger index = 0; index < text.length; index++) {
        unichar character = [text characterAtIndex:index];
        if ([ignoredCharacters characterIsMember:character]) {
            continue;
        }
        [normalized appendFormat:@"%C", character];
    }
    return normalized;
}

static NSString *ui_visibleTextSampleFromView(id rootView) {
    if (!rootView) {
        return @"";
    }

    NSMutableArray *queue = [NSMutableArray arrayWithObject:rootView];
    NSMutableOrderedSet<NSString *> *texts = [NSMutableOrderedSet orderedSet];
    NSUInteger index = 0;
    while (index < queue.count && index < 360 && texts.count < 60) {
        id candidate = queue[index++];
        if (!candidate || (candidate != rootView && !ui_viewIsVisible(candidate))) {
            continue;
        }

        NSString *metadata = ui_viewMetadataText(candidate);
        if (metadata.length > 0) {
            [texts addObject:metadata];
        }

        NSArray *subviews = [candidate respondsToSelector:@selector(subviews)] ?
            ((NSArray * (*)(id, SEL))objc_msgSend)(candidate, @selector(subviews)) :
            nil;
        if (subviews.count > 0) {
            [queue addObjectsFromArray:subviews];
        }
    }
    return [[texts array] componentsJoinedByString:@" | "];
}

static BOOL capture_candidateMatchesVisibleText(NSDictionary *candidate, NSString *visibleText) {
    NSString *goodsName = capture_normalizedTextForMatching([candidate[@"goodsName"] isKindOfClass:[NSString class]] ? candidate[@"goodsName"] : @"");
    NSString *normalizedVisibleText = capture_normalizedTextForMatching(visibleText ?: @"");
    if (goodsName.length < 4 || normalizedVisibleText.length < 4) {
        return NO;
    }

    NSArray<NSNumber *> *prefixLengths = @[@12, @10, @8, @6, @4];
    for (NSNumber *lengthValue in prefixLengths) {
        NSUInteger length = MIN((NSUInteger)lengthValue.unsignedIntegerValue, goodsName.length);
        if (length < 4) {
            continue;
        }
        NSString *needle = [goodsName substringToIndex:length];
        if ([normalizedVisibleText containsString:needle]) {
            return YES;
        }
    }
    return NO;
}

static BOOL task_normalizedTextContainsNeedlePrefix(NSString *normalizedHaystack, NSString *needle) {
    NSString *normalizedNeedle = capture_normalizedTextForMatching(needle ?: @"");
    if (normalizedHaystack.length < 4 || normalizedNeedle.length < 4) {
        return NO;
    }

    NSArray<NSNumber *> *prefixLengths = @[@16, @12, @10, @8, @6, @4];
    for (NSNumber *lengthValue in prefixLengths) {
        NSUInteger length = MIN((NSUInteger)lengthValue.unsignedIntegerValue, normalizedNeedle.length);
        if (length < 4) {
            continue;
        }
        NSString *prefix = [normalizedNeedle substringToIndex:length];
        if ([normalizedHaystack containsString:prefix]) {
            return YES;
        }
    }
    return NO;
}

static NSInteger task_detailEntryBudget(void) {
    return MAX(taskTargetCount * 3, 30);
}

static BOOL task_shouldSkipUnresolvedSearchCell(id cell, NSString **reasonOut) {
    NSString *visibleText = ui_visibleTextSampleFromView(cell) ?: @"";
    if (visibleText.length == 0) {
        return NO;
    }

    NSArray<NSString *> *markers = @[@"广告", @"更多同款商品", @"同款商品", @"更多相似商品"];
    for (NSString *marker in markers) {
        if ([visibleText containsString:marker]) {
            if (reasonOut) {
                *reasonOut = marker;
            }
            return YES;
        }
    }
    return NO;
}

static id task_sharedStateLock(void) {
    static NSObject *lock = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        lock = [NSObject new];
    });
    return lock;
}

static NSDictionary *task_searchPayloadSnapshot(void) {
    @synchronized(task_sharedStateLock()) {
        if (![taskSearchPayloadByGoodsId isKindOfClass:[NSMutableDictionary class]] || taskSearchPayloadByGoodsId.count == 0) {
            return nil;
        }
        return [taskSearchPayloadByGoodsId copy];
    }
}

static NSSet *task_seenGoodsIdsSnapshot(void) {
    @synchronized(task_sharedStateLock()) {
        if (![taskSeenGoodsIds isKindOfClass:[NSMutableSet class]] || taskSeenGoodsIds.count == 0) {
            return nil;
        }
        return [taskSeenGoodsIds copy];
    }
}

static void capture_pruneRecentCandidatesLocked(void) {
    if (![taskRecentCandidates isKindOfClass:[NSMutableArray class]]) {
        return;
    }

    NSTimeInterval now = [[NSDate date] timeIntervalSince1970];
    NSIndexSet *staleIndexes = [taskRecentCandidates indexesOfObjectsPassingTest:^BOOL(NSDictionary *obj, NSUInteger idx, BOOL *stop) {
        return now - [obj[@"capturedAt"] doubleValue] > kTaskCandidateTTLSeconds;
    }];
    if (staleIndexes.count > 0) {
        [taskRecentCandidates removeObjectsAtIndexes:staleIndexes];
    }
    [taskRecentCandidates sortUsingComparator:^NSComparisonResult(NSDictionary *left, NSDictionary *right) {
        double leftCapturedAt = [left[@"capturedAt"] doubleValue];
        double rightCapturedAt = [right[@"capturedAt"] doubleValue];
        if (leftCapturedAt > rightCapturedAt) {
            return NSOrderedAscending;
        }
        if (leftCapturedAt < rightCapturedAt) {
            return NSOrderedDescending;
        }
        return NSOrderedSame;
    }];
    while (taskRecentCandidates.count > kTaskCandidateLimit) {
        [taskRecentCandidates removeLastObject];
    }
}

static void capture_storeRecentCandidate(NSDictionary *candidate) {
    if (!task_hasActiveTask() || ![candidate isKindOfClass:[NSDictionary class]]) {
        return;
    }
    if (!(taskNeedsBackNavigation || task_isWithinRecentDetailWindow())) {
        return;
    }

    NSString *goodsId = [candidate[@"goodsId"] isKindOfClass:[NSString class]] ? candidate[@"goodsId"] : @"";
    if (goodsId.length == 0) {
        return;
    }

    if (![taskRecentCandidates isKindOfClass:[NSMutableArray class]]) {
        taskRecentCandidates = [NSMutableArray array];
    }

    @synchronized (taskRecentCandidates) {
        NSIndexSet *existingIndexes = [taskRecentCandidates indexesOfObjectsPassingTest:^BOOL(NSDictionary *obj, NSUInteger idx, BOOL *stop) {
            return [obj[@"goodsId"] isEqualToString:goodsId];
        }];
        if (existingIndexes.count > 0) {
            [taskRecentCandidates removeObjectsAtIndexes:existingIndexes];
        }
        [taskRecentCandidates insertObject:[candidate copy] atIndex:0];
        capture_pruneRecentCandidatesLocked();
    }
}

static NSDictionary *capture_bestRecentCandidateForVisibleText(NSString *visibleText) {
    if (!task_hasActiveTask() || ![taskRecentCandidates isKindOfClass:[NSMutableArray class]]) {
        return nil;
    }

    NSArray *candidates = nil;
    @synchronized (taskRecentCandidates) {
        capture_pruneRecentCandidatesLocked();
        candidates = [taskRecentCandidates copy];
    }

    NSTimeInterval now = [[NSDate date] timeIntervalSince1970];
    NSDictionary *bestCandidate = nil;
    double bestScore = -DBL_MAX;
    for (NSDictionary *candidate in candidates) {
        double age = now - [candidate[@"capturedAt"] doubleValue];
        if (age > kTaskCandidateTTLSeconds) {
            continue;
        }
        if (!capture_candidateMatchesVisibleText(candidate, visibleText)) {
            continue;
        }

        double dynamicScore = [candidate[@"score"] doubleValue] - age * 1.6;
        dynamicScore += 120.0;
        if ([candidate[@"goodsId"] isEqualToString:taskLastGoodsId ?: @""]) {
            dynamicScore -= 2.0;
        }
        if (dynamicScore > bestScore) {
            bestScore = dynamicScore;
            bestCandidate = candidate;
        }
    }
    return bestCandidate;
}

static BOOL task_finalizeCurrentDetailAttemptIfNeeded(void) {
    if (!task_hasActiveTask() || !taskNeedsBackNavigation || taskDetailAttemptCounted) {
        return NO;
    }

    taskAttemptedCount++;
    taskDetailAttemptCounted = YES;
    task_writeCurrentTaskStatus();
    return YES;
}

static void ui_showTaskCaptureToast(NSString *goodsName) {
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            Class UIApplicationClass = NSClassFromString(@"UIApplication");
            if (!UIApplicationClass) {
                return;
            }

            id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication"));
            if (!app) {
                return;
            }

            id targetWindow = nil;
            if (@available(iOS 13.0, *)) {
                NSSet *scenes = [app valueForKey:@"connectedScenes"];
                for (id scene in scenes) {
                    NSArray *windows = [scene respondsToSelector:NSSelectorFromString(@"windows")] ?
                        ((NSArray * (*)(id, SEL))objc_msgSend)(scene, NSSelectorFromString(@"windows")) :
                        nil;
                    for (id window in windows) {
                        if ([window respondsToSelector:NSSelectorFromString(@"isKeyWindow")] &&
                            ((BOOL (*)(id, SEL))objc_msgSend)(window, NSSelectorFromString(@"isKeyWindow"))) {
                            targetWindow = window;
                            break;
                        }
                    }
                    if (targetWindow) {
                        break;
                    }
                }
            }

            if (!targetWindow) {
                targetWindow = [app valueForKey:@"keyWindow"];
            }
            if (!targetWindow) {
                return;
            }

            if (taskCaptureToastView && [taskCaptureToastView respondsToSelector:@selector(removeFromSuperview)]) {
                ((void (*)(id, SEL))objc_msgSend)(taskCaptureToastView, @selector(removeFromSuperview));
                taskCaptureToastView = nil;
            }

            NSString *title = [NSString stringWithFormat:@"已采集 %ld/%ld", (long)taskSavedCount, (long)taskTargetCount];
            NSString *safeGoodsName = goodsName ?: @"";
            if (safeGoodsName.length > 24) {
                safeGoodsName = [[safeGoodsName substringToIndex:24] stringByAppendingString:@"..."];
            }
            NSString *message = safeGoodsName.length > 0 ? [NSString stringWithFormat:@"%@\n%@", title, safeGoodsName] : title;

            UILabel *label = [[UILabel alloc] initWithFrame:CGRectZero];
            label.numberOfLines = 2;
            label.textAlignment = NSTextAlignmentCenter;
            label.textColor = [UIColor whiteColor];
            label.backgroundColor = [[UIColor colorWithRed:0.08 green:0.10 blue:0.12 alpha:1.0] colorWithAlphaComponent:0.86];
            label.font = [UIFont boldSystemFontOfSize:14.0];
            label.layer.cornerRadius = 12.0;
            label.layer.masksToBounds = YES;
            label.text = message;

            CGFloat maxWidth = 260.0;
            CGSize limit = CGSizeMake(maxWidth - 24.0, CGFLOAT_MAX);
            CGRect textRect = [label.text boundingRectWithSize:limit
                                                       options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingUsesFontLeading
                                                    attributes:@{NSFontAttributeName: label.font}
                                                       context:nil];
            CGFloat width = MIN(maxWidth, MAX(160.0, ceil(textRect.size.width) + 24.0));
            CGFloat height = MAX(48.0, ceil(textRect.size.height) + 18.0);

            CGRect windowBounds = [targetWindow respondsToSelector:@selector(bounds)] ?
                ((CGRect (*)(id, SEL))objc_msgSend)(targetWindow, @selector(bounds)) :
                CGRectMake(0, 0, 375, 667);
            CGFloat topInset = 48.0;
            if (@available(iOS 11.0, *)) {
                UIEdgeInsets safeInsets = [targetWindow respondsToSelector:@selector(safeAreaInsets)] ?
                    ((UIEdgeInsets (*)(id, SEL))objc_msgSend)(targetWindow, @selector(safeAreaInsets)) :
                    UIEdgeInsetsZero;
                topInset = MAX(48.0, safeInsets.top + 16.0);
            }

            label.frame = CGRectMake((CGRectGetWidth(windowBounds) - width) / 2.0, topInset, width, height);
            label.alpha = 0.0;
            ((void (*)(id, SEL, id))objc_msgSend)(targetWindow, @selector(addSubview:), label);
            taskCaptureToastView = label;

            [UIView animateWithDuration:0.18 animations:^{
                label.alpha = 1.0;
            }];

            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1200 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
                @try {
                    if (taskCaptureToastView != label) {
                        return;
                    }
                    [UIView animateWithDuration:0.18 animations:^{
                        label.alpha = 0.0;
                    } completion:^(BOOL finished) {
                        [label removeFromSuperview];
                        if (taskCaptureToastView == label) {
                            taskCaptureToastView = nil;
                        }
                    }];
                } @catch (NSException *exception) {
                }
            });
        } @catch (NSException *exception) {
        }
    });
}

static id runtime_safeValueForObject(id object, NSString *key) {
    if (!object || key.length == 0) {
        return nil;
    }

    NSString *normalizedKey = key;
    while ([normalizedKey hasPrefix:@"_"]) {
        normalizedKey = [normalizedKey substringFromIndex:1];
    }

    NSArray<NSString *> *candidateKeys = (normalizedKey.length > 0 && ![normalizedKey isEqualToString:key]) ?
        @[key, normalizedKey] :
        @[key];

    for (NSString *candidateKey in candidateKeys) {
        SEL selector = NSSelectorFromString(candidateKey);
        if (![object respondsToSelector:selector]) {
            continue;
        }
        @try {
            NSMethodSignature *signature = [object methodSignatureForSelector:selector];
            if (signature && signature.numberOfArguments == 2) {
                const char *returnType = signature.methodReturnType;
                if (returnType && (returnType[0] == '@' || returnType[0] == '#')) {
                    NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
                    invocation.target = object;
                    invocation.selector = selector;
                    [invocation invoke];
                    __unsafe_unretained id result = nil;
                    [invocation getReturnValue:&result];
                    if (result) {
                        return result;
                    }
                }
            }
        } @catch (NSException *exception) {
        }
    }

    @try {
        return [object valueForKey:key];
    } @catch (NSException *exception) {
    }

    if (![normalizedKey isEqualToString:key]) {
        @try {
            return [object valueForKey:normalizedKey];
        } @catch (NSException *exception) {
        }
    }

    return nil;
}

static BOOL runtime_probeKeyLooksInteresting(NSString *key) {
    if (key.length == 0) {
        return NO;
    }

    NSArray<NSString *> *fragments = @[
        @"detail", @"Detail",
        @"goods", @"Goods",
        @"data", @"Data",
        @"model", @"Model",
        @"origin", @"Origin",
        @"entry", @"Entry",
        @"context", @"Context",
        @"state", @"State",
        @"result", @"Result",
        @"content", @"Content",
        @"payload", @"Payload"
    ];
    for (NSString *fragment in fragments) {
        if ([key containsString:fragment]) {
            return YES;
        }
    }
    return NO;
}

static NSArray<NSString *> *runtime_probeMemberKeysForObject(id object) {
    if (!object) {
        return @[];
    }

    NSMutableOrderedSet<NSString *> *keys = [NSMutableOrderedSet orderedSet];

    unsigned int ivarCount = 0;
    Ivar *ivars = class_copyIvarList([object class], &ivarCount);
    for (unsigned int index = 0; index < ivarCount; index++) {
        Ivar ivar = ivars[index];
        const char *encoding = ivar_getTypeEncoding(ivar);
        if (!encoding || encoding[0] != '@') {
            continue;
        }
        NSString *name = [NSString stringWithUTF8String:ivar_getName(ivar)] ?: @"";
        if (runtime_probeKeyLooksInteresting(name)) {
            [keys addObject:name];
        }
        if (keys.count >= 24) {
            break;
        }
    }
    if (ivars) {
        free(ivars);
    }

    if (keys.count < 24) {
        unsigned int propertyCount = 0;
        objc_property_t *properties = class_copyPropertyList([object class], &propertyCount);
        for (unsigned int index = 0; index < propertyCount; index++) {
            objc_property_t property = properties[index];
            const char *attrs = property_getAttributes(property);
            if (!attrs || attrs[0] != 'T' || attrs[1] != '@') {
                continue;
            }
            NSString *name = [NSString stringWithUTF8String:property_getName(property)] ?: @"";
            if (runtime_probeKeyLooksInteresting(name)) {
                [keys addObject:name];
            }
            if (keys.count >= 24) {
                break;
            }
        }
        if (properties) {
            free(properties);
        }
    }

    return [keys array];
}

static BOOL runtime_shouldSkipInspectingObject(id object) {
    return !object ||
        [object isKindOfClass:[NSString class]] ||
        [object isKindOfClass:[NSNumber class]] ||
        [object isKindOfClass:[NSData class]] ||
        [object isKindOfClass:[NSDate class]] ||
        [object isKindOfClass:[NSURL class]] ||
        [object isKindOfClass:[NSValue class]] ||
        [object isKindOfClass:[UIColor class]] ||
        [object isKindOfClass:[UIImage class]];
}

static void runtime_appendProbeChildren(id object, NSString *path, NSMutableArray *roots) {
    if (!object || !roots) {
        return;
    }

    NSArray<NSString *> *keys = @[
        @"viewModel", @"_viewModel",
        @"detailModel", @"_detailModel",
        @"preloadDetailModel", @"_preloadDetailModel",
        @"goodsModel", @"_goodsModel",
        @"model", @"_model",
        @"data", @"_data",
        @"responseData", @"_responseData",
        @"uiModel", @"_uiModel",
        @"reviewData", @"_reviewData",
        @"propertyModel", @"_propertyModel",
        @"originData", @"_originData",
        @"contentView", @"_contentView",
        @"bottomRecViewModel", @"_bottomRecViewModel",
        @"context", @"_context",
        @"state", @"_state"
    ];

    NSMutableSet<NSString *> *seenPaths = [NSMutableSet set];
    for (NSString *key in keys) {
        id value = runtime_safeValueForObject(object, key);
        if (!value || runtime_shouldSkipInspectingObject(value)) {
            continue;
        }
        NSString *childPath = [NSString stringWithFormat:@"%@.%@", path ?: NSStringFromClass([object class]), key];
        if ([seenPaths containsObject:childPath]) {
            continue;
        }
        [seenPaths addObject:childPath];
        [roots addObject:@{@"object": value, @"path": childPath}];
    }
}

static NSDictionary *runtime_makeCandidateFromObject(id object, NSString *path) {
    NSDictionary *detailPayload = capture_detailPayloadContainerFromObject(object, 4);
    if (!detailPayload) {
        return nil;
    }

    NSString *goodsId = capture_goodsIdFromDetailPayload(detailPayload);
    if (goodsId.length == 0) {
        return nil;
    }

    NSString *jsonText = capture_normalizedJSONString(nil, detailPayload);
    if (jsonText.length == 0) {
        return nil;
    }

    NSString *goodsName = capture_goodsNameFromDetailPayload(detailPayload);
    NSInteger detailScore = MAX(260, capture_detailPayloadScore(detailPayload, nil) + 40);
    return @{
        @"goodsId": goodsId,
        @"goodsName": goodsName ?: @"",
        @"rawText": jsonText,
        @"url": path ?: @"runtime://detail",
        @"source": @"RuntimeGraph",
        @"score": @(detailScore),
        @"capturedAt": @([[NSDate date] timeIntervalSince1970])
    };
}

static NSDictionary *runtime_candidateFromInspectableObject(id object, NSString *path, NSInteger depth, NSMutableSet *visited) {
    if (!object || depth < 0) {
        return nil;
    }

    NSString *visitKey = [NSString stringWithFormat:@"%p", object];
    if ([visited containsObject:visitKey]) {
        return nil;
    }
    [visited addObject:visitKey];

    id decodedObject = capture_decodedJSONObjectFromInspectableObject(object);
    if (decodedObject && decodedObject != object) {
        NSDictionary *decodedCandidate = runtime_candidateFromInspectableObject(decodedObject,
            [NSString stringWithFormat:@"%@.decodedJSON", path ?: NSStringFromClass([object class])],
            depth,
            visited);
        if (decodedCandidate) {
            return decodedCandidate;
        }
    }

    if ([object isKindOfClass:[NSDictionary class]] || [object isKindOfClass:[NSArray class]]) {
        NSDictionary *candidate = runtime_makeCandidateFromObject(object, path);
        if (candidate) {
            return candidate;
        }
    }

    if (depth == 0 || runtime_shouldSkipInspectingObject(object)) {
        return nil;
    }

    if ([object isKindOfClass:[NSDictionary class]]) {
        NSDictionary *dict = (NSDictionary *)object;
        NSArray *preferredKeys = @[@"goods", @"data", @"result", @"content", @"detail", @"detailData", @"viewModel", @"model"];
        NSMutableOrderedSet *keys = [NSMutableOrderedSet orderedSetWithArray:preferredKeys];
        [keys addObjectsFromArray:dict.allKeys];
        NSUInteger count = 0;
        for (id key in keys) {
            if (![key isKindOfClass:[NSString class]]) {
                continue;
            }
            NSDictionary *candidate = runtime_candidateFromInspectableObject(dict[(NSString *)key],
                [NSString stringWithFormat:@"%@.%@", path ?: @"dict", (NSString *)key],
                depth - 1,
                visited);
            if (candidate) {
                return candidate;
            }
            if (++count >= 18) {
                break;
            }
        }
        return nil;
    }

    if ([object isKindOfClass:[NSArray class]]) {
        NSArray *array = (NSArray *)object;
        NSUInteger limit = MIN((NSUInteger)12, array.count);
        for (NSUInteger index = 0; index < limit; index++) {
            NSDictionary *candidate = runtime_candidateFromInspectableObject(array[index],
                [NSString stringWithFormat:@"%@[%lu]", path ?: @"array", (unsigned long)index],
                depth - 1,
                visited);
            if (candidate) {
                return candidate;
            }
        }
        return nil;
    }

    NSArray<NSString *> *preferredKeys = @[
        @"viewModel", @"_viewModel",
        @"preloadDetailModel", @"_preloadDetailModel",
        @"goodsModel", @"_goodsModel",
        @"detailModel", @"_detailModel",
        @"model", @"_model",
        @"goods", @"_goods",
        @"data", @"_data",
        @"responseData", @"_responseData",
        @"uiModel", @"_uiModel",
        @"reviewData", @"_reviewData",
        @"propertyModel", @"_propertyModel",
        @"detailData", @"_detailData",
        @"goodsData", @"_goodsData",
        @"result", @"_result",
        @"content", @"_content",
        @"state", @"_state",
        @"props", @"_props",
        @"context", @"_context",
        @"entryData", @"_entryData",
        @"originData", @"_originData"
    ];
    for (NSString *key in preferredKeys) {
        id value = runtime_safeValueForObject(object, key);
        NSDictionary *candidate = runtime_candidateFromInspectableObject(value,
            [NSString stringWithFormat:@"%@.%@", path ?: NSStringFromClass([object class]), key],
            depth - 1,
            visited);
        if (candidate) {
            return candidate;
        }
    }

    NSArray<NSString *> *memberKeys = runtime_probeMemberKeysForObject(object);
    for (NSString *key in memberKeys) {
        if ([preferredKeys containsObject:key]) {
            continue;
        }
        id value = runtime_safeValueForObject(object, key);
        NSDictionary *candidate = runtime_candidateFromInspectableObject(value,
            [NSString stringWithFormat:@"%@.%@", path ?: NSStringFromClass([object class]), key],
            depth - 1,
            visited);
        if (candidate) {
            return candidate;
        }
    }

    return nil;
}

static BOOL capture_trySaveTaskCandidate(NSDictionary *candidate) {
    if (!task_hasActiveTask() || ![candidate isKindOfClass:[NSDictionary class]]) {
        return NO;
    }

    NSString *goodsId = [candidate[@"goodsId"] isKindOfClass:[NSString class]] ? candidate[@"goodsId"] : @"";
    NSString *jsonText = [candidate[@"rawText"] isKindOfClass:[NSString class]] ? candidate[@"rawText"] : @"";
    if (goodsId.length == 0 || jsonText.length == 0 || taskCurrentRawDir.length == 0) {
        return NO;
    }

    NSString *txtFileName = [NSString stringWithFormat:@"%@.txt", goodsId];
    NSString *txtPath = [taskCurrentRawDir stringByAppendingPathComponent:txtFileName];
    NSError *writeError = nil;
    [jsonText writeToFile:txtPath atomically:YES encoding:NSUTF8StringEncoding error:&writeError];
    if (writeError) {
        taskFailedCount++;
        taskConsecutiveFailureCount++;
        task_writeCurrentTaskStatus();
        return NO;
    }

    BOOL isNewGoods = NO;
    @synchronized(task_sharedStateLock()) {
        isNewGoods = ![taskSeenGoodsIds containsObject:goodsId];
        if (isNewGoods) {
            [taskSeenGoodsIds addObject:goodsId];
        }
    }
    taskLastGoodsId = goodsId;
    if (isNewGoods) {
        taskLastDetailCandidateWasDuplicate = NO;
        [taskSavedFiles addObject:txtFileName];
        taskSavedCount++;
        taskConsecutiveFailureCount = 0;
        taskConsecutiveNoNewRounds = 0;
        taskConsecutiveUnknownOrClickFailures = 0;
        task_writeCurrentTaskStatus();
        NSString *goodsName = [candidate[@"goodsName"] isKindOfClass:[NSString class]] ? candidate[@"goodsName"] : @"";
        ui_showTaskCaptureToast(goodsName);
        return YES;
    }

    taskLastDetailCandidateWasDuplicate = YES;
    debug_writeTaskDump(@"detail_duplicate_goods", [NSString stringWithFormat:
        @"goods_id=%@\nlast_goods_id=%@\nsaved_count=%ld\nattempted_count=%ld",
        goodsId,
        taskLastGoodsId ?: @"",
        (long)taskSavedCount,
        (long)taskAttemptedCount]);
    return NO;
}

static NSDictionary *capture_acceptedDetailPayloadFromObject(id jsonObject, NSString *urlString, NSInteger *scoreOut) {
    NSDictionary *detailPayload = capture_detailPayloadContainerFromObject(jsonObject, 3);
    if (!detailPayload) {
        return nil;
    }

    if (capture_payloadLooksLikeTrackingEvent(jsonObject) && detailPayload == jsonObject) {
        return nil;
    }

    NSInteger score = capture_detailPayloadScore(detailPayload, urlString);
    if (capture_payloadLooksLikeSearchResultEvent(jsonObject) && score < 260) {
        return nil;
    }
    if (score < 220) {
        return nil;
    }
    if (capture_goodsIdFromDetailPayload(detailPayload).length == 0) {
        return nil;
    }

    if (scoreOut) {
        *scoreOut = score;
    }
    return detailPayload;
}

static NSDictionary *capture_runtimeFallbackCandidateForCurrentPage(void) {
    id controller = ui_currentRootViewController();
    if (!controller) {
        return nil;
    }

    NSMutableSet *visited = [NSMutableSet set];
    NSMutableArray *roots = [NSMutableArray array];
    [roots addObject:@{@"object": controller, @"path": @"rootVC"}];
    runtime_appendProbeChildren(controller, @"rootVC", roots);

    id controllerView = [controller respondsToSelector:@selector(view)] ? ((id (*)(id, SEL))objc_msgSend)(controller, @selector(view)) : nil;
    if (controllerView) {
        [roots addObject:@{@"object": controllerView, @"path": @"rootView"}];
        runtime_appendProbeChildren(controllerView, @"rootView", roots);
    }

    id navigationItem = [controller respondsToSelector:@selector(navigationItem)] ? ((id (*)(id, SEL))objc_msgSend)(controller, @selector(navigationItem)) : nil;
    if (navigationItem) {
        [roots addObject:@{@"object": navigationItem, @"path": @"navigationItem"}];
        runtime_appendProbeChildren(navigationItem, @"navigationItem", roots);
    }

    NSMutableArray *controllerQueue = [NSMutableArray arrayWithObject:controller];
    NSUInteger controllerIndex = 0;
    while (controllerIndex < controllerQueue.count && controllerIndex < 24) {
        id candidateController = controllerQueue[controllerIndex++];
        if (!candidateController) {
            continue;
        }

        NSString *candidateName = NSStringFromClass([candidateController class]) ?: @"UnknownVC";
        if (candidateController != controller) {
            NSString *controllerPath = [NSString stringWithFormat:@"childVC.%@", candidateName];
            [roots addObject:@{@"object": candidateController, @"path": controllerPath}];
            runtime_appendProbeChildren(candidateController, controllerPath, roots);

            id candidateView = [candidateController respondsToSelector:@selector(view)] ? ((id (*)(id, SEL))objc_msgSend)(candidateController, @selector(view)) : nil;
            if (candidateView) {
                NSString *viewPath = [NSString stringWithFormat:@"childView.%@", candidateName];
                [roots addObject:@{@"object": candidateView, @"path": viewPath}];
                runtime_appendProbeChildren(candidateView, viewPath, roots);
            }
        }

        NSArray *children = nil;
        if ([candidateController respondsToSelector:@selector(childViewControllers)]) {
            children = [candidateController childViewControllers];
        }
        for (id child in children) {
            if (child) {
                [controllerQueue addObject:child];
            }
        }

        if ([candidateController isKindOfClass:[UINavigationController class]]) {
            UINavigationController *navigationController = (UINavigationController *)candidateController;
            if (navigationController.visibleViewController) {
                [controllerQueue addObject:navigationController.visibleViewController];
            }
            if (navigationController.topViewController) {
                [controllerQueue addObject:navigationController.topViewController];
            }
        }
        if ([candidateController isKindOfClass:[UITabBarController class]]) {
            UITabBarController *tabBarController = (UITabBarController *)candidateController;
            if (tabBarController.selectedViewController) {
                [controllerQueue addObject:tabBarController.selectedViewController];
            }
            for (UIViewController *vc in tabBarController.viewControllers ?: @[]) {
                if (vc) {
                    [controllerQueue addObject:vc];
                }
            }
        }

        id presentedViewController = [candidateController respondsToSelector:@selector(presentedViewController)] ?
            ((id (*)(id, SEL))objc_msgSend)(candidateController, @selector(presentedViewController)) :
            nil;
        if (presentedViewController) {
            [controllerQueue addObject:presentedViewController];
        }
    }

    UIView *rootView = [controllerView isKindOfClass:[UIView class]] ? (UIView *)controllerView : nil;
    NSMutableArray<UIView *> *viewQueue = rootView ? [NSMutableArray arrayWithObject:rootView] : [NSMutableArray array];
    NSArray<NSString *> *interestingFragments = @[@"Detail", @"Goods", @"Bottom", @"Price", @"Title", @"Sku", @"Buy", @"Mall"];
    NSUInteger viewIndex = 0;
    while (viewIndex < viewQueue.count && viewIndex < 320) {
        UIView *view = viewQueue[viewIndex++];
        if (!view || (view != rootView && !ui_viewIsVisible(view))) {
            continue;
        }

        NSString *className = NSStringFromClass([view class]) ?: @"";
        BOOL interesting = NO;
        for (NSString *fragment in interestingFragments) {
            if ([className containsString:fragment]) {
                interesting = YES;
                break;
            }
        }
        if (interesting) {
            NSString *viewPath = [NSString stringWithFormat:@"view.%@", className];
            [roots addObject:@{@"object": view, @"path": viewPath}];
            runtime_appendProbeChildren(view, viewPath, roots);

            UIResponder *responder = view;
            NSInteger responderDepth = 0;
            while (responder && responderDepth < 6) {
                responder = responder.nextResponder;
                if (!responder) {
                    break;
                }
                NSString *responderPath = [NSString stringWithFormat:@"responder.%@.%ld", className, (long)responderDepth];
                [roots addObject:@{@"object": responder, @"path": responderPath}];
                runtime_appendProbeChildren(responder, responderPath, roots);
                responderDepth++;
            }
        }

        if (view.subviews.count > 0) {
            [viewQueue addObjectsFromArray:view.subviews];
        }
    }

    for (NSDictionary *entry in roots) {
        id object = entry[@"object"];
        if (!object || object == [NSNull null]) {
            continue;
        }
        NSDictionary *candidate = runtime_candidateFromInspectableObject(object, entry[@"path"], 5, visited);
        if (candidate) {
            return candidate;
        }
    }

    return nil;
}

static NSString *capture_jsonStringFromData(NSData *data) {
    if (!data || data.length == 0) {
        return nil;
    }
    NSString *rawText = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (rawText.length == 0) {
        rawText = [[NSString alloc] initWithData:data encoding:NSASCIIStringEncoding];
    }
    return rawText;
}

static NSString *capture_observedGoodsIdString(id jsonObject) {
    if (!jsonObject) {
        return @"";
    }

    id goodsIdValue = json_findNestedValueForKey(jsonObject, @"goods_id", 6);
    if (!goodsIdValue) {
        goodsIdValue = json_findNestedValueForKey(jsonObject, @"goodsId", 6);
    }
    NSString *goodsId = json_stringFromValue(goodsIdValue);
    return goodsId ?: @"";
}

static NSDictionary *capture_detailCandidateFromObservedObject(id jsonObject, NSString *source, NSString *urlString, NSString *rawText) {
    NSInteger detailScore = 0;
    NSDictionary *detailPayload = capture_acceptedDetailPayloadFromObject(jsonObject, urlString, &detailScore);
    if (!detailPayload) {
        return nil;
    }

    NSString *goodsId = capture_goodsIdFromDetailPayload(detailPayload);
    if (goodsId.length == 0) {
        return nil;
    }

    NSString *goodsName = capture_goodsNameFromDetailPayload(detailPayload);
    NSString *jsonText = (detailPayload == jsonObject)
        ? capture_normalizedJSONString(rawText, detailPayload)
        : capture_normalizedJSONString(nil, detailPayload);
    if (jsonText.length == 0) {
        return nil;
    }

    return @{
        @"goodsId": goodsId,
        @"goodsName": goodsName ?: @"",
        @"rawText": jsonText,
        @"url": urlString ?: @"",
        @"source": source ?: @"",
        @"score": @(detailScore),
        @"capturedAt": @([[NSDate date] timeIntervalSince1970])
    };
}

