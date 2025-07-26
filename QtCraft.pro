QT       += core gui opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    camera.cpp \
    inventory.cpp \
    main.cpp \
    openglwindow.cpp  # <-- 删除了 mainwindow.cpp

HEADERS += \
    FastNoiseLite.h \
    block.h \
    camera.h \
    inventory.h \
    openglwindow.h    # <-- 删除了 mainwindow.h

# FORMS 整个部分都删除了，因为它只包含 mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
INCLUDEPATH += $$PWD/glm

RESOURCES += \
    resources.qrc
