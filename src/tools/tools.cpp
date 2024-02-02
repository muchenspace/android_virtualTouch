#include "tools.h"
#include <linux/input.h>
#include <fcntl.h>
#include <cstdio>
#include <linux/uinput.h>
#include <unistd.h>
#include <iostream>
#include <thread>
#include <filesystem>
#include <vector>

Vector2::Vector2(int x, int y)
{
    this->x =(float)x;
    this->y =(float)y;
}
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

void touch::InitTouchScreenInfo()
{
    for (const auto& entry : std::filesystem::directory_iterator("/dev/input/"))
    {
        int fd = open(entry.path().c_str(),O_RDWR);
        input_absinfo absinfo;
        ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);

        if(absinfo.maximum == 9)
        {
            this->touchScreenInfo.fd = open(entry.path().c_str(),O_RDWR);
            close(fd);
            break;
        }
    }//遍历/dev/input/下所有eventX，如果ABS_MT_SLOT为9(即最大支持10点触控)就视为物理触摸屏
    input_absinfo absX, absY;
    ioctl(touchScreenInfo.fd, EVIOCGABS(ABS_MT_POSITION_X), &absX);
    ioctl(touchScreenInfo.fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY);
    this->touchScreenInfo.width = absX.maximum;
    this->touchScreenInfo.height = absY.maximum;
}

void touch::InitScreenInfo()
{
    std::string window_size = exec("wm size");
    sscanf(window_size.c_str(),"Physical size: %dx%d",&this->screenInfo.width, &this->screenInfo.height);
}//初始化屏幕分辨率,方向单独放在一个线程了

touch::touch()
{
    InitScreenInfo();
    InitTouchScreenInfo();
    GetScreenorientationThread = std::thread(&touch::GetScrorientation, this);
    sleep(2);
    PTScreenEventToFingerThread = std::thread(&touch::PTScreenEventToFinger, this);
    this->uinputFd = open("/dev/uinput", O_RDWR);
    if(uinputFd < 0)
    {
        perror("打开uinput失败！！");
    }

    ioctl(uinputFd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);//设置为直接输入设备
    ioctl(uinputFd, UI_SET_EVBIT, EV_ABS);
    ioctl(uinputFd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinputFd, UI_SET_EVBIT, EV_SYN);//支持的事件类型

    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_SLOT);
    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
    ioctl(uinputFd, UI_SET_ABSBIT, ABS_MT_PRESSURE);

    ioctl(uinputFd, UI_SET_KEYBIT, BTN_TOUCH);
    ioctl(uinputFd, UI_SET_KEYBIT, BTN_TOOL_FINGER);//支持的事件

    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_SPI;
    usetup.id.vendor = 0x6c90;
    usetup.id.product = 0x8fb0;
    strcpy(usetup.name, "Virtual Touch Screen for muchen");//驱动信息

    usetup.absmin[ABS_MT_POSITION_X] = 0;
    usetup.absmax[ABS_MT_POSITION_X] = touchScreenInfo.width;
    usetup.absfuzz[ABS_MT_POSITION_X] = 0;
    usetup.absflat[ABS_MT_POSITION_X] = 0;
    usetup.absmin[ABS_MT_POSITION_Y] = 0;
    usetup.absmax[ABS_MT_POSITION_Y] = touchScreenInfo.height;
    usetup.absfuzz[ABS_MT_POSITION_Y] = 0;
    usetup.absflat[ABS_MT_POSITION_Y] = 0;
    usetup.absmin[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_PRESSURE] = 1000;//触摸压力的最大最小值
    usetup.absfuzz[ABS_MT_PRESSURE] = 0;
    usetup.absflat[ABS_MT_PRESSURE] = 0;
    usetup.absmax[ABS_MT_SLOT] = 9; //同时支持最多9个触点
    usetup.absmax[ABS_MT_TOUCH_MAJOR] = 255; //与屏接触面的最大值
    usetup.absmax[ABS_MT_TRACKING_ID] = 65535; //按键码ID累计叠加最大值

    write(uinputFd, &usetup, sizeof(usetup));//将信息写入即将创建的驱动

    ioctl(uinputFd, UI_DEV_CREATE);//创建驱动

    ioctl(this->touchScreenInfo.fd,EVIOCGRAB,0x1);//独占输入,只有此进程才能接收到事件 --

    std::cout << "触摸屏宽高  " << touchScreenInfo.width <<"   "<< touchScreenInfo.height << std::endl;
    std::cout<<"屏幕分辨率  "<<screenInfo.width<<"   "<<screenInfo.height<<std::endl;
    screenToTouchRatio = (float)(touchScreenInfo.width + touchScreenInfo.height) / (float)(screenInfo.width + screenInfo.height);
    if(screenToTouchRatio < 1 && screenToTouchRatio > 0.9)
    {
        screenToTouchRatio = 1;
    }
    sleep(2);
}

