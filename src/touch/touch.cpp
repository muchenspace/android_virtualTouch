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
#include <string>

namespace {
constexpr int kSimulatedTrackingIdBase = 415411 % 65536;  // 模拟触点 tracking ID 基址 → [22195, 22200]
constexpr int kAbsTrackingIdMax = 65535;   // absmax[ABS_MT_TRACKING_ID]
constexpr int kOrientationInitDelayMs = 2000; // 构造函数等待方向线程首轮 dumpsys 完成
constexpr int kFrameCapacity = 64;         // 单帧事件上限：10 物理槽 × 4 轴 + SYN
constexpr int kVendorId = 0x6c90;
constexpr int kProductId = 0x8fb0;
} // namespace

touch::touch()
{
    InitScreenInfo();
    InitTouchScreenInfo();

    openUinput();
    configureUinputCapabilities();
    setupUinputDeviceParams();
    createUinputDevice();
    grabPhysicalTouchDevices();
    calculateScreenToTouchRatio();

    // 设备就绪后再启动线程，避免透传线程写入尚未打开的 fd
    for (const auto &entry: touchScreenInfo.fd)
    {
        threads.emplace_back(&touch::PTScreenEventPassthrough, this, entry);
    } //每个疑似触摸屏的节点都调用PTScreenEventPassthrough，实际代码无法支持多触摸屏，建立在只有一个触摸屏的前提下
    getScreenOrientationThread = std::thread(&touch::GetScreenOrientation, this);

    // 等方向线程完成首轮 dumpsys，避免首帧坐标按竖屏方向旋转
    std::this_thread::sleep_for(std::chrono::milliseconds(kOrientationInitDelayMs));
}

touch::~touch()
{
    quitFlag.store(true);
    // 关闭触摸屏 fd 使阻塞在 read() 的线程退出
    for (const auto &fd: touchScreenInfo.fd)
    {
        close(fd);
    }
    if (getScreenOrientationThread.joinable())
        getScreenOrientationThread.join();
    for (std::thread &item: threads)
    {
        if (item.joinable())
            item.join();
    }
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
}

// ---------------- 初始化 ----------------

void touch::InitScreenInfo()
{
    std::istringstream ScreenSizeStream(exec("wm size"));
    std::string line{};

    while (std::getline(ScreenSizeStream, line))
    {
        if (sscanf(line.c_str(), "Override size: %dx%d", &screenInfo.width, &screenInfo.height) == 2)
        {
            return; //有Override size则优先使用
        }
        sscanf(line.c_str(), "Physical size: %dx%d", &screenInfo.width, &screenInfo.height);
    } //找不到Override size就使用Physical size
} //初始化屏幕分辨率,方向单独放在一个线程了

void touch::InitTouchScreenInfo()
{
    for (const auto &entry: std::filesystem::directory_iterator("/dev/input/"))
    {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue; // 仅处理 event* 文件

        int fd = open(entry.path().c_str(), O_RDWR | O_CLOEXEC);
        input_absinfo absinfo{};
        ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);

        // 有 ABS_MT_SLOT 且槽数落在物理分区内，才视为触摸屏（非触摸节点 ioctl 失败，maximum 保持 0）
        if (absinfo.maximum > 0 && absinfo.maximum < kMaxPhysicalSlots)
        {
            touchScreenInfo.fd.emplace_back(fd); //直接使用已打开的fd，避免重复open

            if (touchScreenInfo.width == 0 || touchScreenInfo.height == 0) //有多个节点的情况下，只使用第一个节点的信息
            {
                input_absinfo absX{}, absY{};
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX);
                ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY);
                touchScreenInfo.width = absX.maximum;
                touchScreenInfo.height = absY.maximum;
            }
        } else
        {
            close(fd); // 非触摸屏节点，关闭
        }
    } //遍历/dev/input/下所有eventX，如果ABS_MT_SLOT数量在物理分区内就视为触摸屏
}

// ---------------- uinput 设备 ----------------

void touch::openUinput()
{
    uinputFd = open("/dev/uinput", O_RDWR | O_CLOEXEC);
}

void touch::configureUinputCapabilities()
{
    ioctl(uinputFd, (unsigned int) UI_SET_PROPBIT, INPUT_PROP_DIRECT); //设置为直接输入设备
    ioctl(uinputFd, (unsigned int) UI_SET_EVBIT, EV_ABS);
    ioctl(uinputFd, (unsigned int) UI_SET_EVBIT, EV_SYN); //支持的事件类型

    // 声明 ABS_MT_SLOT 即启用 Type B 协议，内核按 absmax+1 建立 slot 表
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinputFd, (unsigned int) UI_SET_ABSBIT, ABS_MT_TRACKING_ID); //支持的事件
}

void touch::setupUinputDeviceParams()
{
    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = kVendorId;
    usetup.id.product = kProductId;
    strcpy(usetup.name, "Virtual Touch Screen for muchen"); //驱动信息

    usetup.absmin[ABS_MT_SLOT] = 0;
    usetup.absmax[ABS_MT_SLOT] = kTotalSlotCount - 1; // 声明 16 个 slot：0-9 物理，10-15 模拟
    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = touchScreenInfo.width;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = touchScreenInfo.height;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_TRACKING_ID] = 0;
    usetup.absmax[ABS_MT_TRACKING_ID] = kAbsTrackingIdMax;
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
    screenToTouchRatio = (float) (screenInfo.width + screenInfo.height) /
                         (float) (touchScreenInfo.width + touchScreenInfo.height);
}

