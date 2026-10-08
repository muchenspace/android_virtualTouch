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
#include <unistd.h>
#include <chrono>
#include <thread>
#include "touch/touch.h"

int main()
{
    touch touchTest;

    const int swipeId = 1;
    const int tapId = 2;

    const Vector2 swipeStart{500.0f, 1200.0f};
    const Vector2 swipeEnd{500.0f, 1000.0f};
    const Vector2 tapPos{500.0f, 700.0f};

    while (true)
    {
        auto periodStart = std::chrono::steady_clock::now();

        while (std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::steady_clock::now() - periodStart).count() < 5)
        {
            touchTest.touchDown(swipeId, swipeStart);
            for (int step = 1; step <= 20; ++step)
            {
                float t = static_cast<float>(step) / 20.0f;
                Vector2 curPos{
                    swipeStart.x + (swipeEnd.x - swipeStart.x) * t,
                    swipeStart.y + (swipeEnd.y - swipeStart.y) * t
                };
                touchTest.touchMove(swipeId, curPos);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            touchTest.touchUp(swipeId);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            touchTest.touchDown(tapId, tapPos);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            touchTest.touchUp(tapId);

            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    return 0;
}
