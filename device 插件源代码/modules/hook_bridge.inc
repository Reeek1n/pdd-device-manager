// 稳定版逻辑：所有功能都在本地，不需要云端下载
static void capture_handleObservedJSONObject(id jsonObject, NSString *source, NSString *url, NSString *rawText) {
    @try {
        if (!tweakIsEnabled) {
            return;
        }

        initializePDDStoragePaths();
        captureObservedPayloadSequence++;

        NSString *observedGoodsId = capture_observedGoodsIdString(jsonObject);

        if (task_hasActiveTask() &&
            taskNeedsBackNavigation &&
            capture_payloadLooksLikeDetailPageSignal(jsonObject, url)) {
            if (observedGoodsId.length > 0) {
                taskLastGoodsId = observedGoodsId;
            }
            taskObservedDetailSignalAt = [[NSDate date] timeIntervalSince1970];
        }

        NSDictionary *candidate = capture_detailCandidateFromObservedObject(jsonObject, source, url, rawText);
        if (!candidate) {
            NSString *debugGoodsId = observedGoodsId.length > 0 ?
                observedGoodsId :
                [NSString stringWithFormat:@"unknown_%ld", (long)captureObservedPayloadSequence];
            capture_cacheSearchSnapshot(jsonObject, debugGoodsId);
            debug_writeCandidatePayload(jsonObject, source, url, debugGoodsId);
            return;
        }

        NSString *goodsId = [candidate[@"goodsId"] isKindOfClass:[NSString class]] ? candidate[@"goodsId"] : @"";
        NSString *jsonText = [candidate[@"rawText"] isKindOfClass:[NSString class]] ? candidate[@"rawText"] : @"";
        if (goodsId.length == 0 || jsonText.length == 0) {
            return;
        }

        NSString *txtFileName = [NSString stringWithFormat:@"%@.txt", goodsId];
        NSString *txtPath = nil;
        if (task_hasActiveTask() && taskCurrentRawDir.length > 0) {
            txtPath = [taskCurrentRawDir stringByAppendingPathComponent:txtFileName];
        } else {
            txtPath = [taskDataRootDir stringByAppendingPathComponent:txtFileName];
        }
        
        if (task_hasActiveTask()) {
            capture_storeRecentCandidate(candidate);
            return;
        }

        if (jsonText.length > 0) {
            NSError *rawErr = nil;
            [jsonText writeToFile:txtPath atomically:YES encoding:NSUTF8StringEncoding error:&rawErr];
            if (rawErr) {
                return;
            }
        }
    } @catch (NSException *exception) {
    }
}


static id ui_currentRootViewController(void) {
    @try {
        id rootVC = nil;
        Class UIApplicationClass = NSClassFromString(@"UIApplication");
        if (UIApplicationClass) {
            SEL sharedApplicationSel = NSSelectorFromString(@"sharedApplication");
            id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, sharedApplicationSel);
            if (app) {
                if (@available(iOS 13.0, *)) {
                    NSSet *scenes = [app valueForKey:@"connectedScenes"];
                    for (id scene in scenes) {
                        id delegate = [scene valueForKey:@"delegate"];
                        if (delegate) {
                            id windowScene = scene;
                            SEL windowsSel = NSSelectorFromString(@"windows");
                            NSArray *windows = ((NSArray * (*)(id, SEL))objc_msgSend)(windowScene, windowsSel);
                            if (windows && windows.count > 0) {
                                id window = windows.firstObject;
                                rootVC = [window valueForKey:@"rootViewController"];
                                if (rootVC) break;
                            }
                        }
                    }
                }
                
                if (!rootVC) {
                    id keyWindow = [app valueForKey:@"keyWindow"];
                    if (keyWindow) {
                        rootVC = [keyWindow valueForKey:@"rootViewController"];
                    }
                }
            }
        }
        
        if (rootVC) {
            id presentedVC = [rootVC valueForKey:@"presentedViewController"];
            while (presentedVC) {
                rootVC = presentedVC;
                presentedVC = [rootVC valueForKey:@"presentedViewController"];
            }
        }
        
        return rootVC;
    } @catch (NSException *exception) {
        // 异常
        return nil;
    }
}

static IMP hookOriginalJSONObjectWithDataIMP = NULL;
static IMP hookOriginalDataTaskWithRequestIMP = NULL;
static IMP hookOriginalDataTaskWithRequestNoHandlerIMP = NULL;
static IMP hookOriginalDataTaskWithURLIMP = NULL;
static IMP hookOriginalDataTaskWithURLNoHandlerIMP = NULL;
static IMP hookOriginalSetHTTPBodyIMP = NULL;
static IMP hookOriginalUploadTaskWithRequestFromDataIMP = NULL;
static IMP hookOriginalUploadTaskWithRequestFromDataCompletionIMP = NULL;
static IMP hookOriginalSessionWithConfigurationDelegateQueueIMP = NULL;
static IMP hookOriginalTaskResumeIMP = NULL;
static NSMutableDictionary *hookOriginalDelegateIMPsByKey = nil;
static NSMutableDictionary *hookTaskResponseBuffersByKey = nil;

