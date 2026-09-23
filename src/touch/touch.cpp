#include "touch/touch.h"

#include <linux/input.h>
#include <linux/uinput.h>

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <filesystem>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <sstream>
#include <vector>
#include <string>
#include <iostream>

#include <android/log.h>

#define TAG "muchen"
#define Log __android_log_print

namespace {
constexpr int kMaxSlots = 10;              // ABS_MT_SLOT 最大数量（物理/模拟共用）
constexpr int kPhysicalTrackingIdBase = 114514 % 65536;   // 物理触点 tracking ID 基址 → [48978, 48987]
constexpr int kSimulatedTrackingIdBase = 415411 % 65536;  // 模拟触点 tracking ID 基址 → [22195, 22204]
constexpr int kAbsTrackingIdMax = 65535;   // absmax[ABS_MT_TRACKING_ID]
constexpr int kUinputInitDelayUs = 100000; // 构造函数中等待方向线程初始化的延时
constexpr int kOrientationPollIterations = 50;
constexpr int kOrientationPollIntervalMs = 100;
constexpr int kFallbackScreenWidth = 1080;
constexpr int kFallbackScreenHeight = 1920;
constexpr int kEventsPerFinger = 4;        // TRACKING_ID + X + Y + SYN_MT_REPORT
constexpr int kEventsCapacity = 2 * kMaxSlots * kEventsPerFinger + 2; // +空帧回退 +SYN_REPORT = 82
constexpr int kVendorId = 0x6c90;
constexpr int kProductId = 0x8fb0;
constexpr int kAbsXMax = 1599;
constexpr int kAbsYMax = 2559;
constexpr int kPressureMax = 1000;
constexpr int kTouchMajorMax = 255;
constexpr float kRatioSnapLower = 0.9f;    // 比例钳制下界
constexpr float kRatioSnapUpper = 1.0f;    // 比例钳制上界
} // namespace

void touch::InitTouchScreenInfo()
{
    for (const auto &entry: std::filesystem::directory_iterator("/dev/input/"))
    {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue; // 仅处理 event* 文件

        int fd = open(entry.path().c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0)
        {
            Log(ANDROID_LOG_WARN,TAG, "%s", std::string("打开 " + entry.path().string() + "失败").c_str());
            std::cout<<std::string("打开 " + entry.path().string() + "失败").c_str()<<std::endl;
            continue;
        }
        input_absinfo absinfo{};
        ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);

        // 只要有 ABS_MT_SLOT 且数量合理 (>1)，就视为触摸屏
        if (absinfo.maximum > 0 && absinfo.maximum < kMaxSlots)
        {
            Log(ANDROID_LOG_INFO,TAG, "%s", std::string("找到疑似触摸节点: " + entry.path().string()).c_str());
            std::cout << std::string("找到疑似触摸节点: " + entry.path().string()).c_str() << std::endl;
            this->touchScreenInfo.fd.emplace_back(fd); //直接使用已打开的fd，避免重复open

            if (touchScreenInfo.width == 0 || touchScreenInfo.height == 0) //有多个节点的情况下，只使用第一个节点的信息
            {
                input_absinfo absX{}, absY{};
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX);
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY);
                if (absX.maximum != 0 && absY.maximum != 0)
                {
                    this->touchScreenInfo.width = absX.maximum;
                    this->touchScreenInfo.height = absY.maximum;
                }
            }
        } else
        {
            close(fd); // 非触摸屏节点，关闭
        }
    } // 遍历/dev/input/下所有eventX，如果ABS_MT_SLOT数量合理就视为触摸屏
}

void touch::InitScreenInfo()
{
    std::string ScreenSize = exec("wm size");
    std::istringstream ScreenSizeStream(ScreenSize);
    std::string line{};

    while (std::getline(ScreenSizeStream, line))
    {
        if (sscanf(line.c_str(), "Override size: %dx%d", &this->screenInfo.width, &this->screenInfo.height) == 2)
        {
            break; // 找到后立即退出循环
        }
        sscanf(line.c_str(), "Physical size: %dx%d", &this->screenInfo.width, &this->screenInfo.height);
    } //有Override size则优先使用,找不到就使用Physical size

    // 防止某些ROM下wm size解析失败导致除零，使用安全回退值
    if (screenInfo.width == 0 || screenInfo.height == 0)
    {
        screenInfo.width = kFallbackScreenWidth;
        screenInfo.height = kFallbackScreenHeight;
        Log(ANDROID_LOG_WARN, TAG, "%s", "wm size解析失败，使用回退分辨率1080x1920");
    }
} //初始化屏幕分辨率,方向单独放在一个线程了

