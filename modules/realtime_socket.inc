// 实时 Socket 通信模块 - 实现真正的双向即时通信
// iOS 作为服务端，桌面应用作为客户端连接

#import <sys/socket.h>
#import <netinet/in.h>
#import <netinet/tcp.h>
#import <arpa/inet.h>
#import <fcntl.h>
#import <unistd.h>

// 外部变量声明
extern NSString *taskCurrentId;
extern NSString *taskCurrentKeyword;
extern NSInteger taskSavedCount;
extern NSInteger taskTargetCount;
extern NSInteger taskAttemptedCount;

// 外部函数声明
extern BOOL task_shouldStopCurrentTask(void);
extern void task_finishActiveTask(NSString *state, NSString *error);

// 静态变量用于命令处理
static BOOL realtime_stopRequested = NO;
static BOOL realtime_pauseRequested = NO;

// 函数前置声明
static BOOL realtime_sendMessage(NSDictionary *message);
static void realtime_sendCurrentStatus(void);
static void realtime_handleCommand(NSDictionary *message);
static void realtime_acceptConnection(void);
static void realtime_commandReceiveLoop(void);
static void realtime_notifyGoodsCaptured(NSDictionary *goodsData);

static int realtime_server_socket = -1;
static int realtime_client_socket = -1;
static int realtime_server_port = 9999;
static dispatch_queue_t realtime_socket_queue = nil;
static NSTimer *realtime_accept_timer = nil;
static BOOL realtime_server_running = NO;

// 消息类型定义
typedef NS_ENUM(NSInteger, RealtimeMessageType) {
    RealtimeMessageTypeGoodsCaptured = 1,    // 商品采集通知
    RealtimeMessageTypeTaskStarted = 2,      // 任务开始
    RealtimeMessageTypeTaskFinished = 3,     // 任务完成
    RealtimeMessageTypeProgressUpdate = 4,   // 进度更新
    RealtimeMessageTypeStatusUpdate = 5,     // 状态更新
    RealtimeMessageTypeCommandStop = 10,     // 停止命令
    RealtimeMessageTypeCommandPause = 11,    // 暂停命令
    RealtimeMessageTypeCommandResume = 12,   // 恢复命令
};

// 启动 Socket 服务端
static BOOL realtime_startServer(int port) {
    if (realtime_server_running) {
        NSLog(@"[RealtimeSocket] 服务端已在运行");
        return YES;
    }
    
    realtime_server_port = port;
    
    // 创建 socket
    realtime_server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (realtime_server_socket < 0) {
        NSLog(@"[RealtimeSocket] 创建 socket 失败");
        return NO;
    }
    
    // 设置端口复用
    int opt = 1;
    setsockopt(realtime_server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // 设置为非阻塞模式
    int flags = fcntl(realtime_server_socket, F_GETFL, 0);
    fcntl(realtime_server_socket, F_SETFL, flags | O_NONBLOCK);
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(realtime_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        NSLog(@"[RealtimeSocket] 绑定端口 %d 失败", port);
        close(realtime_server_socket);
        realtime_server_socket = -1;
        return NO;
    }
    
    // 开始监听
    if (listen(realtime_server_socket, 5) < 0) {
        NSLog(@"[RealtimeSocket] 监听失败");
        close(realtime_server_socket);
        realtime_server_socket = -1;
        return NO;
    }
    
    realtime_server_running = YES;
    NSLog(@"[RealtimeSocket] 服务端启动成功，端口: %d", port);
    
    // 创建队列
    realtime_socket_queue = dispatch_queue_create("com.pdd.realtime.socket", DISPATCH_QUEUE_SERIAL);
    
    // 启动接受连接的定时器
    realtime_accept_timer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                                               target:[NSBlockOperation blockOperationWithBlock:^{
        realtime_acceptConnection();
    }]
                                                             selector:@selector(main)
                                                             userInfo:nil
                                                              repeats:YES];
    
    // 启动命令接收循环
    dispatch_async(realtime_socket_queue, ^{
        realtime_commandReceiveLoop();
    });
    
    return YES;
}

