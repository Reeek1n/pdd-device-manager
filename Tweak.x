#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CommonCrypto/CommonDigest.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <Security/Security.h>
#import <sys/types.h>

static NSString *taskDataRootDir = nil;
static NSInteger captureObservedPayloadSequence = 0;
static BOOL tweakIsEnabled = YES; // 插件启用状态，默认启用

// 自动搜索相关变量
static BOOL taskAutoSearchActive = NO; // 是否正在自动搜索
static NSInteger taskCurrentProductIndex = 0; // 当前商品索引
static NSTimer *taskAutoSearchTimer = nil; // 自动搜索定时器
static NSTimer *taskCommandPollTimer = nil; // 任务轮询定时器
static NSTimeInterval const kTaskCandidateTTLSeconds = 90.0;
static NSUInteger const kTaskCandidateLimit = 8;
static NSInteger const kTaskMaxNoNewScrollRounds = 5;
static NSInteger const kTaskMaxConsecutiveUnknownOrClickFailures = 12;
static NSTimeInterval const kTaskSearchTransitionFirstPollDelaySeconds = 0.35;
static NSTimeInterval const kTaskSearchTransitionPollDelaySeconds = 0.25;
static NSTimeInterval const kTaskSearchTransitionSettleDelaySeconds = 0.3;
static NSTimeInterval const kTaskSearchTransitionPostSubmitDelaySeconds = 0.8;
static NSTimeInterval const kTaskSearchTransitionTimeoutSeconds = 2.0;

// 命令与任务目录
static NSString *taskCommandRootDir = nil;
static NSString *taskCommandInboxDir = nil;
static NSString *taskCommandStatusDir = nil;
static NSString *taskCommandOutboxDir = nil;
static NSString *taskCommandStopDir = nil;
static NSString *taskRootDir = nil;
static NSString *taskLicenseDir = nil;
static NSString *taskLicenseFilePath = nil;
static NSString *taskLicenseStatusPath = nil;
static NSString *taskLicenseBindingFilePath = nil;

// 当前任务状态
static NSString *taskCurrentId = nil;
static NSString *taskCurrentKeyword = nil;
static NSString *taskCurrentDir = nil;
static NSString *taskCurrentRawDir = nil;
static NSString *taskCurrentState = nil;
static NSString *taskCurrentError = nil;
static NSString *taskLastGoodsId = nil;
static NSInteger taskTargetCount = 0;
static NSInteger taskSavedCount = 0;
static NSInteger taskAttemptedCount = 0;
static NSInteger taskFailedCount = 0;
static NSInteger taskConsecutiveFailureCount = 0;
static NSInteger taskConsecutiveNoNewRounds = 0;
static NSInteger taskSavedCountAtLastScroll = 0;
static NSInteger taskDuplicateSkipCount = 0;
static NSInteger taskUnknownEntryCount = 0;
static NSInteger taskConsecutiveUnknownOrClickFailures = 0;
static NSInteger taskConsecutiveSameSearchPageCount = 0;
static NSInteger taskSearchRecoveryAttemptCount = 0;
static NSInteger taskSearchTransitionRetryCount = 0;
static NSString *taskRequestedSortBy = nil;
static NSString *taskRequestedPriceMin = nil;
static NSString *taskRequestedPriceMax = nil;
static NSInteger taskRequestedReviewMin = 0;
static BOOL taskSalesSortApplied = NO;
static BOOL taskPriceFilterApplied = NO;
static NSString *taskFilterWarning = nil;
static BOOL taskSalesSortAttempted = NO;
static BOOL taskSalesSortPendingConfirmation = NO;
static BOOL taskSalesSortObservedListReload = NO;
static BOOL taskPriceFilterPanelOpened = NO;
static BOOL taskPriceFilterInputsFilled = NO;
static BOOL taskPriceFilterPendingConfirmation = NO;
static BOOL taskPriceFilterObservedListReload = NO;
static BOOL taskPriceFilterAttempted = NO;
static NSInteger taskPriceFilterApplyAttemptCount = 0;
static NSInteger taskPriceFilterConfirmAttemptCount = 0;
static NSTimeInterval taskSearchFilterSettleUntil = 0;
static NSTimeInterval taskSalesSortRequestedAt = 0;
static NSTimeInterval taskPriceFilterConfirmedAt = 0;
static NSTimeInterval taskStartedAt = 0;
static NSTimeInterval taskDetailNavigationStartedAt = 0;
static NSTimeInterval taskObservedDetailSignalAt = 0;
static NSTimeInterval taskSearchResultsPendingStartedAt = 0;
static NSInteger taskSalesSortBaselineVisibleCount = 0;
static NSInteger taskPriceFilterBaselineVisibleCount = 0;
static NSInteger taskSavedCountBeforeDetailNavigation = 0;
static NSInteger taskCandidateDebugCount = 0;
static NSInteger taskNetworkDebugCount = 0;
static NSInteger taskRequestDebugCount = 0;
static NSInteger taskLifecycleDebugCount = 0;
static BOOL taskSearchSubmitted = NO;
static BOOL taskAwaitingSearchPage = NO;
static NSTimeInterval taskSearchTransitionStartedAt = 0;
static BOOL taskSearchTransitionSubmitAttempted = NO;
static BOOL taskNeedsBackNavigation = NO;
static NSInteger taskDebugSequence = 0;
static NSInteger taskDetailCollectionStage = 0;
static NSTimeInterval taskDetailCollectionLastActionAt = 0;
static BOOL taskDetailAttemptCounted = NO;
static BOOL taskLastDetailCandidateWasDuplicate = NO;
static NSString *taskStopReasonDetail = nil;
static NSString *taskLastSearchPageSignature = nil;
static NSString *taskSalesSortBaselineSignature = nil;
static NSString *taskPriceFilterBaselineSignature = nil;
static NSMutableSet *taskSeenGoodsIds = nil;
static NSMutableArray *taskSavedFiles = nil;
static NSMutableDictionary *taskSearchPayloadByGoodsId = nil;
static NSMutableArray *taskRecentCandidates = nil;
static id taskCaptureToastView = nil;
static NSDictionary *taskLastLicenseStatus = nil;
static NSString *taskLastPresentedLicenseStatusKey = nil;
static id taskLicenseOverlayView = nil;
static id taskLicenseAlertController = nil;

