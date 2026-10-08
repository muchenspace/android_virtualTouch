#include "touch/touch.h"

#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>
#include <string>

namespace {
constexpr int kSimulatedTrackingIdBase = 415411 % 65536;  // 模拟触点 tracking ID 基址 → [22195, 22200]
constexpr int kAbsTrackingIdMax = 65535;   // absmax[ABS_MT_TRACKING_ID]
constexpr int kFrameCapacity = 64;         // 单帧事件上限：10 物理槽 × 4 轴 + SYN
constexpr int kVendorId = 0x6c90;
constexpr int kProductId = 0x8fb0;
} // namespace

touch::touch()
{
    InitScreenInfo();
    InitTouchScreenInfo();

    uinputFd = static_cast<int>(syscall(__NR_openat, AT_FDCWD, "/dev/uinput", O_RDWR));//打开uinput
    if (uinputFd < 0)
    {
        printf("[ERROR] 打开 /dev/uinput 失败\n");
        return;
    }
    printf("[INFO] 成功打开 /dev/uinput");
    configureUinputCapabilities();
    setupUinputDeviceParams();
    createUinputDevice();
    grabPhysicalTouchDevices();

    // 设备就绪后再启动线程，避免透传线程写入尚未打开的 fd
    for (const auto &dev: touchScreen)
    {
        threads.emplace_back(&touch::PTScreenEventPassthrough, this, dev);
    }
    getScreenOrientationThread = std::thread(&touch::GetScreenOrientation, this);

    // 等方向线程完成首轮 dumpsys，避免首帧坐标按竖屏方向旋转
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    printf("[INFO] 屏幕初始方向: %d (0:竖屏 1:横屏 2:反向竖屏 3:反向横屏)\n", screenOrientation.load(std::memory_order_relaxed));
}

touch::~touch()
{
    quitFlag.store(true);
    // 关闭触摸屏 fd 使阻塞在 read() 的线程退出
    for (auto &dev: touchScreen)
    {
        if (dev.fd >= 0)
        {
            close(dev.fd);
            printf("[INFO] 释放物理触摸节点 (fd: %d)\n", dev.fd);
            dev.fd = -1;
        }
    }
    if (getScreenOrientationThread.joinable())
        getScreenOrientationThread.join();
    for (std::thread &item: threads)
    {
        if (item.joinable())
            item.join();
    }
    if (uinputFd >= 0)
    {
        syscall(__NR_ioctl, uinputFd, UI_DEV_DESTROY);
        close(uinputFd);
        printf("[INFO] 销毁虚拟触摸屏设备 (fd: %d)\n", uinputFd);
        uinputFd = -1;
    }
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
            break; //有Override size则优先使用
        }
        sscanf(line.c_str(), "Physical size: %dx%d", &screenInfo.width, &screenInfo.height);
    } //找不到Override size就使用Physical size

    printf("[INFO] 屏幕分辨率: %dx%d\n", screenInfo.width, screenInfo.height);
} //初始化屏幕分辨率,方向单独放在一个线程了

void touch::InitTouchScreenInfo()
{
    for (const auto &entry: std::filesystem::directory_iterator("/dev/input/"))
    {
        int fd = static_cast<int>(syscall(__NR_openat, AT_FDCWD, entry.path().c_str(), O_RDONLY));
        if (fd < 0)
        {
            printf("[WARN] 无法打开输入节点: %s\n", entry.path().c_str());
            continue;
        }
        input_absinfo absinfo{};
        if (syscall(__NR_ioctl, fd, EVIOCGABS(ABS_MT_SLOT), &absinfo) < 0)
        {
            close(fd);
            continue;
        }
        // SLOT不为0视为触摸屏
        if (absinfo.maximum > 0)
        {
            printf("[INFO] 发现触摸屏节点: %s\n", entry.path().c_str());
            input_absinfo absX{}, absY{};
            if (syscall(__NR_ioctl, fd, EVIOCGABS(ABS_MT_POSITION_X), &absX) >= 0 && syscall(__NR_ioctl, fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY) >= 0)
            {
                int screenSum = screenInfo.width + screenInfo.height;
                int touchSum = absX.maximum + absY.maximum + 2;
                float ratio = (touchSum > 0) ? (static_cast<float>(screenSum) / static_cast<float>(touchSum)) : 1.0f;
                touchScreen.emplace_back(screen{absX.maximum, absY.maximum, fd, ratio});
                printf("[INFO] 触摸屏坐标范围: X=[0, %d], Y=[0, %d], 映射比例: %.4f\n", absX.maximum, absY.maximum, ratio);
            }
            else
            {
                close(fd);
            }
        }
        else
        {
            close(fd);
        }
    } //遍历/dev/input/下所有eventX，如果ABS_MT_SLOT数量不为0即视为触摸屏
}



// ---------------- uinput 设备 ----------------

