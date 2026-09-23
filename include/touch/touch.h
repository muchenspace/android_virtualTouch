#pragma once

#include <linux/uinput.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "vector2.h"

struct screen
{
    int width{};
    int height{};
    int orientation{};
    std::vector<int> fd{};
};

struct touchOBJ
{
    int x{0};
    int y{0};
    int id{0};
    int TRACKING_ID{0};
    bool isDown{false};
    bool isUse{false};
};

class touch
{
public:
    touch();
    ~touch();
    void touchDown(const int& id,const Vector2 &pos);//按下,id可以是任何数
    void touchUp(const int& id);//释放
    void touchMove(const int& id,const Vector2 &pos);//x轴移动到x，y轴移动到y
private:
    std::vector<std::thread> threads;//储存PTScreenEventToFingerByFd
    uinput_user_dev usetup{};//驱动信息
    int uinputFd{};//uinput的文件标识符
    std::thread getScreenOrientationThread{};//循环获取屏幕方向的线程
    float screenToTouchRatio{};//比例
    touchOBJ fingers[2][10]{};//手指，物理触摸屏和模拟触摸
    screen screenInfo{};//屏幕信息
    screen touchScreenInfo{};//触摸屏信息
    std::mutex fingersMutex{};
    std::mutex uploadMutex{};
    std::atomic<bool> quitFlag{false};
    std::atomic<int> screenOrientation{0};
private:
    int GetNoUseIndex();//获取一个没有使用过的finger,仅限模拟触摸
    int GetIndexById(const int& byId);
    void GetScreenOrientation();//循环获取屏幕方向
    static std::string exec(const std::string& command);
    Vector2 rotatePointx(const Vector2& pos, const Vector2& wh, bool reverse) const;//根据方向来重构坐标,pos是坐标，wh是宽高 --reverse为真代表要反向计算 //举个例子：假如你要在横屏时触摸200，200，就让reverse == ture,假如你要让原始坐标转为屏幕分辨率就让reverse == false
    void upLoad();//遍历Finger数组并上报
    void PTScreenEventToFinger(int fd=0);//将物理触摸屏的Event转化存到Finger数组
    void InitTouchScreenInfo();//初始化物理触摸屏信息
    void InitScreenInfo();//初始化屏幕信息
    void openUinputOrThrow();//打开 /dev/uinput，失败时清理已启动线程/fd 并抛异常
    void configureUinputCapabilities();//UI_SET_EVBIT/ABSBIT/KEYBIT/PROPBIT
    void setupUinputDeviceParams();//填充 usetup 字段与 abs 参数
    void createUinputDevice();//write usetup + UI_DEV_CREATE
    void grabPhysicalTouchDevices();//EVIOCGRAB 独占物理触摸屏
    void calculateScreenToTouchRatio();//计算 screenToTouchRatio 并钳制
    void sendInitialTouchDown();//写入 BTN_TOUCH=1（构造函数约束项，逻辑不变）
    Vector2 screenToTouchCoords(const Vector2& pos) const;//屏幕坐标→触摸坐标（用于 touchDown/touchMove）
    Vector2 touchToScreenCoords(const Vector2& pos) const;//触摸坐标→屏幕坐标（用于监听回调）
    static void appendFingerEvents(input_event* events, int& count, const touchOBJ& finger);
};
