static NSString *task_licenseProductCode(void) {
    return @"pdd_ios_agent";
}

static NSString *task_licenseEmbeddedPublicKey(void) {
    return @"-----BEGIN PUBLIC KEY-----\n"
           @"MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE0Q/269FjKwk3z2+RqNt221wJkwE0\n"
           @"O/iCbXIMwbOnprpzboEz1TGMRIa9YKneG40ypE/I1XIk+uGPbCjBVCaXaA==\n"
           @"-----END PUBLIC KEY-----";
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

static NSDateFormatter *task_licenseTimestampFormatter(void) {
    static NSDateFormatter *formatter = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        formatter = [NSDateFormatter new];
        formatter.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
        formatter.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:8 * 60 * 60];
        formatter.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    });
    return formatter;
}

static NSDate *task_licenseParseTimestamp(NSString *text) {
    NSString *value = [[NSString stringWithFormat:@"%@", text ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (value.length == 0) {
        return nil;
    }
    NSDateFormatter *formatter = task_licenseTimestampFormatter();
    @synchronized (formatter) {
        NSDate *date = [formatter dateFromString:value];
        if (!date) {
            return nil;
        }
        NSString *normalized = [formatter stringFromDate:date];
        if (![normalized isEqualToString:value]) {
            return nil;
        }
        return date;
    }
}

static NSString *task_licenseFormatTimestamp(NSDate *date) {
    if (![date isKindOfClass:[NSDate class]]) {
        return @"";
    }
    NSDateFormatter *formatter = task_licenseTimestampFormatter();
    @synchronized (formatter) {
        return [[formatter stringFromDate:date] copy];
    }
}

static NSArray<NSString *> *task_licenseFeatureList(id value) {
    if (![value isKindOfClass:[NSArray class]]) {
        return nil;
    }
    NSMutableArray<NSString *> *features = [NSMutableArray array];
    for (id item in (NSArray *)value) {
        if (![item isKindOfClass:[NSString class]]) {
            return nil;
        }
        NSString *name = [(NSString *)item stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (name.length == 0 || [features containsObject:name]) {
            continue;
        }
        [features addObject:name];
    }
    if (features.count == 0) {
        [features addObjectsFromArray:@[@"collect", @"watch", @"export", @"deploy", @"triage"]];
    }
    return features;
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

static NSData *task_licenseCanonicalPayloadData(NSDictionary *payload) {
    if (![payload isKindOfClass:[NSDictionary class]]) {
        return nil;
    }
    NSMutableDictionary *data = [payload mutableCopy];
    [data removeObjectForKey:@"signature"];
    NSError *error = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:data options:NSJSONWritingSortedKeys error:&error];
    if (!jsonData || error) {
        return nil;
    }
    return jsonData;
}

static NSData *task_licensePublicKeyData(void) {
    NSString *pemText = task_licenseEmbeddedPublicKey();
    NSMutableString *base64Text = [NSMutableString string];
    NSArray *lines = [pemText componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    for (NSString *line in lines) {
        NSString *trimmed = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (trimmed.length == 0 ||
            [trimmed hasPrefix:@"-----BEGIN"] ||
            [trimmed hasPrefix:@"-----END"]) {
            continue;
        }
        [base64Text appendString:trimmed];
    }
    NSData *decodedData = [[NSData alloc] initWithBase64EncodedString:base64Text options:0];
    if (!decodedData) {
        return nil;
    }

    const unsigned char *bytes = (const unsigned char *)decodedData.bytes;
    NSUInteger length = decodedData.length;
    if (length == 65 && bytes[0] == 0x04) {
        return decodedData;
    }

    // OpenSSL PEM public keys are emitted as SubjectPublicKeyInfo DER.
    // Security.framework public-key import on this target expects the raw
    // uncompressed EC point, so extract it from the BIT STRING wrapper.
    for (NSUInteger index = 0; index + 68 <= length; index++) {
        if (bytes[index] == 0x03 &&
            bytes[index + 1] == 0x42 &&
            bytes[index + 2] == 0x00 &&
            bytes[index + 3] == 0x04) {
            return [decodedData subdataWithRange:NSMakeRange(index + 3, 65)];
        }
    }
    return decodedData;
}

static BOOL task_licenseVerifySignature(NSDictionary *payload, NSString **detailOut) {
    NSString *signatureBase64 = [[NSString stringWithFormat:@"%@", payload[@"signature"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (signatureBase64.length == 0) {
        if (detailOut) {
            *detailOut = @"missing signature";
        }
        return NO;
    }

    NSData *signatureData = [[NSData alloc] initWithBase64EncodedString:signatureBase64 options:0];
    if (!signatureData) {
        if (detailOut) {
            *detailOut = @"signature must be valid base64";
        }
        return NO;
    }

    NSData *payloadData = task_licenseCanonicalPayloadData(payload);
    NSData *publicKeyData = task_licensePublicKeyData();
    if (!payloadData || !publicKeyData) {
        if (detailOut) {
            *detailOut = @"unable to prepare license verification payload";
        }
        return NO;
    }

    NSDictionary *attributes = @{
        (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
        (__bridge id)kSecAttrKeyClass: (__bridge id)kSecAttrKeyClassPublic,
        (__bridge id)kSecAttrKeySizeInBits: @256,
    };
    CFErrorRef keyError = NULL;
    SecKeyRef publicKey = SecKeyCreateWithData((__bridge CFDataRef)publicKeyData, (__bridge CFDictionaryRef)attributes, &keyError);
    if (!publicKey) {
        if (detailOut) {
            NSError *error = CFBridgingRelease(keyError);
            *detailOut = error.localizedDescription.length > 0 ? error.localizedDescription : @"unable to create public key";
        } else if (keyError) {
            CFRelease(keyError);
        }
        return NO;
    }

    CFErrorRef verifyError = NULL;
    BOOL verified = SecKeyVerifySignature(
        publicKey,
        kSecKeyAlgorithmECDSASignatureMessageX962SHA256,
        (__bridge CFDataRef)payloadData,
        (__bridge CFDataRef)signatureData,
        &verifyError
    );
    if (!verified && detailOut) {
        NSError *error = CFBridgingRelease(verifyError);
        *detailOut = error.localizedDescription.length > 0 ? error.localizedDescription : @"signature verification failed";
    } else if (verifyError) {
        CFRelease(verifyError);
    }
    CFRelease(publicKey);
    return verified;
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

    NSFileManager *fileManager = [NSFileManager defaultManager];
    BOOL licenseFileExists = taskLicenseFilePath.length > 0 && [fileManager fileExistsAtPath:taskLicenseFilePath];
    if (!licenseFileExists) {
        status[@"installed"] = @NO;
        status[@"valid"] = @NO;
        status[@"status"] = @"missing";
        status[@"error_code"] = @"LICENSE_REQUIRED";
        status[@"error_message"] = @"license is required";
        status[@"detail"] = @"run license install to activate this tool";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSDictionary *payload = task_readJSON(taskLicenseFilePath);
    if (![payload isKindOfClass:[NSDictionary class]]) {
        status[@"installed"] = @YES;
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"license.json is missing or is not valid JSON";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    status[@"installed"] = @YES;
    for (NSString *fieldName in @[
        @"schema_version",
        @"signature_algorithm",
        @"product_code",
        @"license_id",
        @"customer_name",
        @"edition",
        @"notes",
        @"issued_at",
        @"expires_at",
        @"bound_binding_id",
        @"bound_device_name",
        @"bound_product_type",
        @"bound_os_version",
        @"binding_source",
    ]) {
        id value = payload[fieldName];
        if (value != nil) {
            status[fieldName] = value;
        }
    }

    if (![payload[@"schema_version"] isKindOfClass:[NSNumber class]] ||
        [payload[@"schema_version"] integerValue] != 2) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"unsupported schema_version";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSString *signatureAlgorithm = [[NSString stringWithFormat:@"%@", payload[@"signature_algorithm"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![signatureAlgorithm isEqualToString:@"p256-sha256"]) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"unsupported signature_algorithm";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSString *productCode = [[NSString stringWithFormat:@"%@", payload[@"product_code"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (![productCode isEqualToString:task_licenseProductCode()]) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = [NSString stringWithFormat:@"product_code mismatch: %@", productCode.length > 0 ? productCode : @"missing"];
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSString *signatureDetail = nil;
    if (!task_licenseVerifySignature(payload, &signatureDetail)) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = signatureDetail.length > 0 ? signatureDetail : @"license signature verification failed";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSDate *issuedAt = task_licenseParseTimestamp(payload[@"issued_at"]);
    NSDate *expiresAt = task_licenseParseTimestamp(payload[@"expires_at"]);
    if (!issuedAt || !expiresAt) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"issued_at or expires_at must use YYYY-MM-DD HH:mm:ss in UTC+8";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    if ([expiresAt compare:issuedAt] == NSOrderedAscending) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"expires_at must be greater than or equal to issued_at";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSString *boundBindingId = [[NSString stringWithFormat:@"%@", payload[@"bound_binding_id"] ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (boundBindingId.length == 0) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"missing bound_binding_id";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSTimeInterval skew = 300.0;
    NSDate *now = [NSDate date];
    if ([now timeIntervalSinceDate:issuedAt] < -skew) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"local clock is earlier than issued_at";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    if ([now timeIntervalSinceDate:expiresAt] > skew) {
        status[@"valid"] = @NO;
        status[@"status"] = @"expired";
        status[@"error_code"] = @"LICENSE_EXPIRED";
        status[@"error_message"] = @"license has expired";
        status[@"detail"] = task_licenseFormatTimestamp(expiresAt);
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    if (currentBindingId.length == 0) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"current device binding is unavailable; run license fingerprint again";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    if (![currentBindingId isEqualToString:boundBindingId]) {
        status[@"valid"] = @NO;
        status[@"status"] = @"binding_mismatch";
        status[@"error_code"] = @"LICENSE_BINDING_MISMATCH";
        status[@"error_message"] = @"license binding does not match this device";
        status[@"detail"] = [NSString stringWithFormat:@"expected %@ but got %@", boundBindingId, currentBindingId];
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    NSArray<NSString *> *features = task_licenseFeatureList(payload[@"features"]);
    if (!features) {
        status[@"valid"] = @NO;
        status[@"status"] = @"invalid";
        status[@"error_code"] = @"LICENSE_INVALID";
        status[@"error_message"] = @"invalid license file";
        status[@"detail"] = @"features must be an array of strings";
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }
    status[@"features"] = features;

    NSString *required = [[NSString stringWithFormat:@"%@", requiredFeature ?: @""]
        stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (required.length > 0 &&
        ![features containsObject:@"*"] &&
        ![features containsObject:required]) {
        status[@"valid"] = @NO;
        status[@"status"] = @"feature_denied";
        status[@"error_code"] = @"LICENSE_FEATURE_DENIED";
        status[@"error_message"] = @"license does not allow this command";
        status[@"detail"] = [NSString stringWithFormat:@"feature=%@", required];
        status[@"required_feature"] = required;
        status[@"ui_title"] = task_licenseTitleForStatus(status);
        status[@"ui_message"] = task_licenseMessageForStatus(status);
        return status;
    }

    status[@"valid"] = @YES;
    status[@"status"] = @"active";
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