static NSString *hook_delegateHookKey(Class cls, SEL sel);
static BOOL hook_classDefinesSelector(Class cls, SEL sel);
static IMP hook_originalDelegateIMP(id self, SEL sel);
static NSString *hook_taskBufferKey(id task);
static BOOL hook_shouldCaptureTask(id task, NSURLResponse *response);
static void hook_beginTaskBuffer(id task, NSURLResponse *response);
static void hook_appendTaskBuffer(id task, NSData *data);
static NSData *hook_finishTaskBuffer(id task);

// Hook setHTTPBody: 修改请求中的 enter_time
static void hook_NSMutableURLRequest_setHTTPBody(id self, SEL _cmd, NSData *body) {
    void (*originalSetHTTPBodyIMP)(id, SEL, NSData *) = (void (*)(id, SEL, NSData *))hookOriginalSetHTTPBodyIMP;
    
    @try {
        if (!task_hasActiveTask()) {
            originalSetHTTPBodyIMP(self, _cmd, body);
            return;
        }
        if (body && body.length > 0) {
            debug_writeTaskLifecycle(@"set_http_body", nil, self, nil, body, nil, nil);
            // 尝试解析为 JSON
            Class jsonSerializationClass = NSClassFromString(@"NSJSONSerialization");
            if (jsonSerializationClass) {
                SEL jsonObjectSelector = NSSelectorFromString(@"JSONObjectWithData:options:error:");
                NSError *jsonError = nil;
                id parsedJSONObject = ((id (*)(Class, SEL, NSData *, NSInteger, NSError **))objc_msgSend)(jsonSerializationClass, jsonObjectSelector, body, 0, &jsonError);
                
                if (parsedJSONObject && !jsonError && [parsedJSONObject isKindOfClass:[NSDictionary class]]) {
                    NSMutableDictionary *mutableRequestPayload = [parsedJSONObject mutableCopy];
                    BOOL didModifyRequestBody = NO;
                    
                    // 检查是否是商品详情页请求（通过 page_name 或其他标识）
                    id pageNameValue = mutableRequestPayload[@"page_name"];
                    BOOL requestLooksLikeGoodsDetail = NO;
                    if (pageNameValue && [pageNameValue isKindOfClass:[NSString class]]) {
                        NSString *pageNameText = (NSString *)pageNameValue;
                        if ([pageNameText containsString:@"goods_detail"] || [pageNameText containsString:@"detail"]) {
                            requestLooksLikeGoodsDetail = YES;
                        }
                    }
                    
                    // 如果没有 page_name，检查 URL 或其他字段
                    if (!requestLooksLikeGoodsDetail) {
                        SEL requestURLSelector = NSSelectorFromString(@"URL");
                        id requestURLObject = ((id (*)(id, SEL))objc_msgSend)(self, requestURLSelector);
                        if (requestURLObject) {
                            SEL absoluteStringSelector = NSSelectorFromString(@"absoluteString");
                            NSString *requestURLText = ((NSString * (*)(id, SEL))objc_msgSend)(requestURLObject, absoluteStringSelector);
                            if (requestURLText && ([requestURLText containsString:@"goods_detail"] || [requestURLText containsString:@"detail"])) {
                                requestLooksLikeGoodsDetail = YES;
                            }
                        }
                    }
                    
                    // 如果找到 enter_time 字段，修改它（减去 10-30 秒，模拟已浏览一段时间）
                    if (requestLooksLikeGoodsDetail) {
                        debug_writeRequest(@"setHTTPBody", self, body);
                        id enterTimeValue = mutableRequestPayload[@"enter_time"];
                        if (enterTimeValue) {
                            NSTimeInterval currentTimestamp = [[NSDate date] timeIntervalSince1970];
                            double originalEnterTimeValue = 0;
                            BOOL isMilliseconds = NO;
                            
                            if ([enterTimeValue isKindOfClass:[NSNumber class]]) {
                                originalEnterTimeValue = [enterTimeValue doubleValue];
                            } else if ([enterTimeValue isKindOfClass:[NSString class]]) {
                                originalEnterTimeValue = [enterTimeValue doubleValue];
                            }
                            
                            // 判断是秒还是毫秒（如果值大于 1e12，可能是毫秒）
                            NSTimeInterval originalEnterTimeSeconds = originalEnterTimeValue;
                            if (originalEnterTimeValue > 1e12) {
                                originalEnterTimeSeconds = originalEnterTimeValue / 1000.0;
                                isMilliseconds = YES;
                            }
                            
                            // 如果 enter_time 是当前时间或接近当前时间，减去 15-30 秒
                            NSTimeInterval secondsSinceEnter = currentTimestamp - originalEnterTimeSeconds;
                            if (secondsSinceEnter < 5.0) { // 如果时间差小于 5 秒，说明是刚进入的
                                // 减去 15-30 秒（随机），模拟已浏览一段时间
                                NSTimeInterval fakeDelay = 15.0 + (arc4random() % 15000) / 1000.0; // 15-30 秒
                                NSTimeInterval fakeEnterTimestamp = currentTimestamp - fakeDelay;
                                
                                // 根据原始格式返回（秒或毫秒）
                                if (isMilliseconds) {
                                    mutableRequestPayload[@"enter_time"] = @((long long)(fakeEnterTimestamp * 1000));
                                } else {
                                    mutableRequestPayload[@"enter_time"] = @((long long)fakeEnterTimestamp);
                                }
                                didModifyRequestBody = YES;
                            }
                        }
                    }
                    
                    // 如果修改了，重新序列化为 JSON
                    if (didModifyRequestBody) {
                        SEL dataWithJSONObjectSelector = NSSelectorFromString(@"dataWithJSONObject:options:error:");
                        NSError *serializeError = nil;
                        NSData *updatedRequestBody = ((NSData * (*)(Class, SEL, id, NSInteger, NSError **))objc_msgSend)(jsonSerializationClass, dataWithJSONObjectSelector, mutableRequestPayload, 0, &serializeError);
                        
                        if (updatedRequestBody && !serializeError) {
                            originalSetHTTPBodyIMP(self, _cmd, updatedRequestBody);
                            return;
                        }
                    }
                }
            }
        }
    } @catch (NSException *exception) {
    }
    
    // 如果没有修改或出错，使用原始实现
    originalSetHTTPBodyIMP(self, _cmd, body);
}

