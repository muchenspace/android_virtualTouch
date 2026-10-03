#pragma once

#include <linux/uinput.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "vector2.h"

// 槽位分区（Type B 协议）：
//   slot 0-9   物理触摸屏透传
//   slot 10-15 模拟触摸
// 总槽数与 Android 框架的 MAX_POINTERS(16) 对齐，保证全部槽位可同帧同时生效
constexpr int kPhysicalSlotCount = 10;
constexpr int kSimulatedSlotCount = 6;
constexpr int kPhysicalSlotBase = 0;
constexpr int kSimulatedSlotBase = kPhysicalSlotCount;
constexpr int kTotalSlotCount = kPhysicalSlotCount + kSimulatedSlotCount; // 虚拟设备声明的 slot 总数
constexpr int kMaxPhysicalSlots = 10; // 物理触摸屏 slot 数上限

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
    std::vector<std::thread> threads;//储存物理触摸屏透传线程
    uinput_user_dev usetup{};//驱动信息
    int uinputFd{};//uinput的文件标识符
    std::thread getScreenOrientationThread{};//循环获取屏幕方向的线程
    float screenToTouchRatio{};//比例
    touchOBJ fingers[kSimulatedSlotCount]{};//模拟触摸
    screen screenInfo{};//屏幕信息
    screen touchScreenInfo{};//触摸屏信息
    std::mutex fingersMutex{};
    std::atomic<bool> quitFlag{false};
    std::atomic<int> screenOrientation{0};
private:
    int GetNoUseIndex();//获取一个没有使用过的finger
    int GetIndexById(const int& byId);
    void GetScreenOrientation();//循环获取屏幕方向
    static std::string exec(const std::string& command);
    Vector2 rotatePointx(const Vector2& pos, const Vector2& wh, bool reverse) const;//根据方向来重构坐标,pos是坐标，wh是宽高 --reverse为真代表要反向计算
    void PTScreenEventPassthrough(int fd=0);//物理触摸屏事件原样透传到虚拟设备
    void InitTouchScreenInfo();//初始化物理触摸屏信息
    void InitScreenInfo();//初始化屏幕信息
    void openUinput();//打开 /dev/uinput
    void configureUinputCapabilities();//UI_SET_EVBIT/ABSBIT/PROPBIT
    void setupUinputDeviceParams();//填充 usetup 字段与 abs 参数
    void createUinputDevice();//write usetup + UI_DEV_CREATE
    void grabPhysicalTouchDevices();//EVIOCGRAB 独占物理触摸屏
    void calculateScreenToTouchRatio();//计算 screenToTouchRatio
    Vector2 screenToTouchCoords(const Vector2& pos) const;//屏幕坐标→触摸坐标（用于 touchDown/touchMove）
    static bool isForwardedAxis(int code);//判断物理事件是否需要透传
};