// 接受客户端连接
static void realtime_acceptConnection(void) {
    if (realtime_client_socket >= 0) {
        return; // 已有连接
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int new_socket = accept(realtime_server_socket, (struct sockaddr *)&client_addr, &client_len);
    if (new_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            NSLog(@"[RealtimeSocket] 接受连接失败: %d", errno);
        }
        return;
    }
    
    realtime_client_socket = new_socket;
    
    // 设置客户端为非阻塞
    int flags = fcntl(realtime_client_socket, F_GETFL, 0);
    fcntl(realtime_client_socket, F_SETFL, flags | O_NONBLOCK);
    
    NSLog(@"[RealtimeSocket] 客户端已连接");
    
    // 发送欢迎消息
    realtime_sendMessage(@{@"type": @"connected", @"message": @"iOS Realtime Server Ready"});
    
    // 立即发送当前状态
    if (taskCurrentId) {
        realtime_sendCurrentStatus();
    }
}

// 命令接收循环
static void realtime_commandReceiveLoop(void) {
    while (realtime_server_running) {
        if (realtime_client_socket < 0) {
            usleep(100000); // 100ms
            continue;
        }
        
        char buffer[4096];
        ssize_t received = recv(realtime_client_socket, buffer, sizeof(buffer) - 1, 0);
        
        if (received > 0) {
            buffer[received] = '\0';
            NSString *jsonStr = [NSString stringWithUTF8String:buffer];
            if (jsonStr) {
                NSData *jsonData = [jsonStr dataUsingEncoding:NSUTF8StringEncoding];
                NSError *error = nil;
                NSDictionary *message = [NSJSONSerialization JSONObjectWithData:jsonData
                                                                        options:0
                                                                          error:&error];
                if (message && !error) {
                    realtime_handleCommand(message);
                }
            }
        } else if (received == 0) {
            // 客户端断开
            NSLog(@"[RealtimeSocket] 客户端断开连接");
            close(realtime_client_socket);
            realtime_client_socket = -1;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            NSLog(@"[RealtimeSocket] 接收错误: %d", errno);
            close(realtime_client_socket);
            realtime_client_socket = -1;
        }
        
        usleep(10000); // 10ms，高频检查
    }
}

// 处理接收到的命令
static void realtime_handleCommand(NSDictionary *message) {
    NSString *cmd = message[@"command"];
    NSLog(@"[RealtimeSocket] 收到命令: %@", cmd);
    
    if ([cmd isEqualToString:@"stop"]) {
        // 立即停止任务
        realtime_stopRequested = YES;
        // 调用停止函数
        if (taskCurrentId) {
            task_finishActiveTask(@"stopped", @"socket_command");
        }
        NSLog(@"[RealtimeSocket] ⚠️ 收到立即停止命令！");
        
        // 发送确认
        realtime_sendMessage(@{
            @"type": @"command_ack",
            @"command": @"stop",
            @"status": @"executing"
        });
        
    } else if ([cmd isEqualToString:@"pause"]) {
        // 暂停任务
        realtime_pauseRequested = YES;
        realtime_sendMessage(@{
            @"type": @"command_ack",
            @"command": @"pause",
            @"status": @"executed"
        });
        
    } else if ([cmd isEqualToString:@"resume"]) {
        // 恢复任务
        realtime_pauseRequested = NO;
        realtime_sendMessage(@{
            @"type": @"command_ack",
            @"command": @"resume",
            @"status": @"executed"
        });
        
    } else if ([cmd isEqualToString:@"get_status"]) {
        // 获取当前状态
        realtime_sendCurrentStatus();
        
    } else if ([cmd isEqualToString:@"new_task"]) {
        // 启动新任务
        NSString *keyword = message[@"keyword"];
        NSNumber *count = message[@"count"];
        if (keyword && count) {
            // 在新线程中启动任务，避免阻塞 socket
            dispatch_async(dispatch_get_main_queue(), ^{
                // 构建任务信息
                NSDictionary *taskInfo = @{
                    @"keyword": keyword,
                    @"target_count": count,
                    @"source": @"socket"
                };
                // 调用任务启动函数
                extern void task_startCollectTask(NSDictionary *taskInfo);
                task_startCollectTask(taskInfo);
            });
            
            realtime_sendMessage(@{
                @"type": @"command_ack",
                @"command": @"new_task",
                @"status": @"started",
                @"keyword": keyword
            });
        }
    }
}