// ---------------- 物理触摸透传 ----------------

bool touch::isForwardedAxis(int code)
{
    // 只透传虚拟设备声明过的轴，其余（压力、接触面等）丢弃
    return code == ABS_MT_SLOT || code == ABS_MT_TRACKING_ID ||
           code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y;
}

void touch::writeFrame(const input_event *events, int count) const
{
    write(uinputFd, events, count * sizeof(input_event));
}

void touch::PTScreenEventPassthrough(int fd)
{
    input_event ie{};
    input_event frame[kFrameCapacity]{};
    int count = 1;         // frame[0] 预留给帧首的 ABS_MT_SLOT

    // 以物理设备当前 slot 为起点，避免启动瞬间（已有手指按下）首帧落到错误槽位
    input_absinfo slotInfo{};
    ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &slotInfo);
    int currentSlot = slotInfo.value;    // 物理设备当前 slot
    int frameStartSlot = slotInfo.value; // 本帧起始时物理设备的 slot

    while (!quitFlag.load(std::memory_order_relaxed))
    {
        if (read(fd, &ie, sizeof(ie)) <= 0)
        {
            break;
        }

        if (ie.type == EV_ABS)
        {
            if (ie.code == ABS_MT_SLOT)
            {
                currentSlot = ie.value;
            }
            if (isForwardedAxis(ie.code))
            {
                frame[count++] = ie;
            }
            continue;
        }

        if (ie.type == EV_SYN && ie.code == SYN_REPORT)
        {
            if (count > 1)
            {
                // 帧首显式声明 slot：内核的 mt->slot 是设备级状态，会被模拟线程改写，
                // 物理帧若不声明就会落到模拟线程留下的槽位上
                frame[0] = {.type = EV_ABS, .code = ABS_MT_SLOT, .value = frameStartSlot};
                frame[count++] = ie;
                writeFrame(frame, count);
                count = 1;
                frameStartSlot = currentSlot; // 下一帧的起始 slot
            }
            continue;
        }
    }
}

// ---------------- 模拟触摸 ----------------

void touch::touchDown(const int &id, const Vector2 &pos)
{
    Vector2 newPos = screenToTouchCoords(pos);
    std::lock_guard<std::mutex> lock(fingersMutex);

    int index = GetIndexById(id);
    if (index == -1)
    {
        index = GetNoUseIndex();
    }

    fingers[index].id = id;
    fingers[index].x = (int) newPos.x;
    fingers[index].y = (int) newPos.y;
    fingers[index].TRACKING_ID = kSimulatedTrackingIdBase + index;
    fingers[index].isUse = true;
    fingers[index].isDown = true;

    input_event frame[5] = {
        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = kSimulatedSlotBase + index},
        {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = fingers[index].TRACKING_ID},
        {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = fingers[index].x},
        {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = fingers[index].y},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
    };
    writeFrame(frame, 5);
}

void touch::touchMove(const int &id, const Vector2 &pos)
{
    Vector2 newPos = screenToTouchCoords(pos);
    std::lock_guard<std::mutex> lock(fingersMutex);

    int index = GetIndexById(id);
    fingers[index].x = (int) newPos.x;
    fingers[index].y = (int) newPos.y;

    input_event frame[4] = {
        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = kSimulatedSlotBase + index},
        {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = fingers[index].x},
        {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = fingers[index].y},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
    };
    writeFrame(frame, 4);
}

void touch::touchUp(const int &id)
{
    std::lock_guard<std::mutex> lock(fingersMutex);

    int index = GetIndexById(id);
    fingers[index].isDown = false;
    fingers[index].isUse = false;

    input_event frame[3] = {
        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = kSimulatedSlotBase + index},
        {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
    };
    writeFrame(frame, 3);
}

int touch::GetIndexById(const int &byId)
{
    for (int i{0}; i < kSimulatedSlotCount; i++)
    {
        if (fingers[i].isUse && fingers[i].id == byId)
        {
            return i;
        }
    }
    return -1;
}

int touch::GetNoUseIndex()
{
    for (int i{0}; i < kSimulatedSlotCount; i++)
    {
        if (!fingers[i].isUse)
        {
            return i;
        }
    }
    return -1;
}

// ---------------- 方向获取与坐标换算 ----------------

std::string touch::exec(const std::string &command)
{
    char buf[1024];
    std::string result{};
    FILE *pipe = popen(command.c_str(), "r");

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
        screenOrientation.store(
            atoi(exec("dumpsys display | grep 'mCurrentOrientation' | cut -d'=' -f2").c_str()),
            std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

Vector2 touch::rotatePointx(const Vector2 &pos, const Vector2 &wh, bool reverse) const
{
    Vector2 rotated{pos.x, pos.y};
    switch (screenOrientation.load(std::memory_order_relaxed))
    {
        case 0: // 竖屏
            return rotated;
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