static void task_ensureCommandDirectories(void);
static BOOL task_writeJSON(id jsonObject, NSString *path);
static NSDictionary *task_readJSON(NSString *path);
static NSDictionary *task_buildCurrentStatus(void);
static void task_writeCurrentStatus(void);
static NSDictionary *task_refreshLicenseStatus(NSString *requiredFeature, BOOL forceUI);
static NSString *task_licenseTaskErrorForStatus(NSDictionary *status);
static void task_clearActiveTaskState(void);
static BOOL task_hasActiveTask(void);
static NSString *task_stopFlagPath(NSString *taskId);
static BOOL task_shouldStopCurrentTask(void);
static void task_finishActiveTask(NSString *state, NSString *errorMessage);
static void task_startCollectTask(NSDictionary *taskInfo);
static void task_pollCommandInbox(void);
static BOOL ui_scrollVisibleScrollableView(id view);
static BOOL ui_scrollSearchResultsPage(void);
static BOOL task_writeTaskMetadata(NSDictionary *taskInfo);
static NSString *task_filePath(NSString *fileName);
static void task_writeCurrentTaskStatus(void);
static void task_appendFilterWarning(NSString *warning);
static BOOL ui_viewIsVisible(id view);
static CGRect ui_viewFrameInScreen(id view);
static NSString *ui_viewMetadataText(id view);
static BOOL ui_stringLooksLikeSearch(NSString *value);
static id ui_currentRootView(void);
static BOOL ui_submitSearchInView(id searchView, NSString *keyword);
static BOOL ui_trySubmitSearchFromRootView(id rootView, NSString *keyword);
static BOOL ui_invokeGestureTap(id view);
static BOOL ui_sendTapActionToView(id view);
static id ui_bestSearchEntryView(id rootView);
static NSArray *ui_sortedVisibleCellsInListView(id listView);
static NSArray *ui_sortedVisibleCellLikeViews(id rootView);
static NSArray *ui_visibleSearchCells(id rootView);
static id ui_bestResultsListView(id rootView);
static BOOL ui_selectProductFromListView(id listView, NSInteger index);
static BOOL ui_tapVisibleCellLikeView(id rootView, NSInteger index);
static NSString *task_resolvedGoodsIdForSearchCell(id cell);
static NSString *task_currentSearchPageSignature(id rootView);
static NSInteger task_detailEntryBudget(void);
static BOOL task_normalizedTextContainsNeedlePrefix(NSString *normalizedHaystack, NSString *needle);
static BOOL task_shouldSkipUnresolvedSearchCell(id cell, NSString **reasonOut);
static BOOL ui_tryTapBackButton(id rootView);
static BOOL ui_tapAtPoint(CGPoint point);
static BOOL ui_tryTapProductAtIndexByPoint(NSInteger index);
static void debug_writeTaskDump(NSString *category, NSString *detail);
static void debug_appendViewHierarchy(NSMutableString *output, id view, NSInteger depth, NSInteger maxDepth);
static NSString *debug_hitTestDescription(CGPoint point);
static NSString *debug_searchHitTestText(void);
static NSString *debug_productHitTestText(NSInteger index);
static void ui_collectDescendantViews(id view, NSMutableArray *results);
static NSArray *ui_allDescendantViews(id view);
static id ui_bestEditableSearchView(id searchView);
static BOOL ui_tryActivateSearchEntry(id searchEntry);
static NSString *debug_searchEntryText(id searchEntry);
static void debug_appendInterestingView(NSMutableString *output, id view, NSString *label);
static id ui_resolveDirectTextInputView(id view);
static NSString *ui_viewSummaryText(id view);
static BOOL ui_rootViewLooksLikeHomePage(id rootView);
static BOOL ui_viewHasAncestorClassFragment(id view, NSString *classFragment);
static BOOL ui_appendUniqueActivationTarget(NSMutableArray *targets, NSMutableSet *seen, id candidate);
static BOOL ui_viewLooksSearchActionable(id target);
static id ui_nearestActionableAncestor(id source);
static void ui_collectSearchActivationTargets(id source, id searchEntry, NSMutableArray *targets, NSMutableSet *seen);
static BOOL ui_tryInvokeZeroArgumentSelectors(id target, NSArray *selectorNames, NSString **usedSelectorNameOut);
static BOOL ui_tryInvokeObjectArgumentSelectors(id target, NSArray *selectorNames, id argument, NSString **usedSelectorNameOut);
static BOOL ui_tryInvokeSearchActivationOnTarget(id target, id searchEntry);
static BOOL ui_isViewLikeActivationTarget(id target);
static BOOL ui_stringLooksLikeRiskControl(NSString *value);
static NSString *ui_findRiskControlMessageInView(id rootView);
static BOOL task_failIfRiskControlDetected(NSString *context);
static BOOL ui_tryApplySalesSortOnSearchPage(id rootView, NSString **traceOut);
static BOOL ui_tryOpenPriceFilterPanelOnSearchPage(id rootView, NSString **traceOut);
static BOOL ui_searchPageHasOpenPriceFilterPanel(id rootView);
static BOOL ui_tryFillPriceFilterInputsOnSearchPage(id rootView, NSString *priceMin, NSString *priceMax, NSString **traceOut, NSString **warningOut);
static BOOL ui_tryConfirmPriceFilterOnSearchPage(id rootView, NSString **traceOut, NSString **warningOut);
static BOOL ui_tryTapViewWithFallback(id view);
static BOOL ui_tryTapSearchToolbarFallbackAtSlot(id rootView, NSInteger slotIndex, NSString **traceOut);
static id ui_bestSearchToolbarButtonWithKeywords(id rootView, NSArray<NSString *> *keywords, BOOL preferRightSide);
static id ui_nearestAncestorWithClassFragment(id view, NSString *classFragment);
static BOOL ui_searchToolbarItemLooksSelected(id view, NSString **traceOut);
static NSString *debug_searchFilterProbeText(id rootView);
static NSString *debug_searchPriceFilterPanelProbeText(id rootView);
static BOOL ui_tryActivateSearchToolbarItemView(id view, NSString **traceOut);
static NSArray *ui_candidateRootViewsForInspection(id primaryRootView);
static NSString *json_stringFromObject(id jsonObject);
static BOOL task_isWithinRecentDetailWindow(void);
static NSString *json_topLevelKeysText(id jsonObject);
static void debug_writeCandidatePayload(id jsonObject, NSString *source, NSString *url, NSString *goodsId);
static void debug_writeNetworkResponse(NSURLRequest *request, NSURLResponse *response, NSData *data);
static void debug_writeRequest(NSString *source, NSURLRequest *request, NSData *bodyData);
static void debug_writeTaskLifecycle(NSString *event, NSURLSessionTask *task, NSURLRequest *request, NSURLResponse *response, NSData *bodyData, id session, id delegateObject);
static void hook_installURLSessionDelegateClassHooks(Class cls);
static NSString *ui_currentTextValue(id view);
static BOOL ui_rootViewLooksLikeSearchPage(id rootView);
static BOOL capture_payloadLooksLikeDetailPageSignal(id jsonObject, NSString *url);
static BOOL ui_rootViewHasReadySearchResults(id rootView);
static void task_clearPendingDetailNavigation(void);
static CGRect ui_currentInteractiveFrame(void);
static void ui_forceGoBack(void);
static id task_sharedStateLock(void);
static NSDictionary *task_searchPayloadSnapshot(void);
static NSSet *task_seenGoodsIdsSnapshot(void);
static NSInteger ui_visibleSearchResultCount(id rootView);
static BOOL ui_rootViewLooksLikeGoodsDetailPage(id rootView);
static BOOL capture_trySaveGoodsDetailFromView(id rootView);
static void capture_cacheSearchSnapshot(id jsonObject, NSString *goodsId);
static BOOL ui_tryScrollDetailPage(id rootView);
static BOOL ui_tryTapDetailPurchaseButton(id rootView);
static NSArray *ui_collectImageURLs(id rootView);
static NSString *json_stringFromValue(id value);
static NSArray<NSString *> *json_topStringKeysFromDictionary(NSDictionary *dict, NSUInteger limit);
static void ui_updateLicensePresentation(NSDictionary *status, BOOL forcePresentation);
static id capture_decodedJSONObjectFromInspectableObject(id object);
static NSDictionary *capture_detailPayloadContainerFromObject(id object, NSInteger depth);
static NSString *capture_goodsIdFromDetailPayload(NSDictionary *payload);
static NSString *capture_goodsNameFromDetailPayload(NSDictionary *payload);
static NSInteger capture_detailPayloadScore(NSDictionary *payload, NSString *urlString);
static NSString *capture_normalizedJSONString(NSString *rawText, id jsonObject);
static id runtime_safeValueForObject(id object, NSString *key);
static BOOL runtime_probeKeyLooksInteresting(NSString *key);
static NSArray<NSString *> *runtime_probeMemberKeysForObject(id object);
static BOOL runtime_shouldSkipInspectingObject(id object);
static void runtime_appendProbeChildren(id object, NSString *path, NSMutableArray *roots);
static NSDictionary *runtime_makeCandidateFromObject(id object, NSString *path);
static NSDictionary *runtime_candidateFromInspectableObject(id object, NSString *path, NSInteger depth, NSMutableSet *visited);
static BOOL capture_trySaveTaskCandidate(NSDictionary *candidate);
static NSDictionary *capture_acceptedDetailPayloadFromObject(id jsonObject, NSString *urlString, NSInteger *scoreOut);
static NSDictionary *capture_runtimeFallbackCandidateForCurrentPage(void);
static NSString *capture_normalizedTextForMatching(NSString *text);
static NSString *ui_visibleTextSampleFromView(id rootView);
static BOOL capture_candidateMatchesVisibleText(NSDictionary *candidate, NSString *visibleText);
static void capture_pruneRecentCandidatesLocked(void);
static void capture_storeRecentCandidate(NSDictionary *candidate);
static NSDictionary *capture_bestRecentCandidateForVisibleText(NSString *visibleText);
static BOOL task_finalizeCurrentDetailAttemptIfNeeded(void);
static void ui_showTaskCaptureToast(NSString *goodsName);
static void hook_installNSURLSessionDelegateHooks(void);
static void hook_NSURLSessionDelegate_didReceiveResponse(id self, SEL _cmd, NSURLSession *session, NSURLSessionDataTask *dataTask, NSURLResponse *response, void (^completionHandler)(NSInteger));
static void hook_NSURLSessionDelegate_didReceiveData(id self, SEL _cmd, NSURLSession *session, NSURLSessionDataTask *dataTask, NSData *data);
static void hook_NSURLSessionDelegate_taskDidComplete(id self, SEL _cmd, NSURLSession *session, NSURLSessionTask *task, NSError *error);
static void task_runAutoSearchStep(void);
static id ui_currentRootViewController(void);