touch::~touch()
{
    ioctl(uinputFd, UI_DEV_DESTROY);
    close(uinputFd);
    PTScreenEventToFingerThread.detach();
    GetScreenorientationThread.detach();
}

void touch::emit(int fd,input_event ie)
{
    write(fd, &ie, sizeof(ie));
}

void touch::PTScreenEventToFinger()
{
    input_event touchEvent{};
    std::vector<input_event>touchEventS;//储存从两次syn事件中间的事件
    input_absinfo absinfo{};
    ioctl(touchScreenInfo.fd, EVIOCGABS(ABS_MT_SLOT), &absinfo);//获取slot信息
    int Pyslot{};
    while(true)
    {
        read(touchScreenInfo.fd,&touchEvent,sizeof (touchEvent));
        if(touchEvent.code == ABS_MT_SLOT)
        {
            absinfo.value = touchEvent.value;
            continue;
        }
        Pyslot = absinfo.value;
        if(Fingers[absinfo.value].isUse && Fingers[absinfo.value].PySlot == -1)//如果slot被占用且是被虚拟设备占用才会重新分配
        {
            Fingers[this->GetNoUseIndex()].PySlot = absinfo.value;
            absinfo.value = this->GetNoUseIndex();
        }
        else
        {
            if(absinfo.value != 0)//是0就不需要了
            {
                int index = this->GetPyFinger(absinfo.value);
                if(index != -1)
                {
                    absinfo.value = index;
                }//判断一下这个slot是否已经分配了
            }
        }
        Fingers[absinfo.value].PySlot = Pyslot;
        if (touchEvent.type == EV_SYN && touchEvent.code == SYN_REPORT)
        {
            for(auto& event:touchEventS)
            {
                switch (event.type)
                {
                    case EV_ABS:
                        if(event.code == ABS_MT_TRACKING_ID)
                        {
                            if(event.value > 0)
                            {
                                Fingers[absinfo.value].isNeedDown = true;
                            }
                            else
                            {
                                Fingers[absinfo.value].isNeedUp = true;
                            }
                            Fingers[absinfo.value].TRACKING_ID = this->GetTRACKING_ID();
                            continue;
                        }
                        else if(event.code == ABS_MT_POSITION_X)
                        {
                            if(Fingers[absinfo.value].x)
                            {
                                Fingers[absinfo.value].isNeedMove = true;
                            }
                            Fingers[absinfo.value].x = event.value;
                            continue;
                        }
                        else if(event.code == ABS_MT_POSITION_Y)
                        {
                            if(Fingers[absinfo.value].y)
                            {
                                Fingers[absinfo.value].isNeedMove = true;
                            }
                            Fingers[absinfo.value].y = event.value;
                            continue;
                        }
                        else
                        {
                            continue;
                        }
                        break;
                    case EV_KEY:
                        if(event.code == BTN_TOUCH)
                        {
                            if(event.value == 1)
                            {
                                if(this->IsNoFirstDown())
                                {
                                    Fingers[absinfo.value].IsFirstDown = true;
                                }
                            }//按下
                            continue;
                        }
                        break;
                    default:
                        break;
                }
            }
            touchEventS.clear();
            upLoad();
        }
        else
        {
            touchEventS.push_back(touchEvent);
        }
    }
}