touch::touch()
{
    InitScreenInfo();
    InitTouchScreenInfo();
    for (const auto &entry: touchScreenInfo.fd)
    {
        threads.emplace_back(&touch::PTScreenEventToFinger, this, entry);
    } //每个疑似触摸屏的节点都调用PTScreenEventToFinger，实际代码无法支持多触摸屏，建立在只有一个触摸屏的前提下
    getScreenOrientationThread = std::thread(&touch::GetScreenOrientation, this);
    usleep(kUinputInitDelayUs);

    openUinputOrThrow();
    configureUinputCapabilities();
    setupUinputDeviceParams();
    createUinputDevice();
    grabPhysicalTouchDevices();

    std::cout << "触摸屏宽高  " << touchScreenInfo.width << "   " << touchScreenInfo.height << std::endl;
    std::cout << "屏幕分辨率  " << screenInfo.width << "   " << screenInfo.height << std::endl;
    Log(ANDROID_LOG_INFO,TAG, "%s",
        std::string("触摸屏宽高: " + std::to_string(touchScreenInfo.width) + "*" + std::to_string(touchScreenInfo.height)).
        c_str());
    Log(ANDROID_LOG_INFO,TAG, "%s",
        std::string("屏幕分辨率: " + std::to_string(screenInfo.width) + "*" + std::to_string(screenInfo.height)).c_str());

    calculateScreenToTouchRatio();
    sendInitialTouchDown();
}

void touch::openUinputOrThrow()
{
    this->uinputFd = open("/dev/uinput", O_RDWR | O_CLOEXEC);
    if (uinputFd < 0)
    {
        Log(ANDROID_LOG_ERROR,TAG, "uinput打开失败");
        std::cout <<"uinput打开失败"<< std::endl;
        // uinput 打开失败前线程已启动，复用析构清理逻辑避免泄漏
        quitFlag.store(true);
        for (const auto &fd: touchScreenInfo.fd) close(fd);
        if (getScreenOrientationThread.joinable()) getScreenOrientationThread.join();
        for (auto &t: threads) if (t.joinable()) t.join();
        throw std::runtime_error("uinput打开失败");
    }
}

void touch::configureUinputCapabilities()
{
    ioctl(uinputFd, (unsigned int) UI_SET_PROPBIT, INPUT_PROP_DIRECT); //设置为直接输入设备
    ioctl(uinputFd, (unsigned int) UI_SET_EVBIT, EV_ABS);
    ioctl(uinputFd, (unsigned int) UI_SET_EVBIT, EV_KEY);
    ioctl(uinputFd, (unsigned int) UI_SET_EVBIT, EV_SYN); //支持的事件类型

    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_TOUCH_MINOR);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_X);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_Y);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_WIDTH_MAJOR);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_TRACKING_ID); //支持的事件

    ioctl(uinputFd, (unsigned int) UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinputFd, (unsigned int) UI_SET_KEYBIT, BTN_TOOL_FINGER); //支持的事件
}

void touch::setupUinputDeviceParams()
{
    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = kVendorId;
    usetup.id.product = kProductId;
    strcpy(usetup.name, "Virtual Touch Screen for muchen"); //驱动信息

    usetup.absmin[ABS_X] = 0;
    usetup.absmax[ABS_X] = kAbsXMax;
    usetup.absmin[ABS_Y] = 0;
    usetup.absmax[ABS_Y] = kAbsYMax;
    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = touchScreenInfo.width;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = touchScreenInfo.height;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_PRESSURE] = kPressureMax; //触摸压力的最大最小值
    usetup.absfuzz[ABS_MT_PRESSURE] = 0;
    usetup.absflat[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_TOUCH_MAJOR] = kTouchMajorMax; //与屏接触面的最大值
    usetup.absmin[ABS_MT_TRACKING_ID] = 0;
    usetup.absmax[ABS_MT_TRACKING_ID] = kAbsTrackingIdMax; //按键码ID累计叠加最大值
}

void touch::createUinputDevice()
{
    write(uinputFd, &usetup, sizeof(usetup)); //将信息写入即将创建的驱动
    ioctl(uinputFd, UI_DEV_CREATE); //创建驱动
}

void touch::grabPhysicalTouchDevices()
{
    for (const auto &entry: touchScreenInfo.fd)
    {
        ioctl(entry, (unsigned int) EVIOCGRAB, 0x1); // 独占输入,只有此进程才能接收到事件 -_-
    }
}

void touch::calculateScreenToTouchRatio()
{
    screenToTouchRatio = (float) (screenInfo.width + screenInfo.height) / (float) (
                             touchScreenInfo.width + touchScreenInfo.height);
    if (screenToTouchRatio < kRatioSnapUpper && screenToTouchRatio > kRatioSnapLower)
    {
        screenToTouchRatio = kRatioSnapUpper;
    }
}