static id hook_NSJSONSerialization_JSONObjectWithData(id self, SEL _cmd, NSData *data, NSJSONReadingOptions opt, NSError **error) {
    id (*originalJSONObjectWithDataIMP)(id, SEL, NSData *, NSJSONReadingOptions, NSError **) = (id (*)(id, SEL, NSData *, NSJSONReadingOptions, NSError **))hookOriginalJSONObjectWithDataIMP;
    id parsedJSONObject = originalJSONObjectWithDataIMP(self, _cmd, data, opt, error);
    
    @try {
        if (parsedJSONObject && data) {
            capture_handleObservedJSONObject(parsedJSONObject, @"NSJSONSerialization", nil, capture_jsonStringFromData(data));
        }
    } @catch (NSException *exception) {
    }
    
    return parsedJSONObject;
}

static NSURLSessionDataTask *hook_NSURLSession_dataTaskWithRequest_completionHandler(id self, SEL _cmd, NSURLRequest *request, void (^completionHandler)(NSData *, NSURLResponse *, NSError *)) {
    NSURLSessionDataTask * (*originalDataTaskWithRequestIMP)(id, SEL, NSURLRequest *, void (^)(NSData *, NSURLResponse *, NSError *)) = (NSURLSessionDataTask * (*)(id, SEL, NSURLRequest *, void (^)(NSData *, NSURLResponse *, NSError *)))hookOriginalDataTaskWithRequestIMP;
    
    void (^wrappedCompletionHandler)(NSData *, NSURLResponse *, NSError *) = ^(NSData *responseData, NSURLResponse *response, NSError *taskError) {
        if (responseData && !taskError) {
            @try {
                debug_writeNetworkResponse(request, response, responseData);
                NSString *capturedRawText = capture_jsonStringFromData(responseData);
                
                NSError *jsonError;
                Class jsonSerializationClass = NSClassFromString(@"NSJSONSerialization");
                if (jsonSerializationClass) {
                    SEL jsonObjectSelector = NSSelectorFromString(@"JSONObjectWithData:options:error:");
                    id parsedJSONObject = ((id (*)(Class, SEL, NSData *, NSInteger, NSError **))objc_msgSend)(jsonSerializationClass, jsonObjectSelector, responseData, 1, &jsonError);
                    if (parsedJSONObject && !jsonError) {
                        NSString *requestURLText = request.URL.absoluteString;
                        capture_handleObservedJSONObject(parsedJSONObject, @"dataTaskWithRequest", requestURLText, capturedRawText);
                    }
                }
            } @catch (NSException *exception) {
            }
        }
        
        if (completionHandler) {
            completionHandler(responseData, response, taskError);
        }
    };
    
    return originalDataTaskWithRequestIMP(self, _cmd, request, wrappedCompletionHandler);
}

static NSURLSessionDataTask *hook_NSURLSession_dataTaskWithRequest(id self, SEL _cmd, NSURLRequest *request) {
    NSURLSessionDataTask * (*originalIMP)(id, SEL, NSURLRequest *) =
        (NSURLSessionDataTask * (*)(id, SEL, NSURLRequest *))hookOriginalDataTaskWithRequestNoHandlerIMP;
    @try {
        NSData *bodyData = [request respondsToSelector:NSSelectorFromString(@"HTTPBody")] ?
            ((NSData * (*)(id, SEL))objc_msgSend)(request, NSSelectorFromString(@"HTTPBody")) :
            nil;
        debug_writeRequest(@"dataTaskWithRequest", request, bodyData);
    } @catch (NSException *exception) {
    }
    return originalIMP ? originalIMP(self, _cmd, request) : nil;
}

static NSURLSessionUploadTask *hook_NSURLSession_uploadTaskWithRequest_fromData(id self, SEL _cmd, NSURLRequest *request, NSData *bodyData) {
    NSURLSessionUploadTask * (*originalIMP)(id, SEL, NSURLRequest *, NSData *) =
        (NSURLSessionUploadTask * (*)(id, SEL, NSURLRequest *, NSData *))hookOriginalUploadTaskWithRequestFromDataIMP;
    @try {
        debug_writeRequest(@"uploadTaskWithRequest", request, bodyData);
    } @catch (NSException *exception) {
    }
    return originalIMP ? originalIMP(self, _cmd, request, bodyData) : nil;
}

