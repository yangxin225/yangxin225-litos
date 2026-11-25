/*
 * 实训作业：WiFi 单向通信 AP 模式（Hi3861 / HiSpark WiFi IoT）
 * 热点名：H-yangxin   密码：12345678（可自行修改）
 *
 * 用法：把本文件放入 vendor 目录下 wifiap 工程的 src 目录，
 * 并确认工程的 BUILD.gn 将其编入目标（参考文件末尾说明）。
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

#define LOG_I(fmt, args...) printf("  - [AP]:" fmt "\r\n", ##args)
#define LOG_E(fmt, args...) printf("  - [AP_ERR]>>>>>>>>>>>>:" fmt "\r\n", ##args)

/* ===== 按作业要求配置 ===== */
#define AP_SSID "H-yangxin"   /* 热点名称：H-姓名全拼 */
#define AP_PSK  "12345678"    /* 热点密码：自己设置，必须 >= 8 位 */
/* ========================= */

#define ONE_SECOND 1
#define DEF_TIMEOUT 15

static int g_apFlag = 0;
static struct netif *g_apNetif = NULL;
static int g_staJoined = 0;

static void OnHotspotStaJoinHandler(StationInfo *info)
{
    (void)info;
    g_staJoined = 1;
    LOG_I("Station joined. <== 手机已连上热点!\r\n");
}

static void OnHotspotStaLeaveHandler(StationInfo *info)
{
    (void)info;
    g_staJoined = 0;
    LOG_I("Station left.\r\n");
}

static void OnHotspotStateChangedHandler(int state)
{
    LOG_I("Hotspot state: %d\r\n", state);
    if (state == HOTSPOT_ACTIVE) {
        LOG_I("Hotspot [%s] started successfully.\r\n", AP_SSID);
        g_apFlag = 1;
        /* 热点激活后启动 DHCP 服务，为接入的手机分配 IP */
        if (g_apNetif != NULL) {
            netifapi_dhcps_start(g_apNetif, 0, 0);
        }
    } else if (state == HOTSPOT_NOT_ACTIVE) {
        LOG_I("Hotspot stopped.\r\n");
        g_apFlag = 0;
    }
}

static WifiEvent g_wifiEventHandler = {
    .OnHotspotStaJoin    = OnHotspotStaJoinHandler,
    .OnHotspotStaLeave   = OnHotspotStaLeaveHandler,
    .OnHotspotStateChanged = OnHotspotStateChangedHandler,
    .OnWifiScanStateChanged = NULL,
};

static void WifiApDemoTask(void)
{
    WifiErrorCode error;
    HotspotConfig config;

    /* 1. 注册 WiFi 事件回调 */
    error = RegisterWifiEvent(&g_wifiEventHandler);
    if (error != WIFI_SUCCESS) {
        LOG_E("RegisterWifiEvent failed, err=%d\r\n", error);
        return;
    }

    /* 2. 配置热点参数：名称 H-yangxin，WPA2-PSK */
    memset(&config, 0, sizeof(HotspotConfig));
    if (strcpy_s(config.ssid, sizeof(config.ssid), AP_SSID) != EOK) {
        LOG_E("copy ssid failed\r\n");
        return;
    }
    if (strcpy_s(config.psk, sizeof(config.psk), AP_PSK) != EOK) {
        LOG_E("copy psk failed\r\n");
        return;
    }
    config.securityType = WIFI_SEC_TYPE_PSK;
    config.band = HOTSPOT_BAND_TYPE_2G;
    config.channel = 6;

    error = SetHotspotConfig(&config);
    if (error != WIFI_SUCCESS) {
        LOG_E("SetHotspotConfig failed, err=%d (检查密码是否>=8位)\r\n", error);
        return;
    }
    LOG_I("SetHotspotConfig ok. ssid=%s psk=%s\r\n", AP_SSID, AP_PSK);

    /* 3. 启动 AP 热点 */
    error = EnableHotspot();
    if (error != WIFI_SUCCESS) {
        LOG_E("EnableHotspot failed, err=%d\r\n", error);
        return;
    }
    LOG_I("EnableHotspot called, waiting for HOTSPOT_ACTIVE...\r\n");

    /* 4. 等待热点就绪 */
    for (int i = 0; i < DEF_TIMEOUT; i++) {
        sleep(ONE_SECOND);
        if (g_apFlag == 1) {
            break;
        }
    }
    if (g_apFlag != 1) {
        LOG_E("Hotspot not active after %ds, check serial log\r\n", DEF_TIMEOUT);
        return;
    }

    /* 5. 获取 AP 网口，供 DHCP 使用 */
    g_apNetif = netifapi_netif_find("ap0");
    if (g_apNetif == NULL) {
        LOG_E("netif ap0 not found\r\n");
    }

    LOG_I("=== AP mode running. ssid=%s ===\r\n", AP_SSID);
    LOG_I("=== 请用手机 WLAN 连接热点 %s（密码 %s）===\r\n", AP_SSID, AP_PSK);

    while (1) {
        sleep(ONE_SECOND);
        if (g_staJoined) {
            LOG_I(">>> 手机已连接热点 %s，可在手机上截图提交作业 <<<\r\n", AP_SSID);
        }
    }
}

static void WifiApDemoEntry(void)
{
    osThreadAttr_t attr = {0};
    attr.name = "WifiApDemoTask";
    attr.stack_size = 4096;
    attr.priority = osPriorityNormal;
    if (osThreadNew((osThreadFunc_t)WifiApDemoTask, NULL, &attr) == NULL) {
        LOG_E("create WifiApDemoTask failed\r\n");
    }
}

SYS_RUN(WifiApDemoEntry);

/*
 * BUILD.gn 需包含的依赖（如果工程里没有）：
 *   include_dirs += [
 *     "//utils/native/lite/include",
 *     "//kernel/liteos_m/components/cmsis/2.0",
 *     "//foundation/communication/interfaces/kits/wifi_lite/wifiservice",
 *     "//third_party/lwip_sack/include",
 *   ]
 *   deps += [ "//foundation/communication/wifi_lite/wifiservice:wifiservice" ]
 *
 * 若 vendor 里已有 wifiap 示例工程，通常只需把原来的 ssid/psk 改成
 * H-yangxin 和你的密码即可，不必替换整个文件。
 */