// 字符串解密函数（多层加密，兼容旧加密值）
static NSString *decodeObfuscatedString(unsigned char *obfuscatedBytes, int byteCount, int primaryXorKey, int secondaryXorKey) {
    NSMutableString *decodedString = [NSMutableString stringWithCapacity:byteCount];
    for (int byteIndex = 0; byteIndex < byteCount; byteIndex++) {
        unsigned char decodedByte = obfuscatedBytes[byteIndex];
        // 旧算法：XOR + 位移 + 双重密钥（兼容现有加密值）
        decodedByte = decodedByte ^ primaryXorKey;
        decodedByte = decodedByte ^ secondaryXorKey;
        decodedByte = (decodedByte << 3) | (decodedByte >> 5);
        decodedByte = decodedByte ^ 0xAA;
        [decodedString appendFormat:@"%c", decodedByte];
    }
    return decodedString;
}

static void initializePDDStoragePaths() {
    if (!taskDataRootDir) {
        NSArray *documentDirectories = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *documentsDirectoryPath = [documentDirectories firstObject];
        // 字符串加密混淆
        unsigned char obfuscatedGoodsDataNameBytes[] = { 0x39, 0xbb, 0xbb, 0xdb, 0xde, 0xde, 0xbf, 0x5d, 0xbb, 0x1f, 0xbd, 0x1f };
        NSString *decodedGoodsDataDirectoryName = decodeObfuscatedString(obfuscatedGoodsDataNameBytes, 12, 0x5A, 0x3C);
        taskDataRootDir = [documentsDirectoryPath stringByAppendingPathComponent:decodedGoodsDataDirectoryName];
        
        NSFileManager *defaultFileManager = [NSFileManager defaultManager];
        if (![defaultFileManager fileExistsAtPath:taskDataRootDir]) {
            [defaultFileManager createDirectoryAtPath:taskDataRootDir
                   withIntermediateDirectories:YES 
                                    attributes:nil 
                                         error:nil];
        }

        taskCommandRootDir = [taskDataRootDir stringByAppendingPathComponent:@"commands"];
        taskCommandInboxDir = [taskCommandRootDir stringByAppendingPathComponent:@"inbox"];
        taskCommandStatusDir = [taskCommandRootDir stringByAppendingPathComponent:@"status"];
        taskCommandOutboxDir = [taskCommandRootDir stringByAppendingPathComponent:@"outbox"];
        taskCommandStopDir = [taskCommandRootDir stringByAppendingPathComponent:@"stop"];
        taskRootDir = [taskDataRootDir stringByAppendingPathComponent:@"tasks"];
        taskLicenseDir = [taskDataRootDir stringByAppendingPathComponent:@"license"];
        taskLicenseFilePath = [taskLicenseDir stringByAppendingPathComponent:@"license.json"];
        taskLicenseStatusPath = [taskLicenseDir stringByAppendingPathComponent:@"status.json"];
        taskLicenseBindingFilePath = [taskLicenseDir stringByAppendingPathComponent:@"device_binding_id.txt"];
        task_ensureCommandDirectories();
        
    }
}


#pragma mark - Realtime Socket (即时通信)
#include "modules/realtime_socket.inc"

#pragma mark - Capture Pipeline
#include "modules/capture_pipeline.inc"

#pragma mark - Task Store
#include "modules/task_store.inc"

#pragma mark - License Gate
#include "modules/license_gate.inc"

#pragma mark - Hook Bridge
#include "modules/hook_bridge.inc"

#pragma mark - UI State And Actions
#include "modules/ui_state_actions.inc"

#pragma mark - Task Runner
#include "modules/task_runner.inc"
