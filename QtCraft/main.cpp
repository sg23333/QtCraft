#include "openglwindow.h" // 包含我们自己的头文件
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 创建我们的OpenGL窗口实例
    OpenGLWindow w;
    w.resize(400, 300); // 设置一个初始大小
    w.show();           // 显示窗口

    return a.exec();
}