static NSURLSessionUploadTask *hook_NSURLSession_uploadTaskWithRequest_fromData_completionHandler(id self, SEL _cmd, NSURLRequest *request, NSData *bodyData, void (^completionHandler)(NSData *, NSURLResponse *, NSError *)) {
    NSURLSessionUploadTask * (*originalIMP)(id, SEL, NSURLRequest *, NSData *, void (^)(NSData *, NSURLResponse *, NSError *)) =
        (NSURLSessionUploadTask * (*)(id, SEL, NSURLRequest *, NSData *, void (^)(NSData *, NSURLResponse *, NSError *)))hookOriginalUploadTaskWithRequestFromDataCompletionIMP;
    @try {
        debug_writeRequest(@"uploadTaskWithRequestCompletion", request, bodyData);
    } @catch (NSException *exception) {
    }

    void (^wrappedHandler)(NSData *, NSURLResponse *, NSError *) = ^(NSData *data, NSURLResponse *response, NSError *error) {
        if (data && !error) {
            @try {
                debug_writeNetworkResponse(request, response, data);
                NSString *rawText = capture_jsonStringFromData(data);
                NSError *jsonError = nil;
                Class jsonClass = NSClassFromString(@"NSJSONSerialization");
                if (jsonClass) {
                    SEL jsonSel = NSSelectorFromString(@"JSONObjectWithData:options:error:");
                    id jsonObject = ((id (*)(Class, SEL, NSData *, NSInteger, NSError **))objc_msgSend)(jsonClass, jsonSel, data, 1, &jsonError);
                    if (jsonObject && !jsonError) {
                        NSString *url = request.URL.absoluteString ?: @"";
                        capture_handleObservedJSONObject(jsonObject, @"uploadTaskWithRequest", url, rawText);
                    }
                }
            } @catch (NSException *exception) {
            }
        }
        if (completionHandler) {
            completionHandler(data, response, error);
        }
    };

    return originalIMP ? originalIMP(self, _cmd, request, bodyData, wrappedHandler) : nil;
}

static NSURLSession *hook_NSURLSession_sessionWithConfiguration_delegate_delegateQueue(id self, SEL _cmd, NSURLSessionConfiguration *configuration, id delegate, NSOperationQueue *queue) {
    NSURLSession * (*originalIMP)(id, SEL, NSURLSessionConfiguration *, id, NSOperationQueue *) =
        (NSURLSession * (*)(id, SEL, NSURLSessionConfiguration *, id, NSOperationQueue *))hookOriginalSessionWithConfigurationDelegateQueueIMP;
    NSURLSession *session = originalIMP ? originalIMP(self, _cmd, configuration, delegate, queue) : nil;
    @try {
        if (delegate) {
            hook_installURLSessionDelegateClassHooks([delegate class]);
        }
        debug_writeTaskLifecycle(@"session_create", nil, nil, nil, nil, session, delegate);
    } @catch (NSException *exception) {
    }
    return session;
}

static void hook_NSURLSessionTask_resume(id self, SEL _cmd) {
    void (*originalIMP)(id, SEL) = (void (*)(id, SEL))hookOriginalTaskResumeIMP;
    @try {
        debug_writeTaskLifecycle(@"task_resume", self, nil, nil, nil, nil, nil);
    } @catch (NSException *exception) {
    }
    if (originalIMP) {
        originalIMP(self, _cmd);
    }
}

static NSURLSessionDataTask *hook_NSURLSession_dataTaskWithURL(id self, SEL _cmd, NSURL *url) {
    NSURLSessionDataTask * (*originalIMP)(id, SEL, NSURL *) =
        (NSURLSessionDataTask * (*)(id, SEL, NSURL *))hookOriginalDataTaskWithURLNoHandlerIMP;
    NSURLSessionDataTask *task = originalIMP ? originalIMP(self, _cmd, url) : nil;
    @try {
        NSURLRequest *request = url ? [NSURLRequest requestWithURL:url] : nil;
        debug_writeRequest(@"dataTaskWithURL", request, nil);
        debug_writeTaskLifecycle(@"task_create_url", task, request, nil, nil, self, nil);
    } @catch (NSException *exception) {
    }
    return task;
}

