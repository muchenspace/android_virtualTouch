/*
                          _ooOoo_
                         o8888888o
                         88" . "88
                         (| -_- |)
                         O\  =  /O
                      ____/`---'\____
                    .'  \\|     |//  `.
                   /  \\|||  :  |||//  \
                  /  _||||| -:- |||||-  \
                  |   | \\\  -  /// |   |
                  | \_|  ''\---/''  |   |
                  \  .-\__  `-`  ___/-. /
                ___`. .'  /--.--\  `. . __
             ."" '<  `.___\_<|>_/___.'  >'"".
            | | :  `- \`.;`\ _ /`;.`/ - ` : | |
            \  \ `-.   \_ __\ /__ _/   .-` /  /
       ======`-.____`-.___\_____/___.-`____.-'======
                          `=---='
       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
                佛祖保佑                  永无BUG
                佛祖镇楼                  BUG辟易
           佛曰:
                   写字楼里写字间，写字间里程序员；
                   程序人员写程序，又拿程序换酒钱。
                   酒醒只在网上坐，酒醉还来网下眠；
                   酒醉酒醒日复日，网上网下年复年。
                   但愿老死电脑间，不愿鞠躬老板前；
                   奔驰宝马贵者趣，公交自行程序员。
                   别人笑我忒疯癫，我笑自己命太贱；
                   不见满街漂亮妹，哪个归得程序员？
                         ！！BUG退散！！
 */
#include <iostream>
#include <unistd.h>
#include <cmath>
#include <chrono>
#include <thread>
#include "touch/touch.h"

int main()
{
    touch touchTest;

    constexpr float kPi = 3.14159265358979323846f;
    const int fingerIds[4] = {101, 102, 103, 104};
    // 四指各自的圆心坐标
    const Vector2 centers[4] = {
        {300.0f, 600.0f},   // 顶部左 (持续)
        {800.0f, 600.0f},   // 顶部右 (持续)
        {300.0f, 1400.0f},  // 底部左 (画一圈抬起0.5s)
        {800.0f, 1400.0f}   // 底部右 (画一圈抬起0.5s)
    };
    const float radius = 120.0f;

    std::cout << "[Test] 上方两指按下 touchDown..." << std::endl;
    touchTest.touchDown(fingerIds[0], {centers[0].x + radius, centers[0].y});
    touchTest.touchDown(fingerIds[1], {centers[1].x + radius, centers[1].y});

    std::cout << "[Test] 测试开始：上方持续旋转，下方画一圈抬起 0.5 秒再循环..." << std::endl;
    uint64_t step = 0;
    while (true)
    {
        // 1. 上方两指：每步都在以 100Hz 持续画圆，永不停顿
        float topAngle = (step % 100) * (2.0f * kPi / 100.0f);
        touchTest.touchMove(fingerIds[0], {centers[0].x + radius * std::cos(topAngle), centers[0].y + radius * std::sin(topAngle)});
        touchTest.touchMove(fingerIds[1], {centers[1].x + radius * std::cos(topAngle), centers[1].y + radius * std::sin(topAngle)});

        // 2. 下方两指：150步为一个周期（画圈100步=1s，抬起50步=0.5s）
        int cycleStep = step % 150;
        if (cycleStep == 0)
        {
            // 周期开始：下方两指落点
            touchTest.touchDown(fingerIds[2], {centers[2].x + radius, centers[2].y});
            touchTest.touchDown(fingerIds[3], {centers[3].x + radius, centers[3].y});
        }
        else if (cycleStep < 100)
        {
            // 画圆中（持续 1 秒）
            float bottomAngle = cycleStep * (2.0f * kPi / 100.0f);
            touchTest.touchMove(fingerIds[2], {centers[2].x + radius * std::cos(bottomAngle), centers[2].y + radius * std::sin(bottomAngle)});
            touchTest.touchMove(fingerIds[3], {centers[3].x + radius * std::cos(bottomAngle), centers[3].y + radius * std::sin(bottomAngle)});
        }
        else if (cycleStep == 100)
        {
            // 刚好满一圈：下方两指抬起
            touchTest.touchUp(fingerIds[2]);
            touchTest.touchUp(fingerIds[3]);
        }
        // cycleStep 处于 101~149 期间：下方保持抬起（空跑 50 步 * 10ms = 0.5 秒）

        ++step;
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 100Hz
    }

    return 0;
}