void touch::sendInitialTouchDown()
{
    input_event down{};
    down.type = EV_KEY;
    down.code = BTN_TOUCH;
    down.value = 1;
    write(uinputFd, &down, sizeof(down));
    usleep(100000);
}

touch::~touch()
{
    quitFlag.store(true);
    // 关闭触摸屏 fd 使阻塞在 read() 的线程退出
    for (const auto &fd: touchScreenInfo.fd)
    {
        close(fd);
    }
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    if (getScreenOrientationThread.joinable())
        getScreenOrientationThread.join();
    for (std::thread &item: threads)
    {
        if (item.joinable())
            item.join();
    }
}


void touch::PTScreenEventToFinger(int fd)
{
    input_event ie{};
    int latestSlot{};
    bool frameChanged{false};
    while (!quitFlag.load(std::memory_order_relaxed))
    {
        if (read(fd, &ie, sizeof(ie)) <= 0)
        {
            break;
        }
        {
            if (ie.type == EV_ABS)
            {
                if (ie.code == ABS_MT_SLOT)
                {
                    latestSlot = ie.value;
                    // 越界 slot 标记为 -1，丢弃其后续事件，避免污染 slot 0
                    if (latestSlot < 0 || latestSlot >= kMaxSlots)
                    {
                        latestSlot = -1;
                        continue;
                    }
                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        // absmax[ABS_MT_TRACKING_ID]=65535，原值 114514 超限被内核钳位为 65535，导致所有手指 ID 碰撞。取模落入合法区间。
                        fingers[0][latestSlot].TRACKING_ID = kPhysicalTrackingIdBase + latestSlot;
                    }
                    continue;
                }
                if (latestSlot < 0) continue; // 越界 slot 的事件全部丢弃
                if (ie.code == ABS_MT_TRACKING_ID)
                {
                    if (ie.value == -1)
                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        fingers[0][latestSlot].isDown = false;
                        fingers[0][latestSlot].isUse = false;
                        fingers[0][latestSlot].x = 0;
                        fingers[0][latestSlot].y = 0;
                    } else
                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        fingers[0][latestSlot].isUse = true;
                        fingers[0][latestSlot].isDown = true;
                    }
                    frameChanged = true;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X)
                {
                    std::lock_guard<std::mutex> lock(fingersMutex);
                    fingers[0][latestSlot].x = ie.value;
                    frameChanged = true;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y)
                {
                    std::lock_guard<std::mutex> lock(fingersMutex);
                    fingers[0][latestSlot].y = ie.value;
                    frameChanged = true;
                    continue;
                }
            }
            if (ie.type == EV_SYN)
            {
                if (ie.code == SYN_REPORT)
                {
                    if (frameChanged)
                    {
                        upLoad();
                        frameChanged = false;
                    }
                    continue;
                }
                continue;
            }
        }
    }
}


void touch::upLoad()
{
    touchOBJ snapshot[2][kMaxSlots]{};
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        memcpy(snapshot, fingers, sizeof(fingers));
    }

    // 栈分配，避免每次堆分配
    input_event events[kEventsCapacity]{};
    int count = 0;

    for (int i = 0; i < 2; i++)
    {
        for (int j = 0; j < kMaxSlots; j++)
        {
            appendFingerEvents(events, count, snapshot[i][j]);
        }
    }
    // 协议要求每帧至少一个 SYN_MT_REPORT，否则内核复用上一帧触点导致无法抬起
    if (count == 0)
    {
        events[count++] = {.type = EV_SYN, .code = SYN_MT_REPORT, .value = 0};
    }
    events[count++] = {.type = EV_SYN, .code = SYN_REPORT, .value = 0};

    // 锁住整个写入阶段，防止多线程并发写 uinputFd 导致事件帧交叠
    std::lock_guard<std::mutex> lock(uploadMutex);
    write(uinputFd, events, count * sizeof(input_event));
}

void touch::appendFingerEvents(input_event *events, int &count, const touchOBJ &finger)
{
    if (!finger.isDown) return;
    events[count++] = {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = finger.TRACKING_ID};
    events[count++] = {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = finger.x};
    events[count++] = {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = finger.y};
    events[count++] = {.type = EV_SYN, .code = SYN_MT_REPORT, .value = 0};
}

