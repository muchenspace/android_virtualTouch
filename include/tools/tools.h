#include <linux/uinput.h>
#include <thread>

struct screen
{
    int width{};
    int height{};
    int orientation{};
    int fd{};
};

struct touchOBJ
{
    int x{0};
    int y{0};
    int id{0};
    int PySlot{-1};//如果数据来自于物理触摸屏，这个成员会被赋值
    int TRACKING_ID{0};
    bool isDown{false};
    bool isUse{false};
    bool isNeedMove{false};
    bool isNeedDown{false};
    bool isNeedUp{false};
    bool IsFirstDown{false};
};

class Vector2
{
public:
    Vector2();
    Vector2(float x, float y);
    Vector2(int x, int y);
    Vector2(Vector2 &va);
    Vector2& operator=(const Vector2& other);
    float x{};
    float y{};
};

class Vector3
{
public:
    Vector3();
    Vector3(int x,int y,int z);
    int x{};
    int y{};
    int z{};
};

class touch
{
public:
    touch();
    ~touch();
    void touch_down(const int& id,Vector2 pos);//按下,id可以是任何数
    void touch_up(const int& id);//释放,id可以是任何数
    void touch_move(const int& id,Vector2 pos);//x轴移动到x，y轴移动到y
private:
    uinput_user_dev usetup;//驱动信息
    int uinputFd{};//uinput的文件标识符
    std::thread PTScreenEventToFingerThread{};//将物理触摸屏的Event转化存到Finger数组的线程
    std::thread GetScreenorientationThread{};//循环获取屏幕方向的线程
    float screenToTouchRatio{};//比例
    touchOBJ Fingers[10] = {};//手指
    screen screenInfo = {};//屏幕信息
    screen touchScreenInfo = {};//触摸屏信息
private:
    int GetPyFinger(int slot);
    bool IsNoFirstDown();
    int GetTRACKING_ID();
    int GetNoUseIndex();//获取一个没有使用过的finger
    int GetindexById(const int& byId);
    void GetScrorientation();//循环获取屏幕方向
    std::string exec(std::string command);
    Vector2 rotatePointx(Vector2 pos, const Vector2 wh, bool reverse = false);//根据方向来重构坐标,pos是坐标，wh是宽高 --reverse为真代表要反向计算 //举个例子：假如你要在横屏时触摸200，200，就让reverse == ture,假如你要让原始坐标转为实际触摸的就不用动
    void upLoad();//遍历Finger数组并发送
    void PTScreenEventToFinger();//将物理触摸屏的Event转化存到Finger数组
    void emit(int fd,input_event ie);//将ie写入fd
    void InitTouchScreenInfo();//初始化物理触摸屏info
    void InitScreenInfo();//初始化屏幕Info
};