static NSURLSessionDataTask *hook_NSURLSession_dataTaskWithURL_completionHandler(id self, SEL _cmd, NSURL *url, void (^completionHandler)(NSData *, NSURLResponse *, NSError *)) {
    NSURLSessionDataTask * (*originalIMP)(id, SEL, NSURL *, void (^)(NSData *, NSURLResponse *, NSError *)) =
        (NSURLSessionDataTask * (*)(id, SEL, NSURL *, void (^)(NSData *, NSURLResponse *, NSError *)))hookOriginalDataTaskWithURLIMP;

    void (^wrappedHandler)(NSData *, NSURLResponse *, NSError *) = ^(NSData *data, NSURLResponse *response, NSError *error) {
        if (data && !error) {
            @try {
                NSURLRequest *request = url ? [NSURLRequest requestWithURL:url] : nil;
                debug_writeNetworkResponse(request, response, data);
                debug_writeTaskLifecycle(@"task_complete_url", nil, request, response, data, self, nil);
                NSString *rawText = capture_jsonStringFromData(data);

                NSError *jsonError = nil;
                Class jsonClass = NSClassFromString(@"NSJSONSerialization");
                if (jsonClass) {
                    SEL jsonSel = NSSelectorFromString(@"JSONObjectWithData:options:error:");
                    id jsonObject = ((id (*)(Class, SEL, NSData *, NSInteger, NSError **))objc_msgSend)(jsonClass, jsonSel, data, 1, &jsonError);
                    if (jsonObject && !jsonError) {
                        NSString *urlString = url.absoluteString ?: @"";
                        capture_handleObservedJSONObject(jsonObject, @"dataTaskWithURL", urlString, rawText);
                    }
                }
            } @catch (NSException *exception) {
            }
        }

        if (completionHandler) {
            completionHandler(data, response, error);
        }
    };

    NSURLSessionDataTask *task = originalIMP ? originalIMP(self, _cmd, url, wrappedHandler) : nil;
    @try {
        NSURLRequest *request = url ? [NSURLRequest requestWithURL:url] : nil;
        debug_writeRequest(@"dataTaskWithURL", request, nil);
        debug_writeTaskLifecycle(@"task_create_url_completion", task, request, nil, nil, self, nil);
    } @catch (NSException *exception) {
    }
    return task;
}

static NSString *hook_delegateHookKey(Class cls, SEL sel) {
    if (!cls || !sel) {
        return @"";
    }
    return [NSString stringWithFormat:@"%s|%s", class_getName(cls), sel_getName(sel)];
}

static BOOL hook_classDefinesSelector(Class cls, SEL sel) {
    if (!cls || !sel) {
        return NO;
    }

    unsigned int methodCount = 0;
    Method *methods = class_copyMethodList(cls, &methodCount);
    BOOL found = NO;
    for (unsigned int idx = 0; idx < methodCount; idx++) {
        if (method_getName(methods[idx]) == sel) {
            found = YES;
            break;
        }
    }
    if (methods) {
        free(methods);
    }
    return found;
}

static IMP hook_originalDelegateIMP(id self, SEL sel) {
    if (!self || !sel || !hookOriginalDelegateIMPsByKey) {
        return NULL;
    }
    NSString *key = hook_delegateHookKey([self class], sel);
    NSValue *value = hookOriginalDelegateIMPsByKey[key];
    return value ? [value pointerValue] : NULL;
}

static void hook_installURLSessionDelegateClassHooks(Class cls) {
    if (!cls) {
        return;
    }
    if (!hookOriginalDelegateIMPsByKey) {
        hookOriginalDelegateIMPsByKey = [NSMutableDictionary dictionary];
    }

    SEL didReceiveResponseSel = NSSelectorFromString(@"URLSession:dataTask:didReceiveResponse:completionHandler:");
    SEL didReceiveDataSel = NSSelectorFromString(@"URLSession:dataTask:didReceiveData:");
    SEL didCompleteSel = NSSelectorFromString(@"URLSession:task:didCompleteWithError:");

    if (hook_classDefinesSelector(cls, didReceiveResponseSel)) {
        Method method = class_getInstanceMethod(cls, didReceiveResponseSel);
        NSString *key = hook_delegateHookKey(cls, didReceiveResponseSel);
        if (method && !hookOriginalDelegateIMPsByKey[key]) {
            IMP original = method_getImplementation(method);
            hookOriginalDelegateIMPsByKey[key] = [NSValue valueWithPointer:original];
            method_setImplementation(method, (IMP)hook_NSURLSessionDelegate_didReceiveResponse);
        }
    }

    if (hook_classDefinesSelector(cls, didReceiveDataSel)) {
        Method method = class_getInstanceMethod(cls, didReceiveDataSel);
        NSString *key = hook_delegateHookKey(cls, didReceiveDataSel);
        if (method && !hookOriginalDelegateIMPsByKey[key]) {
            IMP original = method_getImplementation(method);
            hookOriginalDelegateIMPsByKey[key] = [NSValue valueWithPointer:original];
            method_setImplementation(method, (IMP)hook_NSURLSessionDelegate_didReceiveData);
        }
    }

    if (hook_classDefinesSelector(cls, didCompleteSel)) {
        Method method = class_getInstanceMethod(cls, didCompleteSel);
        NSString *key = hook_delegateHookKey(cls, didCompleteSel);
        if (method && !hookOriginalDelegateIMPsByKey[key]) {
            IMP original = method_getImplementation(method);
            hookOriginalDelegateIMPsByKey[key] = [NSValue valueWithPointer:original];
            method_setImplementation(method, (IMP)hook_NSURLSessionDelegate_taskDidComplete);
        }
    }
}

static NSString *hook_taskBufferKey(id task) {
    if (!task) {
        return @"";
    }
    return [NSString stringWithFormat:@"%p", task];
}