std::string touch::exec(const std::string &command)
{
    char buf[1024];
    std::string result{};
    // 加 timeout 防止 dumpsys/wm 在 system_server 卡顿时永久挂起导致线程无法退出
    std::string fullCmd = "timeout 5 " + command;
    FILE *pipe = popen(fullCmd.c_str(), "r");

    if (!pipe)
    {
        Log(ANDROID_LOG_WARN,TAG, "%s", std::string("命令 " + command + "执行失败").c_str());
        return "";
    }
    while (fgets(buf, sizeof(buf), pipe))
    {
        result += buf;
    }
    pclose(pipe);
    return result;
}

void touch::GetScreenOrientation()
{
    while (!quitFlag.load(std::memory_order_relaxed))
    {
        this->screenOrientation.store(
            atoi(exec("dumpsys display | grep 'mCurrentOrientation' | cut -d'=' -f2").c_str()),
            std::memory_order_relaxed);

        // 使用更精细的 sleep 循环以响应退出请求
        for (int i = 0; i < kOrientationPollIterations; ++i)
        {
            if (quitFlag.load(std::memory_order_relaxed)) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(kOrientationPollIntervalMs));
        }
    }
}


Vector2 touch::rotatePointx(const Vector2 &pos, const Vector2 &wh, bool reverse) const
{
    Vector2 rotated{pos.x, pos.y};
    switch (screenOrientation.load(std::memory_order_relaxed))
    {
        case 0: // 竖屏
            return rotated;
            break;
        case 1: // 横屏
            rotated.y = reverse ? pos.x : wh.y - pos.x;
            rotated.x = reverse ? wh.x - pos.y : pos.y;
            break;
        case 2: // 反向竖屏
            rotated.x = wh.x - pos.x;
            rotated.y = wh.y - pos.y;
            break;
        case 3: // 反向横屏
            rotated.y = reverse ? wh.y - pos.x : pos.x;
            rotated.x = reverse ? pos.y : wh.x - pos.y;
            break;
    }
    return rotated;
}

Vector2 touch::screenToTouchCoords(const Vector2 &pos) const
{
    Vector2 result = rotatePointx(pos, {screenInfo.width, screenInfo.height}, true);
    result /= screenToTouchRatio;
    return result;
}



int touch::GetIndexById(const int &byId)
{
    for (int i{0}; i < kMaxSlots; i++)
    {
        if (fingers[1][i].id == byId)
        {
            return i;
        }
    }
    return -1;
}

int touch::GetNoUseIndex()
{
    for (int i{0}; i < kMaxSlots; i++)
    {
        if (!fingers[1][i].isUse)
        {
            return i;
        }
    }
    return -1;
}

void touch::touchDown(const int &id, const Vector2 &pos)
{
    Vector2 newPos = screenToTouchCoords(pos);
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        // 检查 id 是否已存在，避免同一 id 占多个槽导致手指泄漏
        int existIndex = GetIndexById(id);
        if (existIndex != -1)
        {
            fingers[1][existIndex].x = (int) newPos.x;
            fingers[1][existIndex].y = (int) newPos.y;
            fingers[1][existIndex].isDown = true;
        } else
        {
            int index = GetNoUseIndex();
            if (index == -1)
            {
                return;
            }
            fingers[1][index].isDown = true;
            fingers[1][index].id = id;
            // 用 index 而非 id 生成 tracking ID：id 可任意大且 (415411+id)%65536 会与物理区间(48978~48987)碰撞；改用 index 后区间固定 22305~22314，与物理不重叠。
            fingers[1][index].TRACKING_ID = kSimulatedTrackingIdBase + index;
            fingers[1][index].x = (int) newPos.x;
            fingers[1][index].y = (int) newPos.y;
            fingers[1][index].isUse = true;
        }
    }
    this->upLoad();
}

void touch::touchMove(const int &id, const Vector2 &pos)
{
    Vector2 newPos = screenToTouchCoords(pos);
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        int index = GetIndexById(id);
        if (index == -1)
        {
            return;
        }
        if (!(fingers[1][index].isUse && fingers[1][index].isDown))
        {
            return;
        }
        // 坐标未变化则跳过，避免高频次重复触摸时的冗余上报
        if (fingers[1][index].x == (int) newPos.x && fingers[1][index].y == (int) newPos.y)
        {
            return;
        }
        fingers[1][index].x = (int) newPos.x;
        fingers[1][index].y = (int) newPos.y;
    }
    this->upLoad();
}

void touch::touchUp(const int &id)
{
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        int index = GetIndexById(id);
        if (index == -1)
        {
            return;
        }
        if (!(fingers[1][index].isDown && fingers[1][index].isUse))
        {
            return;
        }
        fingers[1][index].isDown = false;
        fingers[1][index].isUse = false;
        fingers[1][index].id = 0;
    }
    this->upLoad();
}
