/*
 * 实训作业：WiFi 双向通信 —— AP 端（服务端）
 * 板子：Hi3861 / HiSpark WiFi IoT
 *
 * 功能：
 *   1. 启动 AP 热点 H-yangxin（密码 12345678），启动 DHCP
 *   2. 在 8888 端口开 TCP 服务器，等待 STA 板接入
 *   3. 串口收到什么，就通过 WiFi 原样发给 STA 板
 *      （在串口助手里发送“您好”即可）
 *   4. 收到 STA 发来的数据，打印 "STA发来:'收到'"
 *
 * 提交内容：本工程代码 + 串口截图（包含 AP发送'您好' 与 STA发来'收到'）
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"

#include "wifi_device.h"
#include "wifi_hotspot.h"
#include "wifi_error_code.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"
#include "iot_uart.h"

#define LOG_I(fmt, args...) printf("  - [AP]:" fmt "\r\n", ##args)
#define LOG_E(fmt, args...) printf("  - [AP_ERR]:" fmt "\r\n", ##args)

/* ===== 热点配置（与 STA 端同学约定一致）===== */
#define AP_SSID "H-yangxin"
#define AP_PSK  "12345678"
#define AP_CHANNEL 6

#define TCP_PORT  8888          /* TCP 服务器端口 */
#define RECV_BUF_LEN 256

static int g_apFlag = 0;
static int g_cliFd = -1;        /* 已连接的 STA 客户端 socket */

/* ---------------- WiFi AP 部分 ---------------- */
static void OnHotspotStateChangedHandler(int state)
{
    if (state == HOTSPOT_ACTIVE) {
        LOG_I("Hotspot [%s] started.\r\n", AP_SSID);
        g_apFlag = 1;
    } else {
        LOG_I("Hotspot stopped.\r\n");
        g_apFlag = 0;
    }
}

static WifiEvent g_wifiEventHandler = {
    .OnHotspotStaJoin = NULL,
    .OnHotspotStaLeave = NULL,
    .OnHotspotStateChanged = OnHotspotStateChangedHandler,
    .OnWifiScanStateChanged = NULL,
};

static void WifiApSetup(void)
{
    HotspotConfig config = {0};

    if (RegisterWifiEvent(&g_wifiEventHandler) != WIFI_SUCCESS) {
        LOG_E("RegisterWifiEvent failed\r\n");
        return;
    }

    strcpy_s(config.ssid, sizeof(config.ssid), AP_SSID);
    strcpy_s(config.psk, sizeof(config.psk), AP_PSK);
    config.securityType = WIFI_SEC_TYPE_PSK;
    config.band = HOTSPOT_BAND_TYPE_2G;
    config.channel = AP_CHANNEL;

    if (SetHotspotConfig(&config) != WIFI_SUCCESS) {
        LOG_E("SetHotspotConfig failed\r\n");
        return;
    }
    if (EnableHotspot() != WIFI_SUCCESS) {
        LOG_E("EnableHotspot failed\r\n");
        return;
    }

    /* 等热点激活后启动 DHCP */
    for (int i = 0; i < 15 && g_apFlag == 0; i++) {
        sleep(1);
    }
    if (g_apFlag == 0) {
        LOG_E("hotspot not active\r\n");
        return;
    }

    struct netif *netif = netifapi_netif_find("ap0");
    if (netif != NULL) {
        netifapi_dhcps_start(netif, 0, 0);
        LOG_I("DHCP started. AP IP: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    }
}

/* ---------------- TCP 服务器部分 ---------------- */
static void TcpServerTask(void)
{
    char buf[RECV_BUF_LEN];
    struct sockaddr_in servAddr = {0};
    struct sockaddr_in cliAddr = {0};
    socklen_t cliLen = sizeof(cliAddr);

    WifiApSetup();

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        LOG_E("create socket failed\r\n");
        return;
    }

    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(TCP_PORT);
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0) {
        LOG_E("bind failed\r\n");
        closesocket(sockfd);
        return;
    }
    if (listen(sockfd, 1) < 0) {
        LOG_E("listen failed\r\n");
        closesocket(sockfd);
        return;
    }
    LOG_I("TCP server listening on port %d, waiting STA...\r\n", TCP_PORT);

    while (1) {
        /* 等待 STA 板接入 */
        g_cliFd = accept(sockfd, (struct sockaddr *)&cliAddr, &cliLen);
        if (g_cliFd < 0) {
            continue;
        }
        LOG_I("STA connected: %s\r\n", inet_ntoa(cliAddr.sin_addr));

        /* 循环接收 STA 发来的数据 */
        while (1) {
            int ret = recv(g_cliFd, buf, sizeof(buf) - 1, 0);
            if (ret <= 0) {
                LOG_I("STA disconnected, waiting again...\r\n");
                closesocket(g_cliFd);
                g_cliFd = -1;
                break;
            }
            buf[ret] = '\0';
            /* 作业要求：AP 端在串口上看到 STA 发送的“收到” */
            printf("  - [AP]:STA发来:'%s'\r\n", buf);
        }
    }
}

/* ---------------- 串口转发部分 ---------------- */
static void UartForwardTask(void)
{
    unsigned char buf[RECV_BUF_LEN];

    while (1) {
        /* 读串口（调试串口 UART0，串口助手里发什么就转发什么） */
        int len = IoTUartRead(WIFI_IOT_UART_IDX_0, buf, sizeof(buf) - 1);
        if (len <= 0) {
            usleep(100 * 1000);
            continue;
        }
        buf[len] = '\0';

        if (g_cliFd < 0) {
            LOG_E("STA not connected yet, cannot send\r\n");
            continue;
        }

        /* 通过 WiFi 发给 STA 板 */
        int ret = send(g_cliFd, buf, len, 0);
        if (ret > 0) {
            printf("  - [AP]:AP发送:'%s'\r\n", buf);
        } else {
            LOG_E("send failed\r\n");
        }
    }
}

static void ApSideEntry(void)
{
    osThreadAttr_t attr = {0};

    attr.name = "TcpServerTask";
    attr.stack_size = 8192;
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)TcpServerTask, NULL, &attr);

    attr.name = "UartForwardTask";
    attr.stack_size = 4096;
    attr.priority = osPriorityNormal;
    osThreadNew((osThreadFunc_t)UartForwardTask, NULL, &attr);
}

SYS_RUN(ApSideEntry);