static BOOL hook_shouldCaptureTask(id task, NSURLResponse *response) {
    if (!tweakIsEnabled || !task) {
        return NO;
    }

    @try {
        NSString *mimeType = @"";
        if ([response respondsToSelector:NSSelectorFromString(@"MIMEType")]) {
            mimeType = ((NSString * (*)(id, SEL))objc_msgSend)(response, NSSelectorFromString(@"MIMEType")) ?: @"";
        }
        if ([mimeType hasPrefix:@"image/"] || [mimeType hasPrefix:@"video/"] || [mimeType hasPrefix:@"audio/"]) {
            return NO;
        }

        NSURLRequest *request = [task respondsToSelector:NSSelectorFromString(@"currentRequest")] ?
            ((NSURLRequest * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"currentRequest")) :
            nil;
        NSString *url = request.URL.absoluteString ?: @"";
        if ([url hasSuffix:@".jpg"] || [url hasSuffix:@".jpeg"] || [url hasSuffix:@".png"] ||
            [url hasSuffix:@".gif"] || [url hasSuffix:@".webp"] || [url hasSuffix:@".mp4"]) {
            return NO;
        }
    } @catch (NSException *exception) {
    }

    return YES;
}

static void hook_beginTaskBuffer(id task, NSURLResponse *response) {
    if (!hook_shouldCaptureTask(task, response)) {
        return;
    }
    if (!hookTaskResponseBuffersByKey) {
        hookTaskResponseBuffersByKey = [NSMutableDictionary dictionary];
    }
    NSString *key = hook_taskBufferKey(task);
    if (key.length == 0) {
        return;
    }
    if (!hookTaskResponseBuffersByKey[key]) {
        hookTaskResponseBuffersByKey[key] = [NSMutableData data];
    }
}

static void hook_appendTaskBuffer(id task, NSData *data) {
    if (!task || !data || data.length == 0 || !hookTaskResponseBuffersByKey) {
        return;
    }
    NSString *key = hook_taskBufferKey(task);
    NSMutableData *buffer = [hookTaskResponseBuffersByKey[key] isKindOfClass:[NSMutableData class]] ? hookTaskResponseBuffersByKey[key] : nil;
    if (!buffer) {
        return;
    }
    NSUInteger maxCaptureLength = 1024 * 1024;
    if (buffer.length >= maxCaptureLength) {
        return;
    }
    NSUInteger remaining = maxCaptureLength - buffer.length;
    if (data.length <= remaining) {
        [buffer appendData:data];
    } else {
        [buffer appendData:[data subdataWithRange:NSMakeRange(0, remaining)]];
    }
}

static NSData *hook_finishTaskBuffer(id task) {
    if (!task || !hookTaskResponseBuffersByKey) {
        return nil;
    }
    NSString *key = hook_taskBufferKey(task);
    NSData *data = [hookTaskResponseBuffersByKey[key] copy];
    [hookTaskResponseBuffersByKey removeObjectForKey:key];
    return data;
}

static void hook_NSURLSessionDelegate_didReceiveResponse(id self, SEL _cmd, NSURLSession *session, NSURLSessionDataTask *dataTask, NSURLResponse *response, void (^completionHandler)(NSInteger)) {
    @try {
        hook_beginTaskBuffer(dataTask, response);
    } @catch (NSException *exception) {
    }

    void (*originalIMP)(id, SEL, NSURLSession *, NSURLSessionDataTask *, NSURLResponse *, void (^)(NSInteger)) =
        (void (*)(id, SEL, NSURLSession *, NSURLSessionDataTask *, NSURLResponse *, void (^)(NSInteger)))hook_originalDelegateIMP(self, _cmd);
    if (originalIMP) {
        originalIMP(self, _cmd, session, dataTask, response, completionHandler);
    } else if (completionHandler) {
        completionHandler(1);
    }
}

static void hook_NSURLSessionDelegate_didReceiveData(id self, SEL _cmd, NSURLSession *session, NSURLSessionDataTask *dataTask, NSData *data) {
    @try {
        hook_appendTaskBuffer(dataTask, data);
    } @catch (NSException *exception) {
    }

    void (*originalIMP)(id, SEL, NSURLSession *, NSURLSessionDataTask *, NSData *) =
        (void (*)(id, SEL, NSURLSession *, NSURLSessionDataTask *, NSData *))hook_originalDelegateIMP(self, _cmd);
    if (originalIMP) {
        originalIMP(self, _cmd, session, dataTask, data);
    }
}

static void hook_NSURLSessionDelegate_taskDidComplete(id self, SEL _cmd, NSURLSession *session, NSURLSessionTask *task, NSError *error) {
    @try {
        NSData *capturedData = hook_finishTaskBuffer(task);
        if (capturedData.length > 0 && !error) {
            NSURLRequest *request = [task respondsToSelector:NSSelectorFromString(@"currentRequest")] ?
                ((NSURLRequest * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"currentRequest")) :
                nil;
            NSURLResponse *response = [task respondsToSelector:NSSelectorFromString(@"response")] ?
                ((NSURLResponse * (*)(id, SEL))objc_msgSend)(task, NSSelectorFromString(@"response")) :
                nil;
            debug_writeNetworkResponse(request, response, capturedData);

            NSError *jsonError = nil;
            Class jsonClass = NSClassFromString(@"NSJSONSerialization");
            if (jsonClass) {
                SEL jsonSel = NSSelectorFromString(@"JSONObjectWithData:options:error:");
                id jsonObject = ((id (*)(Class, SEL, NSData *, NSInteger, NSError **))objc_msgSend)(jsonClass, jsonSel, capturedData, 1, &jsonError);
                if (jsonObject && !jsonError) {
                    NSString *url = request.URL.absoluteString ?: @"";
                    NSString *rawText = capture_jsonStringFromData(capturedData);
                    capture_handleObservedJSONObject(jsonObject, @"delegateDidComplete", url, rawText);
                }
            }
        }
    } @catch (NSException *exception) {
    }

    void (*originalIMP)(id, SEL, NSURLSession *, NSURLSessionTask *, NSError *) =
        (void (*)(id, SEL, NSURLSession *, NSURLSessionTask *, NSError *))hook_originalDelegateIMP(self, _cmd);
    if (originalIMP) {
        originalIMP(self, _cmd, session, task, error);
    }
}

