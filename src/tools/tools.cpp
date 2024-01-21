#include "tools.h"
#include <linux/input.h>
#include <fcntl.h>
#include <cstdio>
#include <linux/uinput.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <filesystem>


static int id{};
touchOBJ toucharr[10] = {};

Vector2::Vector2(float x, float y)
{
    this->x =x;
    this->y =y;
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


Vector3::Vector3(int x,int y,int z)
{
    this->x =x;
    this->y =y;
    this->z =z;
}

Vector3::Vector3()
{
    x = 0;
    y = 0;
    z = 0;
}

void touch::InitTouchScreenSize()
{
    input_absinfo absX, absY;
    ioctl(PTScreenfd, EVIOCGABS(ABS_MT_POSITION_X), &absX);
    ioctl(PTScreenfd, EVIOCGABS(ABS_MT_POSITION_Y), &absY);
    touchSize = Vector2(absX.maximum, absY.maximum);
}


void touch::InitScreenResolution()
{
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("wm size", "r"), pclose);

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }
    sscanf(result .c_str(), "Physical size: %fx%f", &screenResolution.x, &screenResolution.y);
}//初始化屏幕分辨率

void touch::InitPTScreenfd()
{

    char name[256] = "";
    int eventCount{};
    std::string eventPath{};
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input/"))
    {
        eventCount++;
    }
    for(int i{0};i<eventCount;i++)
    {
        eventPath = "/dev/input/event" + std::to_string(i);
        int fd = open(eventPath.c_str(),O_RDWR);
        if(fd)
        {
            ioctl(fd, EVIOCGNAME(sizeof(name)), name);
            std::string deviceName(name);
            if (deviceName.find("Touch") != std::string::npos)
            {
                PTScreenfd = fd;
                return;
            }
        }
    }
}
//解析一下这段代码
//1：为什么不在第一个范围for进行判断
//答：因为部分设备可能会有多个触摸输入设备，而真实的物理触摸屏的eventx中，数字偏小，而这个范围for的文件顺序不是这样
//2：为什么要在deviceName中查找Touch
//答：这是一个取巧的办法，因为在实际中，物理触摸屏的名字普遍带有Touch

touch::touch()
{
    InitPTScreenfd();
    InitScreenResolution();
    InitTouchScreenSize();
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(fd<0)
    {
        perror("打开uinput失败！！");
    }

    ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);//设置为直接输入设备
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);//支持的事件类型

    ioctl(fd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);

    ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);//支持的事件

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = 0x6c90;
    usetup.id.product = 0x8fb0;
    strcpy(usetup.name, "Virtual Touch Screen for muchen");//驱动信息

    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = touchSize.x;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = touchSize.y;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_PRESSURE] = 1000;//触摸压力的最大最小值
    usetup.absfuzz[ABS_MT_PRESSURE] = 0;
    usetup.absflat[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_SLOT] = 9; //同时支持最多9个触点
    usetup.absmax[ABS_MT_TOUCH_MAJOR] = 255; //与屏接触面的最大值
    usetup.absmax[ABS_MT_TRACKING_ID] = 65535; //按键码ID累计叠加最大值



    write(fd, &usetup, sizeof(usetup));//将信息写入即将创建的驱动

    ioctl(fd, UI_DEV_CREATE);//创建驱动

    //ioctl(PTScreenfd,EVIOCGRAB,0x1);//独占输入,只有此进程才能接收到事件

    forwardTouchThread = std::thread(&touch::Touch, this);//转发事件线程

    std::cout << "触摸屏宽高" << touchSize.x << touchSize.y << std::endl;
    std::cout<<"屏幕分辨率"<<screenResolution.x<<screenResolution.y<<std::endl;
    screenToTouchRatio = (touchSize.x + touchSize.y) / (screenResolution.x + screenResolution.y);
    sleep(2);
}

touch::~touch()
{
    ioctl(fd, UI_DEV_DESTROY);
    close(fd);
    close(PTScreenfd);
    forwardTouchThread.detach();
}


void touch::emit(int fd,input_event ie)
{
    write(fd, &ie, sizeof(ie));
}

void touch::touch_down(int id, int x, int y)
{
    int touch_x = x * screenToTouchRatio;
    int touch_y = y * screenToTouchRatio;
    toucharr[id].isDown = true;
    toucharr[id].isUse  = false;
    toucharr[id].slot = id;
    toucharr[id].x = touch_x;
    toucharr[id].y = touch_y;
}

void touch::touch_move(int id, int x, int y)
{
    int touch_x = x * screenToTouchRatio;
    int touch_y = y * screenToTouchRatio;
    toucharr[id].x = touch_x;
    toucharr[id].y = touch_y;
    toucharr[id].isNeedMove = true;
}

void touch::touch_up(int id)
{
    toucharr[id].isUp = true;
}

void touch::Touch()
{
    while(true)
    {
        for(int i{};i<10;i++)
        {
            if(toucharr[i].isDown && !toucharr[i].isUse)
            {
                struct input_event down_event[8] = {
                        { .type = EV_ABS, .code = ABS_MT_SLOT, .value = toucharr[i].slot },
                        { .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = 114514 + id },
                        { .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = toucharr[i].x },
                        { .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = toucharr[i].y },
                        { .type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0x00000006 },
                        { .type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0x000003e8 },
                        { .type = EV_KEY, .code = BTN_TOUCH, .value = 1 }, // DOWN
                        { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }
                };
                emit(fd,down_event[0]);
                emit(fd,down_event[1]);
                emit(fd,down_event[2]);
                emit(fd,down_event[3]);
                emit(fd,down_event[4]);
                emit(fd,down_event[5]);
                emit(fd,down_event[6]);
                emit(fd,down_event[7]);
                id++;
                toucharr[i].isUse = true;
            }
            if(toucharr[i].isUp && toucharr[i].isUse)
            {
                struct input_event up_event[6] = {
                        { .type = EV_ABS, .code = ABS_MT_SLOT, .value = toucharr[i].slot },
                        { .type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0x00000000 },
                        { .type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0x00000000 },
                        { .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
                        { .type = EV_KEY, .code = BTN_TOUCH, .value = 0 }, // UP
                        { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }
                };
                emit(fd,up_event[0]);
                emit(fd,up_event[1]);
                emit(fd,up_event[2]);
                emit(fd,up_event[3]);
                emit(fd,up_event[4]);
                emit(fd,up_event[5]);
                toucharr[i].isUp = false;
                toucharr[i].isUse = false;
                toucharr[i].isDown = false;
            }
            if(toucharr[i].isNeedMove)
            {
                struct input_event move_event[4] = {
                        { .type = EV_ABS, .code = ABS_MT_SLOT, .value = toucharr[i].slot },
                        { .type = EV_ABS, .code = ABS_MT_POSITION_X, .value = toucharr[i].x },
                        { .type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = toucharr[i].y },
                        { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }
                };
                emit(fd,move_event[0]);
                emit(fd,move_event[1]);
                emit(fd,move_event[2]);
                emit(fd,move_event[3]);
                toucharr[i].isNeedMove = false;
            }

        }
//        if(read(PTScreenfd,&PTScreenEvent,sizeof (struct input_event)))
//        {
//            emit(fd,PTScreenEvent);
//        }
    }
//将物理触摸屏的信息转发到我们创建的虚拟输入设备
//ps -- 因为有两个触摸输入设备同时发送事件大概率会冲突
}




