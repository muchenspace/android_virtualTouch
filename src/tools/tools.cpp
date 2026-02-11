#include "tools.h"
#include <linux/input.h>
#include <fcntl.h>
#include <cstdio>
#include <linux/uinput.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <filesystem>
#include <sstream>
#include <vector>
#include <android/log.h>

#define TAG "muchen"
#define Log __android_log_print

Vector2::Vector2(int x, int y)
{
    this->x = (float) x;
    this->y = (float) y;
}

Vector2::Vector2(float x, float y)
{
    this->x = x;
    this->y = y;
}

Vector2::Vector2()
{
    x = 0;
    y = 0;
}

Vector2::Vector2(Vector2 &va)
{
    this->x = va.x;
    this->y = va.y;
}

Vector2 &Vector2::operator=(const Vector2 &other)
{
    // 防止自赋值
    if (this != &other)
    {
        this->x = other.x;
        this->y = other.y;
    }
    return *this;
}

void touch::InitTouchScreenInfo()
{
    for (const auto &entry: std::filesystem::directory_iterator("/dev/input/"))
    {
        if (entry.path().filename().string().rfind("event", 0) != 0) continue; // 仅处理 event* 文件

        int fd = open(entry.path().c_str(), O_RDWR);
        if (fd < 0)
        {
            Log(ANDROID_LOG_WARN,TAG, "%s", std::string("打开 " + entry.path().string() + "失败").c_str());
            std::cout<<std::string("打开 " + entry.path().string() + "失败").c_str()<<std::endl;
            continue;
        }
        input_absinfo absinfo{};
        ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);

        // 只要有 ABS_MT_SLOT 且数量合理 (>1)，就视为触摸屏
        if (absinfo.maximum > 0 && absinfo.maximum < 10)
        {
            Log(ANDROID_LOG_INFO,TAG, "%s", std::string("找到疑似触摸节点: " + entry.path().string()).c_str());
            std::cout << std::string("找到疑似触摸节点: " + entry.path().string()).c_str() << std::endl;
            this->touchScreenInfo.fd.emplace_back(open(entry.path().c_str(), O_RDWR)); //加入到节点组中

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
        }
        close(fd);
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
} //初始化屏幕分辨率,方向单独放在一个线程了

touch::touch()
{
    InitScreenInfo();
    InitTouchScreenInfo();
    for (const auto &entry: touchScreenInfo.fd)
    {
        threads.emplace_back(&touch::PTScreenEventToFinger, this, entry);
    } //每个疑似触摸屏的节点都调用PTScreenEventToFinger，建立在只有一个触摸屏的前提下
    GetScreenorientationThread = std::thread(&touch::GetScrorientation, this);
    sleep(2);

    this->uinputFd = open("/dev/uinput", O_RDWR);
    if (uinputFd < 0)
    {
        Log(ANDROID_LOG_ERROR,TAG, "uinput打开失败");
        std::cout <<"uinput打开失败"<< std::endl;
        throw std::runtime_error("uinput打开失败");
    }

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


    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = 0x6c90;
    usetup.id.product = 0x8fb0;
    strcpy(usetup.name, "Virtual Touch Screen for muchen"); //驱动信息

    usetup.absmin[ABS_X] = 0;
    usetup.absmax[ABS_X] = 1599;
    usetup.absmin[ABS_Y] = 0;
    usetup.absmax[ABS_Y] = 2559;
    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = touchScreenInfo.width;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = touchScreenInfo.height;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_PRESSURE] = 1000; //触摸压力的最大最小值
    usetup.absfuzz[ABS_MT_PRESSURE] = 0;
    usetup.absflat[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_TOUCH_MAJOR] = 255; //与屏接触面的最大值
    usetup.absmin[ABS_MT_TRACKING_ID] = 0;
    usetup.absmax[ABS_MT_TRACKING_ID] = 65535; //按键码ID累计叠加最大值

    write(uinputFd, &usetup, sizeof(usetup)); //将信息写入即将创建的驱动

    ioctl(uinputFd, UI_DEV_CREATE); //创建驱动

    for (const auto &entry: touchScreenInfo.fd)
    {
        ioctl(entry, (unsigned int) EVIOCGRAB, 0x1); // 独占输入,只有此进程才能接收到事件 -_-
    }

    std::cout << "触摸屏宽高  " << touchScreenInfo.width << "   " << touchScreenInfo.height << std::endl;
    std::cout << "屏幕分辨率  " << screenInfo.width << "   " << screenInfo.height << std::endl;
    Log(ANDROID_LOG_INFO,TAG, "%s",
        std::string("触摸屏宽高: " + std::to_string(touchScreenInfo.width) + "*" + std::to_string(touchScreenInfo.height)).
        c_str());
    Log(ANDROID_LOG_INFO,TAG, "%s",
        std::string("屏幕分辨率: " + std::to_string(screenInfo.width) + "*" + std::to_string(screenInfo.height)).c_str());
    screenToTouchRatio = (float) (screenInfo.width + screenInfo.height) / (float) (
                             touchScreenInfo.width + touchScreenInfo.height);
    if (screenToTouchRatio < 1 && screenToTouchRatio > 0.9)
    {
        screenToTouchRatio = 1;
    }
    input_event down{};
    down.type = EV_KEY;
    down.code = BTN_TOUCH;
    down.value = 1;
    write(uinputFd, &down, sizeof(down));
    sleep(2);
}