// 发送消息到客户端
static BOOL realtime_sendMessage(NSDictionary *message) {
    if (realtime_client_socket < 0) {
        return NO; // 没有连接
    }
    
    NSError *error = nil;
    NSData *jsonData = [NSJSONSerialization dataWithJSONObject:message
                                                        options:0
                                                          error:&error];
    if (error || !jsonData) {
        return NO;
    }
    
    // 添加换行符作为消息分隔
    NSMutableData *sendData = [NSMutableData dataWithData:jsonData];
    [sendData appendData:[@"\n" dataUsingEncoding:NSUTF8StringEncoding]];
    
    ssize_t sent = send(realtime_client_socket, [sendData bytes], [sendData length], 0);
    return sent > 0;
}

// 发送当前状态
static void realtime_sendCurrentStatus(void) {
    if (!taskCurrentId) {
        realtime_sendMessage(@{
            @"type": @"status",
            @"has_task": @NO,
            @"state": @"idle"
        });
        return;
    }
    
    NSMutableDictionary *message = [NSMutableDictionary dictionary];
    message[@"type"] = @"status";
    message[@"has_task"] = @YES;
    message[@"task_id"] = taskCurrentId ?: @"";
    message[@"keyword"] = taskCurrentKeyword ?: @"";
    message[@"saved_count"] = @(taskSavedCount);
    message[@"target_count"] = @(taskTargetCount);
    message[@"attempted_count"] = @(taskAttemptedCount);
    message[@"progress"] = @((taskTargetCount > 0) ? (taskSavedCount * 100 / taskTargetCount) : 0);
    
    realtime_sendMessage(message);
}

// 商品采集时调用 - 即时推送
static void realtime_notifyGoodsCaptured(NSDictionary *goodsData) {
    if (!realtime_server_running) return;
    
    NSMutableDictionary *message = [NSMutableDictionary dictionary];
    message[@"type"] = @"goods_captured";
    message[@"timestamp"] = @((long long)[[NSDate date] timeIntervalSince1970]);
    message[@"task_id"] = taskCurrentId ?: @"";
    message[@"keyword"] = taskCurrentKeyword ?: @"";
    message[@"saved_count"] = @(taskSavedCount);
    message[@"target_count"] = @(taskTargetCount);
    message[@"progress"] = @((taskTargetCount > 0) ? (taskSavedCount * 100 / taskTargetCount) : 0);
    
    // 添加商品数据
    if (goodsData) {
        message[@"goods"] = goodsData;
    }
    
    BOOL sent = realtime_sendMessage(message);
    NSLog(@"[RealtimeSocket] 推送商品采集通知: %@, 成功: %d", 
          goodsData[@"goodsName"] ?: @"unknown", sent);
}

// 任务开始时调用
static void realtime_notifyTaskStarted(NSString *keyword, int targetCount) {
    if (!realtime_server_running) return;
    
    realtime_sendMessage(@{
        @"type": @"task_started",
        @"task_id": taskCurrentId ?: @"",
        @"keyword": keyword ?: @"",
        @"target_count": @(targetCount),
        @"timestamp": @((long long)[[NSDate date] timeIntervalSince1970])
    });
}

// 任务结束时调用
static void realtime_notifyTaskFinished(NSString *state, NSString *error) {
    if (!realtime_server_running) return;
    
    realtime_sendMessage(@{
        @"type": @"task_finished",
        @"task_id": taskCurrentId ?: @"",
        @"state": state ?: @"",
        @"error": error ?: @"",
        @"saved_count": @(taskSavedCount),
        @"timestamp": @((long long)[[NSDate date] timeIntervalSince1970])
    });
}

// 进度更新 (保留供将来使用)
/*
static void realtime_notifyProgress(void) {
    if (!realtime_server_running || !taskCurrentId) return;
    
    realtime_sendMessage(@{
        @"type": @"progress",
        @"saved_count": @(taskSavedCount),
        @"target_count": @(taskTargetCount),
        @"progress": @((taskTargetCount > 0) ? (taskSavedCount * 100 / taskTargetCount) : 0),
        @"attempted_count": @(taskAttemptedCount)
    });
}
*/