void touch::configureUinputCapabilities()
{
    syscall(__NR_ioctl, uinputFd, UI_SET_PROPBIT, INPUT_PROP_DIRECT); //设置为直接输入设备
    syscall(__NR_ioctl, uinputFd, UI_SET_EVBIT, EV_ABS);
    syscall(__NR_ioctl, uinputFd, UI_SET_EVBIT, EV_SYN); //支持的事件类型

    // 声明 ABS_MT_SLOT 即启用 Type B 协议，内核按 absmax+1 建立 slot 表
    syscall(__NR_ioctl, uinputFd, UI_SET_ABSBIT, ABS_MT_SLOT);
    syscall(__NR_ioctl, uinputFd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    syscall(__NR_ioctl, uinputFd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    syscall(__NR_ioctl, uinputFd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID); //支持的事件
}//能力声明

void touch::setupUinputDeviceParams()
{
    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = kVendorId;
    usetup.id.product = kProductId;
    strcpy(usetup.name, "Virtual Touch Screen for muchen"); //驱动信息

    usetup.absmin[ABS_MT_SLOT] = 0;
    usetup.absmax[ABS_MT_SLOT] = kTotalSlotCount - 1; // 声明 16 个 slot：0-9 物理，10-15 模拟
    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = screenInfo.width;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = screenInfo.height;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_TRACKING_ID] = 0;
    usetup.absmax[ABS_MT_TRACKING_ID] = kAbsTrackingIdMax;
}//参数

void touch::createUinputDevice()
{
    if (uinputFd < 0) return;
    if (write(uinputFd, &usetup, sizeof(usetup)) != sizeof(usetup))
    {
        printf("[ERROR] 向 /dev/uinput 写入设备参数失败\n");
        return;
    }
    if (syscall(__NR_ioctl, uinputFd, UI_DEV_CREATE) < 0)
    {
        printf("[ERROR] 创建 uinput 虚拟设备失败\n");
        return;
    }
    printf("[INFO] 虚拟触摸屏创建成功 (\"%s\")\n", usetup.name);
}//创建驱动

void touch::grabPhysicalTouchDevices()
{
    for (const auto &dev: touchScreen)
    {
        if (syscall(__NR_ioctl, dev.fd, EVIOCGRAB, 0x1) < 0)
        {
            printf("[WARN] 独占物理触摸节点 (fd: %d) 失败\n", dev.fd);
        }
        else
        {
            printf("[INFO] 成功独占物理触摸节点 (fd: %d)\n", dev.fd);
        }
    }
}//独占输入

// ---------------- 物理触摸透传 ----------------

bool touch::isForwardedAxis(int code)
{
    // 只透传虚拟设备声明过的轴，其余（压力、接触面等）丢弃
    return code == ABS_MT_SLOT || code == ABS_MT_TRACKING_ID ||
           code == ABS_MT_POSITION_X || code == ABS_MT_POSITION_Y;
}


void touch::PTScreenEventPassthrough(screen dev)
{
    input_event ie{};
    input_event frame[kFrameCapacity]{};
    int count = 1;         // frame[0] 预留给帧首的 ABS_MT_SLOT

    input_absinfo slotInfo{};
    syscall(__NR_ioctl, dev.fd, EVIOCGABS(ABS_MT_SLOT), &slotInfo);
    int currentSlot = slotInfo.value;    // 物理设备当前 slot，首次向内核获取，后续自行记录

    while (!quitFlag.load(std::memory_order_relaxed))
    {
        if (read(dev.fd, &ie, sizeof(ie)) <= 0)
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
                if (ie.code == ABS_MT_POSITION_X || ie.code == ABS_MT_POSITION_Y)
                {
                    ie.value = static_cast<int>(std::round(static_cast<float>(ie.value) * dev.ratio));
                }
                frame[count++] = ie;
            }
            continue;
        }

        if (ie.type == EV_SYN && ie.code == SYN_REPORT)
        {
            frame[0] = {.type = EV_ABS, .code = ABS_MT_SLOT, .value = currentSlot};
            frame[count++] = ie;
            write(uinputFd, frame, count * sizeof(input_event));
            count = 1;
        }
    }
}

// ---------------- 模拟触摸 ----------------

void touch::touchDown(const int &id, const Vector2 &pos)
{
    if (uinputFd < 0) return;

    Vector2 newPos = screenToTouchCoords(pos);

    int index = GetIndexById(id);
    if (index == -1)
    {
        index = GetNoUseIndex();
    }
    if (index == -1)
    {
        printf("[WARN] 模拟触摸槽位已满(已占用 %d 个)，忽略按下请求 (id: %d)\n", kSimulatedSlotCount, id);
        return;
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
    write(uinputFd, frame, 5 * sizeof(input_event));
}

void touch::touchMove(const int &id, const Vector2 &pos)
{
    if (uinputFd < 0) return;

    int index = GetIndexById(id);
    if (index == -1) 
    {
        return;
    }

    Vector2 newPos = screenToTouchCoords(pos);
    fingers[index].x = (int) newPos.x;
    fingers[index].y = (int) newPos.y;

    input_event frame[4] = {
        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = kSimulatedSlotBase + index},
        {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = fingers[index].x},
        {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = fingers[index].y},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
    };
    write(uinputFd, frame, 4 * sizeof(input_event));
}

void touch::touchUp(const int &id)
{
    if (uinputFd < 0) return;

    int index = GetIndexById(id);
    if (index == -1) return;

    fingers[index].isDown = false;
    fingers[index].isUse = false;

    input_event frame[3] = {
        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = kSimulatedSlotBase + index},
        {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1},
        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
    };
    write(uinputFd, frame, 3 * sizeof(input_event));
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
    if (!pipe)
    {
        printf("[WARN] 执行系统命令失败: %s\n", command.c_str());
        return result;
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
        std::string out = exec("dumpsys display | grep 'mCurrentOrientation' | cut -d'=' -f2");
        if (!out.empty())
        {
            screenOrientation.store(atoi(out.c_str()), std::memory_order_relaxed);
        }
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
    return result;
}
