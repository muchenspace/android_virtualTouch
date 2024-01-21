#include <linux/uinput.h>
#include <thread>

struct touchOBJ
{
    int x{0};
    int y{0};
    int slot{};
    bool isDown{false};
    bool isUse{false};
    bool isUp{false};
    bool isNeedMove{false};
};

class Vector2
{
public:
    Vector2();
    Vector2(float x,float y);
    Vector2(Vector2& va);
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
    void touch_down(int id,int x,int y);//按下,这里的id记得从0开始，0-9
    void touch_up(int id);//释放
    void touch_move(int id,int x,int y);//x轴移动到x，y轴移动到y
private:
    input_event PTScreenEvent{};//物理触摸屏发出的事件
    uinput_user_dev usetup;//驱动信息
    int PTScreenfd;//物理触摸屏的标识符
    int fd{};//uinput的文件标识符
    std::thread forwardTouchThread{};//转发触摸线程
    Vector2 screenResolution{};//屏幕分辨率
    Vector2 touchSize{};//触摸屏宽高
    float screenToTouchRatio{};//比例

    void Touch();//遍历toucharr并处理
    void emit(int fd,input_event ie);//将ie写入fd
    void InitPTScreenfd();//初始化物理触摸屏标识符
    void InitTouchScreenSize();//初始化触摸屏宽高
    void InitScreenResolution();//初始化屏幕分辨率
};