static void hook_NSURLSessionTaskDelegate_didReceiveChallenge(id self, SEL _cmd, NSURLSession *session, NSURLSessionTask *task, NSURLAuthenticationChallenge *challenge, void (^completionHandler)(NSURLSessionAuthChallengeDisposition, NSURLCredential *)) {
    if (completionHandler && challenge) {
        @try {
            id protectionSpace = [challenge valueForKey:@"protectionSpace"];
            if (protectionSpace) {
                SecTrustRef serverTrust = (__bridge SecTrustRef)[protectionSpace valueForKey:@"serverTrust"];
                if (serverTrust) {
                    Class credentialClass = NSClassFromString(@"NSURLCredential");
                    if (credentialClass) {
                        SEL credentialSel = NSSelectorFromString(@"credentialForTrust:");
                        id credential = ((id (*)(Class, SEL, SecTrustRef))objc_msgSend)(credentialClass, credentialSel, serverTrust);
                        completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
                        return;
                    }
                }
            }
        } @catch (NSException *exception) {
        }
        completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    }
}

static void hook_NSURLSessionDelegate_didReceiveChallenge(id self, SEL _cmd, NSURLSession *session, NSURLAuthenticationChallenge *challenge, void (^completionHandler)(NSURLSessionAuthChallengeDisposition, NSURLCredential *)) {
    if (completionHandler && challenge) {
        @try {
            id protectionSpace = [challenge valueForKey:@"protectionSpace"];
            if (protectionSpace) {
                SecTrustRef serverTrust = (__bridge SecTrustRef)[protectionSpace valueForKey:@"serverTrust"];
                if (serverTrust) {
                    Class credentialClass = NSClassFromString(@"NSURLCredential");
                    if (credentialClass) {
                        SEL credentialSel = NSSelectorFromString(@"credentialForTrust:");
                        id credential = ((id (*)(Class, SEL, SecTrustRef))objc_msgSend)(credentialClass, credentialSel, serverTrust);
                        completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
                        return;
                    }
                }
            }
        } @catch (NSException *exception) {
        }
        completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    }
}

static void hook_installNSURLSessionDelegateHooks(void) {
    @try {
        if (!hookOriginalDelegateIMPsByKey) {
            hookOriginalDelegateIMPsByKey = [NSMutableDictionary dictionary];
        }

        unsigned int classCount = 0;
        Class *classes = objc_copyClassList(&classCount);
        for (unsigned int idx = 0; idx < classCount; idx++) {
            Class cls = classes[idx];
            if (!cls) {
                continue;
            }
            hook_installURLSessionDelegateClassHooks(cls);
        }

        if (classes) {
            free(classes);
        }
    } @catch (NSException *exception) {
    }
}

