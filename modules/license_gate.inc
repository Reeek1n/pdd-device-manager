static NSString *task_licenseProductCode(void) {
    return @"pdd_ios_agent";
}

static NSString *task_licenseStatusValue(NSDictionary *status) {
    NSString *value = [[NSString stringWithFormat:@"%@", status[@"status"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return value.length > 0 ? value : @"unknown";
}

static NSString *task_licenseTitleForStatus(NSDictionary *status) {
    NSString *statusValue = task_licenseStatusValue(status);
    if ([statusValue isEqualToString:@"active"]) {
        return @"";
    }
    return @"拼多多采集许可证";
}

static NSString *task_licenseMessageForStatus(NSDictionary *status) {
    NSString *statusValue = task_licenseStatusValue(status);
    if ([statusValue isEqualToString:@"active"]) {
        return @"";
    }
    return @"无效，请重新授权";
}

static NSString *task_licensePresentationKey(NSDictionary *status) {
    NSString *statusValue = task_licenseStatusValue(status);
    NSString *errorCode = [[NSString stringWithFormat:@"%@", status[@"error_code"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *expiresAt = [[NSString stringWithFormat:@"%@", status[@"expires_at"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *boundBindingId = [[NSString stringWithFormat:@"%@", status[@"bound_binding_id"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    NSString *currentBindingId = [[NSString stringWithFormat:@"%@", status[@"current_binding_id"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    return [NSString stringWithFormat:@"%@|%@|%@|%@|%@", statusValue, errorCode, expiresAt, boundBindingId, currentBindingId];
}

static NSString *task_licenseTaskErrorForStatus(NSDictionary *status) {
    NSString *errorCode = [[NSString stringWithFormat:@"%@", status[@"error_code"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if ([errorCode isEqualToString:@"LICENSE_REQUIRED"]) {
        return @"license_required";
    }
    if ([errorCode isEqualToString:@"LICENSE_EXPIRED"]) {
        return @"license_expired";
    }
    if ([errorCode isEqualToString:@"LICENSE_BINDING_MISMATCH"]) {
        return @"license_binding_mismatch";
    }
    if ([errorCode isEqualToString:@"LICENSE_FEATURE_DENIED"]) {
        return @"license_feature_denied";
    }
    return @"license_invalid";
}

static NSString *task_sha256HexForText(NSString *value) {
    NSData *data = [[value ?: @"" description] dataUsingEncoding:NSUTF8StringEncoding];
    if (!data) {
        return @"";
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes, (CC_LONG)data.length, digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:CC_SHA256_DIGEST_LENGTH * 2];
    for (NSInteger index = 0; index < CC_SHA256_DIGEST_LENGTH; index++) {
        [hex appendFormat:@"%02x", digest[index]];
    }
    return hex;
}

static NSString *task_currentBindingId(void) {
    if (taskLicenseBindingFilePath.length == 0) {
        return @"";
    }
    NSError *error = nil;
    NSString *stableId = [NSString stringWithContentsOfFile:taskLicenseBindingFilePath encoding:NSUTF8StringEncoding error:&error];
    if (error || stableId.length == 0) {
        return @"";
    }
    stableId = [stableId stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (stableId.length == 0) {
        return @"";
    }
    NSString *input = [NSString stringWithFormat:@"%@:%@", task_licenseProductCode(), stableId];
    return [NSString stringWithFormat:@"sha256:%@", task_sha256HexForText(input)];
}

static NSDictionary *task_evaluateLicenseStatus(NSString *requiredFeature) {
    initializePDDStoragePaths();
    task_ensureCommandDirectories();

    NSMutableDictionary *status = [NSMutableDictionary dictionary];
    status[@"updated_at"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    status[@"product_code"] = task_licenseProductCode();
    status[@"installed_path"] = taskLicenseFilePath ?: @"";
    status[@"status_path"] = taskLicenseStatusPath ?: @"";

    NSString *currentBindingId = task_currentBindingId();
    if (currentBindingId.length > 0) {
        status[@"current_binding_id"] = currentBindingId;
    }

    status[@"valid"] = @YES;
    status[@"status"] = @"active";
    status[@"features"] = @[@"*"];
    status[@"ui_title"] = @"";
    status[@"ui_message"] = @"";
    return status;
}

static void task_writeLicenseStatus(NSDictionary *status) {
    if (taskLicenseStatusPath.length == 0 || !status) {
        return;
    }
    task_writeJSON(status, taskLicenseStatusPath);
}

static id ui_licenseKeyWindow(void) {
    @try {
        Class UIApplicationClass = NSClassFromString(@"UIApplication");
        if (!UIApplicationClass) {
            return nil;
        }
        id app = ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication"));
        if (!app) {
            return nil;
        }

        SEL keyWindowSel = NSSelectorFromString(@"keyWindow");
        if ([app respondsToSelector:keyWindowSel]) {
            id keyWindow = ((id (*)(id, SEL))objc_msgSend)(app, keyWindowSel);
            if (keyWindow) {
                return keyWindow;
            }
        }

        SEL windowsSel = NSSelectorFromString(@"windows");
        if ([app respondsToSelector:windowsSel]) {
            NSArray *windows = ((NSArray * (*)(id, SEL))objc_msgSend)(app, windowsSel);
            for (id window in windows) {
                if (window) {
                    return window;
                }
            }
        }
    } @catch (NSException *exception) {
    }
    return nil;
}

static id ui_licenseTopViewController(void) {
    id controller = ui_currentRootViewController();
    @try {
        while (controller) {
            SEL presentedSel = NSSelectorFromString(@"presentedViewController");
            if (![controller respondsToSelector:presentedSel]) {
                break;
            }
            id presented = ((id (*)(id, SEL))objc_msgSend)(controller, presentedSel);
            if (!presented) {
                break;
            }
            controller = presented;
        }
    } @catch (NSException *exception) {
    }
    return controller;
}

static void ui_dismissLicensePresentation(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            if (taskLicenseOverlayView && [taskLicenseOverlayView respondsToSelector:@selector(removeFromSuperview)]) {
                ((void (*)(id, SEL))objc_msgSend)(taskLicenseOverlayView, @selector(removeFromSuperview));
            }
        } @catch (NSException *exception) {
        }
        taskLicenseOverlayView = nil;

        dispatch_async(dispatch_get_main_queue(), ^{
            @try {
                if (taskLicenseAlertController) {
                    SEL dismissSel = NSSelectorFromString(@"dismissViewControllerAnimated:completion:");
                    if ([taskLicenseAlertController respondsToSelector:dismissSel]) {
                        ((void (*)(id, SEL, BOOL, id))objc_msgSend)(taskLicenseAlertController, dismissSel, YES, nil);
                    }
                }
            } @catch (NSException *exception) {
            }
            taskLicenseAlertController = nil;
        });
    });
}

@interface PDDLicenseOverlayDismissTarget : NSObject
+ (instancetype)sharedTarget;
- (void)closeOverlay:(id)sender;
@end

@implementation PDDLicenseOverlayDismissTarget
+ (instancetype)sharedTarget {
    static PDDLicenseOverlayDismissTarget *sharedInstance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedInstance = [PDDLicenseOverlayDismissTarget new];
    });
    return sharedInstance;
}

- (void)closeOverlay:(__unused id)sender {
    ui_dismissLicensePresentation();
}
@end

static void ui_showLicenseOverlay(NSDictionary *status) {
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            id window = ui_licenseKeyWindow();
            if (!window) {
                return;
            }
            if (taskLicenseOverlayView && [taskLicenseOverlayView respondsToSelector:@selector(removeFromSuperview)]) {
                ((void (*)(id, SEL))objc_msgSend)(taskLicenseOverlayView, @selector(removeFromSuperview));
                taskLicenseOverlayView = nil;
            }

            CGRect bounds = CGRectZero;
            SEL boundsSel = NSSelectorFromString(@"bounds");
            if ([window respondsToSelector:boundsSel]) {
                bounds = ((CGRect (*)(id, SEL))objc_msgSend)(window, boundsSel);
            }
            if (CGRectIsEmpty(bounds)) {
                bounds = CGRectMake(0, 0, 320, 568);
            }

            UIView *overlay = [[UIView alloc] initWithFrame:bounds];
            overlay.backgroundColor = [[UIColor blackColor] colorWithAlphaComponent:0.12];
            overlay.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

            UIButton *backgroundDismissButton = [UIButton buttonWithType:UIButtonTypeCustom];
            backgroundDismissButton.frame = overlay.bounds;
            backgroundDismissButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [backgroundDismissButton addTarget:[PDDLicenseOverlayDismissTarget sharedTarget]
                                        action:@selector(closeOverlay:)
                              forControlEvents:UIControlEventTouchUpInside];
            [overlay addSubview:backgroundDismissButton];

            CGFloat cardWidth = MIN(MAX(bounds.size.width - 48.0, 240.0), 320.0);
            CGFloat cardHeight = 148.0;
            UIView *container = [[UIView alloc] initWithFrame:CGRectMake((bounds.size.width - cardWidth) / 2.0,
                                                                         (bounds.size.height - cardHeight) / 2.0,
                                                                         cardWidth,
                                                                         cardHeight)];
            container.backgroundColor = [[UIColor whiteColor] colorWithAlphaComponent:0.78];
            container.layer.cornerRadius = 18.0;
            container.layer.masksToBounds = YES;
            container.layer.borderWidth = 0.5;
            container.layer.borderColor = [[UIColor colorWithWhite:1.0 alpha:0.35] CGColor];
            container.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin |
                                         UIViewAutoresizingFlexibleRightMargin |
                                         UIViewAutoresizingFlexibleTopMargin |
                                         UIViewAutoresizingFlexibleBottomMargin;

            UIButton *cardDismissButton = [UIButton buttonWithType:UIButtonTypeCustom];
            cardDismissButton.frame = container.bounds;
            cardDismissButton.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [cardDismissButton addTarget:[PDDLicenseOverlayDismissTarget sharedTarget]
                                  action:@selector(closeOverlay:)
                        forControlEvents:UIControlEventTouchUpInside];
            [container addSubview:cardDismissButton];

            UILabel *titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 30, cardWidth - 40, 28)];
            titleLabel.textAlignment = NSTextAlignmentCenter;
            titleLabel.textColor = [UIColor colorWithRed:0.10 green:0.11 blue:0.12 alpha:1.0];
            titleLabel.font = [UIFont boldSystemFontOfSize:18.0];
            titleLabel.text = task_licenseTitleForStatus(status);
            [container addSubview:titleLabel];

            UILabel *messageLabel = [[UILabel alloc] initWithFrame:CGRectMake(20, 72, cardWidth - 40, 28)];
            messageLabel.numberOfLines = 0;
            messageLabel.textAlignment = NSTextAlignmentCenter;
            messageLabel.textColor = [UIColor colorWithRed:0.32 green:0.34 blue:0.36 alpha:1.0];
            messageLabel.font = [UIFont systemFontOfSize:15.0 weight:UIFontWeightMedium];
            messageLabel.text = task_licenseMessageForStatus(status);
            [container addSubview:messageLabel];

            [overlay addSubview:container];

            SEL addSubviewSel = NSSelectorFromString(@"addSubview:");
            if ([window respondsToSelector:addSubviewSel]) {
                ((void (*)(id, SEL, id))objc_msgSend)(window, addSubviewSel, overlay);
                taskLicenseOverlayView = overlay;
            }
        } @catch (NSException *exception) {
        }
    });
}

__attribute__((unused)) static void ui_showLicenseAlert(NSDictionary *status) {
    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            id presenter = ui_licenseTopViewController();
            Class UIAlertControllerClass = NSClassFromString(@"UIAlertController");
            if (!presenter || !UIAlertControllerClass) {
                ui_showLicenseOverlay(status);
                return;
            }

            if (taskLicenseAlertController) {
                return;
            }

            id alert = ((id (*)(id, SEL, id, id, NSInteger))objc_msgSend)(
                UIAlertControllerClass,
                NSSelectorFromString(@"alertControllerWithTitle:message:preferredStyle:"),
                task_licenseTitleForStatus(status),
                task_licenseMessageForStatus(status),
                1
            );
            Class UIAlertActionClass = NSClassFromString(@"UIAlertAction");
            if (UIAlertActionClass && alert) {
                id action = ((id (*)(id, SEL, id, NSInteger, id))objc_msgSend)(
                    UIAlertActionClass,
                    NSSelectorFromString(@"actionWithTitle:style:handler:"),
                    @"知道了",
                    0,
                    ^(__unused id actionObject) {
                        taskLicenseAlertController = nil;
                    }
                );
                if (action && [alert respondsToSelector:NSSelectorFromString(@"addAction:")]) {
                    ((void (*)(id, SEL, id))objc_msgSend)(alert, NSSelectorFromString(@"addAction:"), action);
                }
            }

            SEL presentSel = NSSelectorFromString(@"presentViewController:animated:completion:");
            if (alert && [presenter respondsToSelector:presentSel]) {
                taskLicenseAlertController = alert;
                ((void (*)(id, SEL, id, BOOL, id))objc_msgSend)(presenter, presentSel, alert, YES, nil);
                return;
            }
        } @catch (NSException *exception) {
        }

        ui_showLicenseOverlay(status);
    });
}

static void ui_updateLicensePresentation(NSDictionary *status, BOOL forcePresentation) {
    if (![status isKindOfClass:[NSDictionary class]]) {
        return;
    }

    dispatch_async(dispatch_get_main_queue(), ^{
        @try {
            Class UIApplicationClass = NSClassFromString(@"UIApplication");
            id app = UIApplicationClass ? ((id (*)(Class, SEL))objc_msgSend)(UIApplicationClass, NSSelectorFromString(@"sharedApplication")) : nil;
            if (app && [app respondsToSelector:NSSelectorFromString(@"applicationState")]) {
                NSInteger applicationState = ((NSInteger (*)(id, SEL))objc_msgSend)(app, NSSelectorFromString(@"applicationState"));
                if (applicationState != 0) {
                    return;
                }
            }
        } @catch (NSException *exception) {
        }

        NSString *statusValue = task_licenseStatusValue(status);
        if ([statusValue isEqualToString:@"active"]) {
            taskLastPresentedLicenseStatusKey = nil;
            ui_dismissLicensePresentation();
            return;
        }

        if (!forcePresentation) {
            return;
        }

        NSString *presentationKey = task_licensePresentationKey(status);
        if (!forcePresentation && [taskLastPresentedLicenseStatusKey isEqualToString:presentationKey]) {
            return;
        }
        taskLastPresentedLicenseStatusKey = presentationKey;
        ui_showLicenseOverlay(status);
    });
}

static NSDictionary *task_refreshLicenseStatus(NSString *requiredFeature, BOOL forceUI) {
    NSDictionary *status = task_evaluateLicenseStatus(requiredFeature);
    taskLastLicenseStatus = status;
    task_writeLicenseStatus(status);
    ui_updateLicensePresentation(status, forceUI);
    return status;
}
