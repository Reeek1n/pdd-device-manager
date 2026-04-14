// SSH 保活模块 - 保持 OpenSSH 连接活跃，防止超时断开
// 功能：
// 1. 定期发送保活包
// 2. 监控连接状态
// 3. 自动重启 SSH 服务（如果需要）

#import <sys/socket.h>
#import <netinet/in.h>
#import <netinet/tcp.h>
#import <unistd.h>
#import <fcntl.h>

static NSTimer *sshKeepaliveTimer = nil;
static int sshKeepaliveInterval = 30; // 30秒发送一次保活
static BOOL sshKeepaliveEnabled = YES;
static int sshPort = 22;
static NSString *sshStatusFilePath = nil;

// 检查 SSH 服务状态
static BOOL ssh_isServiceRunning(void) {
    // 检查 sshd 进程是否存在
    FILE *fp = popen("pgrep -x sshd", "r");
    if (fp) {
        char buffer[128];
        BOOL running = (fgets(buffer, sizeof(buffer), fp) != NULL);
        pclose(fp);
        return running;
    }
    return NO;
}

// 获取 SSH 连接数
static int ssh_getConnectionCount(void) {
    FILE *fp = popen("netstat -an | grep ':22 ' | grep ESTABLISHED | wc -l", "r");
    if (fp) {
        char buffer[128];
        int count = 0;
        if (fgets(buffer, sizeof(buffer), fp)) {
            count = atoi(buffer);
        }
        pclose(fp);
        return count;
    }
    return 0;
}

// 发送 TCP KeepAlive 包到所有 SSH 连接
static void ssh_sendKeepalive(void) {
    if (!sshKeepaliveEnabled) return;
    
    // 获取所有 ESTABLISHED 的 SSH 连接
    FILE *fp = popen("netstat -an | grep ':22 ' | grep ESTABLISHED | awk '{print $5}'", "r");
    if (!fp) return;
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        // 解析客户端地址
        char *clientAddr = strtok(buffer, ":");
        char *clientPort = strtok(NULL, ":");
        
        if (clientAddr && clientPort) {
            // 创建临时 socket 发送保活
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock >= 0) {
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(atoi(clientPort));
                inet_pton(AF_INET, clientAddr, &addr.sin_addr);
                
                // 设置 TCP KeepAlive
                int keepalive = 1;
                int keepidle = 10;    // 10秒空闲后开始保活
                int keepinterval = 5; // 每5秒发送一次
                int keepcount = 3;    // 重试3次
                
                setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE, &keepidle, sizeof(keepidle));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));
                
                close(sock);
            }
        }
    }
    pclose(fp);
    
    NSLog(@"[SSHKeepalive] 保活包已发送");
}

// 检查并更新 SSH 配置
static void ssh_ensureKeepaliveConfig(void) {
    // 检查 sshd_config 是否配置了保活
    NSString *configPath = @"/etc/ssh/sshd_config";
    NSFileManager *fm = [NSFileManager defaultManager];
    
    if (![fm fileExistsAtPath:configPath]) {
        configPath = @"/private/etc/ssh/sshd_config";
    }
    
    if ([fm fileExistsAtPath:configPath]) {
        NSError *error = nil;
        NSString *content = [NSString stringWithContentsOfFile:configPath
                                                       encoding:NSUTF8StringEncoding
                                                          error:&error];
        
        if (!error && content) {
            BOOL needsUpdate = NO;
            NSMutableString *newContent = [content mutableCopy];
            
            // 检查 ClientAliveInterval
            if (![content containsString:@"ClientAliveInterval"]) {
                [newContent appendString:@"\n# PDD Plugin KeepAlive Settings\n"];
                [newContent appendString:@"ClientAliveInterval 30\n"];
                [newContent appendString:@"ClientAliveCountMax 3\n"];
                [newContent appendString:@"TCPKeepAlive yes\n"];
                needsUpdate = YES;
            }
            
            if (needsUpdate) {
                // 写入新配置
                [newContent writeToFile:configPath
                            atomically:YES
                              encoding:NSUTF8StringEncoding
                                 error:&error];
                
                if (!error) {
                    NSLog(@"[SSHKeepalive] SSH 配置已更新，重启服务...");
                    // 重启 SSH 服务
                    system("launchctl unload /System/Library/LaunchDaemons/ssh.plist 2>/dev/null; launchctl load /System/Library/LaunchDaemons/ssh.plist 2>/dev/null");
                }
            }
        }
    }
}

