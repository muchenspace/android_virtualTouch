# android_virtualTouch
## 介绍
在安卓上纯native层模拟手指触摸
<br>
不会和正常手指触摸冲突
## 使用
    void touch_down(const int& id,Vector2 pos);//按下,id可以是任何数
    void touch_up(const int& id);//释放
    void touch_move(const int& id,Vector2 pos);//x轴移动到x，y轴移动到y
## 关于
使用多点触控协议A类型
<br>
之前使用b协议的版本放到了另一个分支，有需要可以去下载
<br>
-_-
## 注
关于第一次执行正常使用，第二次无法使用
<br>
出现这个问题大概率是apatch/kernelsu用户,因为root实现方式的原因(应该是),你直接划掉终端的进程并不会关闭本进程，导致本进程在后台一直运行,然后你第二次执行就出问题了
<br>
解决办法：
<br>
以正常方法关闭进程
## 
20240904 修复卡屏bug
<br>
20240906 适配更多设备