std::string touch::exec(std::string command)
{
    char buffer[128];
    std::string result = "";

    FILE* pipe = popen(command.c_str(), "r");
    while (!feof(pipe))
    {
        if (fgets(buffer, 128, pipe) != nullptr)
        {
            result += buffer;
        }
    }
    pclose(pipe);
    return result;
}

int touch::GetPyFinger(int slot)
{
    for(int i{};i<10;i++)
    {
        if(Fingers[i].PySlot == slot)
        {
            return i;
        }
    }
    return -1;
}

void touch::GetScrorientation()
{
    while(true)
    {
        this->screenInfo.orientation = atoi(exec("dumpsys display | grep 'mCurrentOrientation' | cut -d'=' -f2").c_str());
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

Vector2 touch::rotatePointx(Vector2 pos, const Vector2 wh, bool reverse)
{
    if (this->screenInfo.orientation == 0)
    {
        return pos;
    }
    Vector2 xy(pos.x, pos.y);
    if (this->screenInfo.orientation == 3)
    {
        xy.x = reverse ? pos.y : (float) wh.y - pos.y;
        xy.y = reverse ? (float) wh.y - pos.x : pos.x;
    }
    else if (this->screenInfo.orientation == 2)
    {
        xy.x = reverse ? (float) wh.x - pos.x : (float) wh.x - pos.x;
        xy.y = reverse ? (float) wh.y - pos.y : (float) wh.y - pos.y;
    }
    else if (this->screenInfo.orientation == 1)
    {
        xy.x = reverse ? (float) wh.x - pos.x : pos.y;
        xy.y = reverse ? pos.y : (float) wh.x - pos.x;
    }
    return xy;
}

void touch::upLoad()
{
    int index{0};
    std::vector<input_event>events{};
    for(auto& finger:Fingers)
    {
        if (finger.isNeedDown && !finger.isUse)
        {
            if(finger.IsFirstDown)
            {
                struct input_event firstDown_event[] = {
                        {.type = EV_ABS, .code = ABS_MT_SLOT, .value = index},
                        {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = finger.TRACKING_ID},
                        {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = finger.x},
                        {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = finger.y},
                        {.type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0x00000006},
                        {.type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0x000003e8},
                        {.type = EV_KEY, .code = BTN_TOUCH, .value = 1}, // DOWN
                        {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
                };
                int arrCount = sizeof (firstDown_event) / sizeof (firstDown_event[0]);
                events.insert(events.end(), firstDown_event, firstDown_event + arrCount);
                finger.isUse = true;
                finger.isDown = true;
                continue;
            }
            struct input_event down_event[7] = {
                    {.type = EV_ABS, .code = ABS_MT_SLOT, .value = index},
                    {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = finger.TRACKING_ID},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = finger.x},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = finger.y},
                    {.type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0x00000006},
                    {.type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0x000003e8},
                    //{.type = EV_KEY, .code = BTN_TOUCH, .value = 1}, // DOWN
                    {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
            };
            int arrCount = sizeof (down_event) / sizeof (down_event[0]);
            events.insert(events.end(), down_event, down_event + arrCount);
            finger.isUse = true;
            finger.isDown = true;
        }
        if (finger.isNeedMove && finger.isUse && finger.isDown)
        {
            struct input_event move_event[4] = {
                    {.type = EV_ABS, .code = ABS_MT_SLOT, .value = index},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_X, .value = finger.x},
                    {.type = EV_ABS, .code = ABS_MT_POSITION_Y, .value = finger.y},
                    { .type = EV_SYN, .code = SYN_REPORT, .value = 0 }
            };
            int arrCount = sizeof (move_event) / sizeof (move_event[0]);
            events.insert(events.end(), move_event, move_event + arrCount);
            finger.isNeedMove = false;
        }
        if (finger.isNeedUp && finger.isUse && finger.isDown)
        {
            struct input_event up_event[5] = {
                    {.type = EV_ABS, .code = ABS_MT_SLOT, .value = index},
                    {.type = EV_ABS, .code = ABS_MT_TOUCH_MAJOR, .value = 0x00000000},
                    {.type = EV_ABS, .code = ABS_MT_PRESSURE, .value = 0x00000000},
                    {.type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1},
                    //{.type = EV_KEY, .code = BTN_TOUCH, .value = 0}, // UP
                    {.type = EV_SYN, .code = SYN_REPORT, .value = 0}
            };
            int arrCount = sizeof(up_event) / sizeof(up_event[0]);
            events.insert(events.end(), up_event, up_event + arrCount);

            finger.x = 0;
            finger.y =0;
            finger.id = 0;
            //finger.TRACKING_ID = 0;
            finger.isNeedDown = 0;
            finger.isNeedUp = false;
            finger.isNeedMove = false;
            finger.isUse = false;
            finger.isDown = false;
            finger.IsFirstDown = false;
        }
        index++;
    }
    for (const auto& event : events)
    {
        write(uinputFd, &event, sizeof(event));
    }
    events.clear();
}

int touch::GetindexById(const int& byId)
{
    for(int i{0};i<10;i++)
    {
        if(Fingers[i].id == byId)
        {
            return i;
        }
    }
    return -1;
}

int touch::GetNoUseIndex()
{
    for(int i{0};i<10;i++)
    {
        if(!Fingers[i].isUse)
        {
            return i;
        }
    }
    return -1;
}

int touch::GetTRACKING_ID()
{
    int maxTrackingID = INT_MIN;

    for (const auto& obj : Fingers)
    {
        if (obj.TRACKING_ID > maxTrackingID)
        {
            maxTrackingID = obj.TRACKING_ID;
        }
    }
    if(maxTrackingID == 0)
    {
        return 114514;
    }
    return maxTrackingID + 1;
}

void touch::touch_down(const int& id,Vector2 pos)
{
    Vector2 NewPos = this->rotatePointx(pos,{this->screenInfo.width, this->screenInfo.height}, true);
    NewPos.x *= screenToTouchRatio;
    NewPos.y *= screenToTouchRatio;
    std::cout<<"NewPos: "<<NewPos.x<<NewPos.y<<std::endl;
    if(this->IsNoFirstDown())
    {
        Fingers[this->GetNoUseIndex()].IsFirstDown = true;
    }
    Fingers[this->GetNoUseIndex()].id = id;
    Fingers[this->GetNoUseIndex()].x = NewPos.x;
    Fingers[this->GetNoUseIndex()].y = NewPos.y;
    Fingers[this->GetNoUseIndex()].TRACKING_ID = this->GetTRACKING_ID();
    Fingers[this->GetNoUseIndex()].isNeedDown = true;
    this->upLoad();
}

void touch::touch_move(const int& id, Vector2 pos)
{
    Vector2 NewPos = this->rotatePointx(pos,{this->screenInfo.width, this->screenInfo.height}, true);
    NewPos.x *= screenToTouchRatio;
    NewPos.y *= screenToTouchRatio;
    Fingers[this->GetindexById(id)].x = NewPos.x;
    Fingers[this->GetindexById(id)].y = NewPos.y;
    Fingers[this->GetindexById(id)].isNeedMove = true;
    this->upLoad();
}

void touch::touch_up(const int& id)
{
    Fingers[this->GetindexById(id)].isNeedUp = true;
    this->upLoad();
}

bool touch::IsNoFirstDown()
{
    for(auto& finger:Fingers)
    {
        if(finger.IsFirstDown)
        {
            return false;
        }
    }
    return true;
}