// 保活定时器回调
static void ssh_keepaliveCallback(void) {
    // 检查 SSH 服务是否运行
    if (!ssh_isServiceRunning()) {
        NSLog(@"[SSHKeepalive] ⚠️ SSH 服务未运行，尝试重启...");
        system("launchctl load /System/Library/LaunchDaemons/ssh.plist 2>/dev/null");
        return;
    }
    
    // 检查连接数
    int connCount = ssh_getConnectionCount();
    if (connCount > 0) {
        NSLog(@"[SSHKeepalive] 当前有 %d 个 SSH 连接", connCount);
        ssh_sendKeepalive();
    }
    
    // 更新状态文件
    ssh_updateStatusFile();
}

// 更新状态文件
static void ssh_updateStatusFile(void) {
    if (!taskDataRootDir) return;
    
    if (!sshStatusFilePath) {
        sshStatusFilePath = [taskDataRootDir stringByAppendingPathComponent:@"ssh_status.json"];
    }
    
    NSDictionary *status = @{
        @"timestamp": @((long long)[[NSDate date] timeIntervalSince1970]),
        @"is_running": @(ssh_isServiceRunning()),
        @"connection_count": @(ssh_getConnectionCount()),
        @"port": @(sshPort),
        @"keepalive_enabled": @(sshKeepaliveEnabled),
        @"keepalive_interval": @(sshKeepaliveInterval)
    };
    
    task_writeJSON(status, sshStatusFilePath);
}

// 启动保活
static void ssh_startKeepalive(void) {
    if (sshKeepaliveTimer) {
        [sshKeepaliveTimer invalidate];
    }
    
    // 确保 SSH 配置正确
    ssh_ensureKeepaliveConfig();
    
    // 创建定时器
    sshKeepaliveTimer = [NSTimer scheduledTimerWithTimeInterval:sshKeepaliveInterval
                                                           target:[NSBlockOperation blockOperationWithBlock:^{
        ssh_keepaliveCallback();
    }]
                                                         selector:@selector(main)
                                                         userInfo:nil
                                                          repeats:YES];
    
    NSLog(@"[SSHKeepalive] SSH 保活已启动，间隔: %d 秒", sshKeepaliveInterval);
    
    // 立即执行一次
    ssh_keepaliveCallback();
}

// 停止保活
static void ssh_stopKeepalive(void) {
    if (sshKeepaliveTimer) {
        [sshKeepaliveTimer invalidate];
        sshKeepaliveTimer = nil;
        NSLog(@"[SSHKeepalive] SSH 保活已停止");
    }
}

// 设置保活间隔
static void ssh_setKeepaliveInterval(int interval) {
    if (interval < 5) interval = 5; // 最小5秒
    if (interval > 300) interval = 300; // 最大5分钟
    
    sshKeepaliveInterval = interval;
    
    // 如果正在运行，重启定时器
    if (sshKeepaliveTimer) {
        ssh_stopKeepalive();
        ssh_startKeepalive();
    }
}

// 获取 SSH 状态
static NSDictionary *ssh_getStatus(void) {
    return @{
        @"is_running": @(ssh_isServiceRunning()),
        @"connection_count": @(ssh_getConnectionCount()),
        @"port": @(sshPort),
        @"keepalive_enabled": @(sshKeepaliveEnabled),
        @"keepalive_interval": @(sshKeepaliveInterval)
    };
}

// 在应用启动时调用
static void ssh_initKeepalive(void) {
    // 检查 SSH 是否已安装
    if (![[NSFileManager defaultManager] fileExistsAtPath:@"/usr/sbin/sshd"]) {
        NSLog(@"[SSHKeepalive] ⚠️ SSH 服务未安装");
        return;
    }
    
    // 启动保活
    ssh_startKeepalive();
    
    // 监听应用进入后台/前台
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidEnterBackgroundNotification
                                                        object:nil
                                                         queue:[NSOperationQueue mainQueue]
                                                    usingBlock:^(NSNotification *note) {
        NSLog(@"[SSHKeepalive] 应用进入后台，保持保活");
    }];
    
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillEnterForegroundNotification
                                                        object:nil
                                                         queue:[NSOperationQueue mainQueue]
                                                    usingBlock:^(NSNotification *note) {
        NSLog(@"[SSHKeepalive] 应用回到前台，检查 SSH 状态");
        ssh_keepaliveCallback();
    }];
}