touch::~touch()
{
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    GetScreenorientationThread.detach();
    for (std::thread &item: threads)
    {
        item.detach();
    } //直接detach线程即可，touch析构程序也退出
}


void touch::PTScreenEventToFinger(int fd)
{
    input_event ie{};
    int latestSlot{};
    while (true)
    {
        if (read(fd, &ie, sizeof(ie)) <= 0)
        {
            // 处理 read 错误或 EOF
            break;
        }
        {
            if (ie.type == EV_ABS)
            {
                if (ie.code == ABS_MT_SLOT)
                {
                    latestSlot = ie.value;
                    // 边界检查：防止 slot 越界
                    if (latestSlot < 0 || latestSlot >= 10)
                    {
                        latestSlot = 0; // Fallback or handle error
                        continue;
                    }

                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        Fingers[0][latestSlot].TRACKING_ID = 114514 + latestSlot;
                    }
                    continue;
                }
                if (ie.code == ABS_MT_TRACKING_ID)
                {
                    if (ie.value == -1)
                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        Fingers[0][latestSlot].isDown = false;
                        Fingers[0][latestSlot].isUse = false;
                        Fingers[0][latestSlot].x = 0;
                        Fingers[0][latestSlot].y = 0;
                    } else
                    {
                        std::lock_guard<std::mutex> lock(fingersMutex);
                        Fingers[0][latestSlot].isUse = true;
                        Fingers[0][latestSlot].isDown = true;
                    }
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X)
                {
                    std::lock_guard<std::mutex> lock(fingersMutex);
                    Fingers[0][latestSlot].x = ie.value;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y)
                {
                    std::lock_guard<std::mutex> lock(fingersMutex);
                    Fingers[0][latestSlot].y = ie.value;
                    continue;
                }
            }
            if (ie.type == EV_SYN)
            {
                if (ie.code == SYN_REPORT)
                {
                    auto callBack = monitorCallBack.load(std::memory_order_relaxed);
                    if (callBack != nullptr)
                    {
                        bool isDown{};
                        Vector2 pos{};
                        {
                            std::lock_guard<std::mutex> lock(fingersMutex);
                            isDown = Fingers[0][latestSlot].isDown;
                            if (isDown)
                            {
                                pos = {Fingers[0][latestSlot].x, Fingers[0][latestSlot].y};
                            }
                        }
                        if (isDown)
                        {
                            Vector2 newPos = rotatePointx(pos, {screenInfo.width, screenInfo.height}, false);
                            newPos.x *= this->screenToTouchRatio;
                            newPos.y *= this->screenToTouchRatio;
                            callBack(latestSlot, newPos, 0);
                        }
                        if (!isDown)
                        {
                            callBack(latestSlot, {0, 0}, 1);
                        }
                    }
                    upLoad();
                    continue;
                }
                continue;
            }
        }
    }
}


