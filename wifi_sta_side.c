/*
 * 实训作业：WiFi 双向通信 —— STA 端（客户端）
 * 板子：Hi3861 / HiSpark WiFi IoT
 *
 * 功能：
 *   1. 以 STA 模式连接 AP 板的热点 H-yangxin（密码 12345678）
 *   2. 作为 TCP 客户端连接 AP 板的 8888 端口
 *   3. 收到 AP 发来的数据：串口打印 "接收到:'您好'"
 *   4. 自动回复 "收到" 给 AP 板
 *
 * 提交内容：本工程代码 + 串口截图（包含 接收到'您好' 与 STA发送'收到'）
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "wifi_device.h"
#include "wifi_error_code.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"

#define LOG_I(fmt, args...) printf("  - [STA]:" fmt "\r\n", ##args)
#define LOG_E(fmt, args...) printf("  - [STA_ERR]:" fmt "\r\n", ##args)

/* ===== 与 AP 端同学约定一致的热点参数 ===== */
#define AP_SSID "H-yangxin"
#define AP_PSK  "12345678"

/* AP 板开热点后的网关 IP，一般默认是 192.168.4.1。
 * 以 AP 端串口打印的 "AP IP: xxx.xxx.xxx.xxx" 为准，不一致就改这里 */
#define SERVER_IP   "192.168.4.1"
#define SERVER_PORT 8888

#define RECV_BUF_LEN 256

static int g_connected = 0;

/* ---------------- WiFi STA 部分 ---------------- */
static void OnWifiConnectionChangedHandler(int state, WifiSecurityType *unused)
{
    (void)unused;
    if (state == WIFI_STATION_CONNECTED) {
        LOG_I("connected to AP [%s]\r\n", AP_SSID);
        g_connected = 1;
    } else {
        LOG_I("disconnected\r\n");
        g_connected = 0;
    }
}

static WifiEvent g_wifiEventHandler = {
    .OnWifiConnectionChanged = OnWifiConnectionChangedHandler,
    .OnWifiScanStateChanged = NULL,
};

static int WifiStaSetup(void)
{
    WifiDeviceConfig apConfig = {0};

    if (RegisterWifiEvent(&g_wifiEventHandler) != WIFI_SUCCESS) {
        LOG_E("RegisterWifiEvent failed\r\n");
        return -1;
    }

    strcpy_s(apConfig.ssid, sizeof(apConfig.ssid), AP_SSID);
    strcpy_s(apConfig.psk, sizeof(apConfig.psk), AP_PSK);
    apConfig.securityType = WIFI_SEC_TYPE_PSK;

    int netId = AddDeviceConfig(&apConfig);
    if (netId < 0) {
        LOG_E("AddDeviceConfig failed\r\n");
        return -1;
    }

    if (ConnectTo(netId) != WIFI_SUCCESS) {
        LOG_E("ConnectTo failed\r\n");
        return -1;
    }

    /* 等待连接成功 */
    for (int i = 0; i < 20 && g_connected == 0; i++) {
        sleep(1);
    }
    if (g_connected == 0) {
        LOG_E("connect timeout, check AP board\r\n");
        return -1;
    }

    /* 启动 DHCP 获取 IP */
    struct netif *netif = netifapi_netif_find("wlan0");
    if (netif != NULL) {
        netifapi_dhcp_start(netif);
        LOG_I("DHCP started on wlan0\r\n");
    }
    return 0;
}

/* ---------------- TCP 客户端部分 ---------------- */
static void TcpClientTask(void)
{
    char buf[RECV_BUF_LEN];
    struct sockaddr_in servAddr = {0};

    if (WifiStaSetup() != 0) {
        return;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_E("create socket failed\r\n");
        return;
    }

    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(SERVER_PORT);
    servAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    LOG_I("connecting to %s:%d ...\r\n", SERVER_IP, SERVER_PORT);
    if (connect(sockfd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        LOG_E("connect failed, check SERVER_IP and AP board\r\n");
        closesocket(sockfd);
        return;
    }
    LOG_I("TCP connected!\r\n");

    /* 循环接收 AP 发来的数据并自动回复 */
    while (1) {
        int ret = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (ret <= 0) {
            LOG_I("AP disconnected\r\n");
            break;
        }
        buf[ret] = '\0';

        /* 作业要求：STA 端串口打印 接收到“您好” */
        printf("  - [STA]:接收到:'%s'\r\n", buf);

        /* 自动回复“收到” */
        const char *reply = "收到";
        if (send(sockfd, reply, strlen(reply), 0) > 0) {
            printf("  - [STA]:STA发送:'%s'\r\n", reply);
        }
    }
    closesocket(sockfd);
}

static void StaSideEntry(void)
{
    osThreadAttr_t attr = {0};
    attr.name = "TcpClientTask";
    attr.stack_size = 8192;
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)TcpClientTask, NULL, &attr);
}

SYS_RUN(StaSideEntry);
