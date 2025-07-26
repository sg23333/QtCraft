#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

// 定义摄像机移动方向的枚举
enum class Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

// 默认摄像机参数
const float YAW         = -90.0f;
const float PITCH       = -45.0f;
const float SPEED       = 5.0f;
const float SENSITIVITY = 0.1f;
const float ZOOM        = 45.0f;

// 视锥平面
struct Frustum {
    glm::vec4 planes[6];
};


class Camera
{
public:
    // 摄像机属性
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;
    // 欧拉角
    float Yaw;
    float Pitch;
    // 摄像机选项
    float MovementSpeed;
    float MouseSensitivity;
    float Zoom;
    // 视锥
    Frustum viewFrustum;


    // 构造函数
    explicit Camera(glm::vec3 position = glm::vec3(8.0f, 25.0f, 8.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);

    // 返回视图矩阵
    glm::mat4 GetViewMatrix() const;

    // 处理键盘输入
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);

    // 处理鼠标移动
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    // 更新视锥
    void UpdateFrustum(const glm::mat4& proj, const glm::mat4& view);

    // 检查AABB是否在视锥内
    bool IsBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const;


private:
    // 根据摄像机的欧拉角更新其方向向量
    void updateCameraVectors();
};
#endif