void touch::upLoad()
{
    touchOBJ snapshot[2][10]{};
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        for (int i = 0; i < 2; i++)
        {
            for (int j = 0; j < 10; j++)
            {
                snapshot[i][j] = Fingers[i][j];
            }
        }
    }
    std::vector<input_event> events{};
    for (auto &fingers: snapshot)
    {
        for (auto &finger: fingers)
        {
            if (finger.isDown)
            {
                input_event down_events[]
                {
                    {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = finger.TRACKING_ID},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = finger.x},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = finger.y},
                    {.type = EV_SYN, .code = SYN_MT_REPORT, .value = 0},
                };
                int arrCount = sizeof(down_events) / sizeof(down_events[0]);
                events.insert(events.end(), down_events, down_events + arrCount);
            }
        }
    }
    input_event touchEnd{};
    touchEnd.type = EV_SYN;
    touchEnd.code = SYN_MT_REPORT;
    touchEnd.value = 0;
    events.push_back(touchEnd);
    input_event end{};
    end.type = EV_SYN;
    end.code = SYN_REPORT;
    end.value = 0;
    events.push_back(end);
    for (const auto &event: events)
    {
        write(uinputFd, &event, sizeof(event));
    }
    events.clear();
}

std::string touch::exec(const std::string &command)
{
    char buf[1024];
    std::string result{};
    FILE *pipe = popen(command.c_str(), "r");

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

void touch::GetScrorientation()
{
    while (true)
    {
        this->screenOrientation.store(
            atoi(exec("dumpsys display | grep 'mCurrentOrientation' | cut -d'=' -f2").c_str()),
            std::memory_order_relaxed);

        // 使用更精细的 sleep 循环以响应退出请求
        for (int i = 0; i < 50; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

int touch::GetindexById(const int &byId)
{
    for (int i{0}; i < 10; i++)
    {
        if (Fingers[1][i].id == byId)
        {
            return i;
        }
    }
    return -1;
}

int touch::GetNoUseIndex()
{
    for (int i{0}; i < 10; i++)
    {
        if (!Fingers[1][i].isUse)
        {
            return i;
        }
    }
    return -1;
}

void touch::touchDown(const int &id, const Vector2 &pos)
{
    Vector2 newPos = rotatePointx(pos, {screenInfo.width, screenInfo.height}, true);
    newPos.x /= this->screenToTouchRatio;
    newPos.y /= this->screenToTouchRatio;
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        int index = GetNoUseIndex();
        if (index == -1)
        {
            return;
        }
        if (Fingers[1][index].isDown && Fingers[1][index].isUse)
        {
            return;
        }
        Fingers[1][index].isDown = true;
        Fingers[1][index].id = id;
        Fingers[1][index].TRACKING_ID = 415411 + id;
        Fingers[1][index].x = (int) newPos.x;
        Fingers[1][index].y = (int) newPos.y;
        Fingers[1][index].isUse = true;
    }
    this->upLoad();
}

void touch::touchMove(const int &id, const Vector2 &pos)
{
    Vector2 newPos = rotatePointx(pos, {screenInfo.width, screenInfo.height}, true);
    newPos.x /= this->screenToTouchRatio;
    newPos.y /= this->screenToTouchRatio;
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        int index = GetindexById(id);
        if (index == -1)
        {
            return;
        }
        if (!(Fingers[1][index].isUse && Fingers[1][index].isDown))
        {
            return;
        }
        Fingers[1][index].x = (int) newPos.x;
        Fingers[1][index].y = (int) newPos.y;
    }
    this->upLoad();
}

void touch::touchUp(const int &id)
{
    {
        std::lock_guard<std::mutex> lock(fingersMutex);
        int index = GetindexById(id);
        if (index == -1)
        {
            return;
        }
        if (!(Fingers[1][index].isDown && Fingers[1][index].isUse))
        {
            return;
        }
        Fingers[1][index].isDown = false;
        Fingers[1][index].isUse = false;
        Fingers[1][index].id = 0;
    }
    this->upLoad();
}

void touch::monitorEvent(void (*callBack)(int, Vector2, int))
{
    monitorCallBack.store(callBack, std::memory_order_relaxed);
}