static void hook_installCoreHooks() {
    @try {
        Class jsonClass = NSClassFromString(@"NSJSONSerialization");
        if (jsonClass) {
            SEL jsonSel = NSSelectorFromString(@"JSONObjectWithData:options:error:");
            Method jsonMethod = class_getClassMethod(jsonClass, jsonSel);
            if (jsonMethod) {
                hookOriginalJSONObjectWithDataIMP = method_getImplementation(jsonMethod);
                method_setImplementation(jsonMethod, (IMP)hook_NSJSONSerialization_JSONObjectWithData);
            }
        }
        
        Class sessionClass = NSClassFromString(@"NSURLSession");
        if (sessionClass) {
            SEL sessionSel = NSSelectorFromString(@"dataTaskWithRequest:completionHandler:");
            Method sessionMethod = class_getInstanceMethod(sessionClass, sessionSel);
            if (sessionMethod) {
                hookOriginalDataTaskWithRequestIMP = method_getImplementation(sessionMethod);
                method_setImplementation(sessionMethod, (IMP)hook_NSURLSession_dataTaskWithRequest_completionHandler);
            }

            SEL sessionSelNoHandler = NSSelectorFromString(@"dataTaskWithRequest:");
            Method sessionMethodNoHandler = class_getInstanceMethod(sessionClass, sessionSelNoHandler);
            if (sessionMethodNoHandler) {
                hookOriginalDataTaskWithRequestNoHandlerIMP = method_getImplementation(sessionMethodNoHandler);
                method_setImplementation(sessionMethodNoHandler, (IMP)hook_NSURLSession_dataTaskWithRequest);
            }

            SEL sessionURLSel = NSSelectorFromString(@"dataTaskWithURL:completionHandler:");
            Method sessionURLMethod = class_getInstanceMethod(sessionClass, sessionURLSel);
            if (sessionURLMethod) {
                hookOriginalDataTaskWithURLIMP = method_getImplementation(sessionURLMethod);
                method_setImplementation(sessionURLMethod, (IMP)hook_NSURLSession_dataTaskWithURL_completionHandler);
            }

            SEL sessionURLSelNoHandler = NSSelectorFromString(@"dataTaskWithURL:");
            Method sessionURLMethodNoHandler = class_getInstanceMethod(sessionClass, sessionURLSelNoHandler);
            if (sessionURLMethodNoHandler) {
                hookOriginalDataTaskWithURLNoHandlerIMP = method_getImplementation(sessionURLMethodNoHandler);
                method_setImplementation(sessionURLMethodNoHandler, (IMP)hook_NSURLSession_dataTaskWithURL);
            }

            SEL uploadSel = NSSelectorFromString(@"uploadTaskWithRequest:fromData:");
            Method uploadMethod = class_getInstanceMethod(sessionClass, uploadSel);
            if (uploadMethod) {
                hookOriginalUploadTaskWithRequestFromDataIMP = method_getImplementation(uploadMethod);
                method_setImplementation(uploadMethod, (IMP)hook_NSURLSession_uploadTaskWithRequest_fromData);
            }

            SEL uploadCompletionSel = NSSelectorFromString(@"uploadTaskWithRequest:fromData:completionHandler:");
            Method uploadCompletionMethod = class_getInstanceMethod(sessionClass, uploadCompletionSel);
            if (uploadCompletionMethod) {
                hookOriginalUploadTaskWithRequestFromDataCompletionIMP = method_getImplementation(uploadCompletionMethod);
                method_setImplementation(uploadCompletionMethod, (IMP)hook_NSURLSession_uploadTaskWithRequest_fromData_completionHandler);
            }

            SEL resumeSel = NSSelectorFromString(@"resume");
            Method resumeMethod = class_getInstanceMethod(NSClassFromString(@"NSURLSessionTask"), resumeSel);
            if (resumeMethod) {
                hookOriginalTaskResumeIMP = method_getImplementation(resumeMethod);
                method_setImplementation(resumeMethod, (IMP)hook_NSURLSessionTask_resume);
            }

            SEL sessionFactorySel = NSSelectorFromString(@"sessionWithConfiguration:delegate:delegateQueue:");
            Method sessionFactoryMethod = class_getClassMethod(sessionClass, sessionFactorySel);
            if (sessionFactoryMethod) {
                hookOriginalSessionWithConfigurationDelegateQueueIMP = method_getImplementation(sessionFactoryMethod);
                method_setImplementation(sessionFactoryMethod, (IMP)hook_NSURLSession_sessionWithConfiguration_delegate_delegateQueue);
            }
        }
        
        // Hook NSMutableURLRequest 的 setHTTPBody: 方法，修改 enter_time
        Class mutableRequestClass = NSClassFromString(@"NSMutableURLRequest");
        if (mutableRequestClass) {
            SEL setHTTPBodySel = NSSelectorFromString(@"setHTTPBody:");
            Method setHTTPBodyMethod = class_getInstanceMethod(mutableRequestClass, setHTTPBodySel);
            if (setHTTPBodyMethod) {
                hookOriginalSetHTTPBodyIMP = method_getImplementation(setHTTPBodyMethod);
                method_setImplementation(setHTTPBodyMethod, (IMP)hook_NSMutableURLRequest_setHTTPBody);
            }
        }
        
        Class delegateClass = NSClassFromString(@"NSURLSessionTaskDelegate");
        if (delegateClass) {
            SEL delegateSel1 = NSSelectorFromString(@"URLSession:task:didReceiveChallenge:completionHandler:");
            Method delegateMethod1 = class_getInstanceMethod(delegateClass, delegateSel1);
            if (delegateMethod1) {
                method_setImplementation(delegateMethod1, (IMP)hook_NSURLSessionTaskDelegate_didReceiveChallenge);
            }
        }
        
        Class delegateClass2 = NSClassFromString(@"NSURLSessionDelegate");
        if (delegateClass2) {
            SEL delegateSel2 = NSSelectorFromString(@"URLSession:didReceiveChallenge:completionHandler:");
            Method delegateMethod2 = class_getInstanceMethod(delegateClass2, delegateSel2);
            if (delegateMethod2) {
                method_setImplementation(delegateMethod2, (IMP)hook_NSURLSessionDelegate_didReceiveChallenge);
            }
        }

        hook_installNSURLSessionDelegateHooks();
    } @catch (NSException *exception) {
    }
}

__attribute__((constructor))
static void tweak_initialize() {
    initializePDDStoragePaths();
    task_clearActiveTaskState();
    dispatch_async(dispatch_get_main_queue(), ^{
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            hook_installCoreHooks();

            if (!taskCommandPollTimer) {
                taskCommandPollTimer = [NSTimer scheduledTimerWithTimeInterval:2.0 target:[NSBlockOperation blockOperationWithBlock:^{
                    task_pollCommandInbox();
                }] selector:@selector(main) userInfo:nil repeats:YES];
            }
            task_writeCurrentStatus();
        });
    });
}
