#include "camera.h"

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
{
    Position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    updateCameraVectors();
}


glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(Position, Position + Front, Up);
}

void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = MovementSpeed * deltaTime;
    glm::vec3 flat_front = glm::normalize(glm::vec3(Front.x, 0.0f, Front.z));
    glm::vec3 right = glm::normalize(glm::cross(flat_front, WorldUp));


    if (direction == Camera_Movement::FORWARD)
        Position += flat_front * velocity;
    if (direction == Camera_Movement::BACKWARD)
        Position -= flat_front * velocity;
    if (direction == Camera_Movement::LEFT)
        Position -= right * velocity;
    if (direction == Camera_Movement::RIGHT)
        Position += right * velocity;
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw   += xoffset;
    Pitch += yoffset;

    if (constrainPitch)
    {
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::updateCameraVectors()
{
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up    = glm::normalize(glm::cross(Right, Front));
}

void Camera::UpdateFrustum(const glm::mat4& proj, const glm::mat4& view) {
    glm::mat4 clip = proj * view;

    // Right
    viewFrustum.planes[0] = glm::vec4(clip[0][3] - clip[0][0], clip[1][3] - clip[1][0], clip[2][3] - clip[2][0], clip[3][3] - clip[3][0]);
    // Left
    viewFrustum.planes[1] = glm::vec4(clip[0][3] + clip[0][0], clip[1][3] + clip[1][0], clip[2][3] + clip[2][0], clip[3][3] + clip[3][0]);
    // Bottom
    viewFrustum.planes[2] = glm::vec4(clip[0][3] + clip[0][1], clip[1][3] + clip[1][1], clip[2][3] + clip[2][1], clip[3][3] + clip[3][1]);
    // Top
    viewFrustum.planes[3] = glm::vec4(clip[0][3] - clip[0][1], clip[1][3] - clip[1][1], clip[2][3] - clip[2][1], clip[3][3] - clip[3][1]);
    // Near
    viewFrustum.planes[4] = glm::vec4(clip[0][3] + clip[0][2], clip[1][3] + clip[1][2], clip[2][3] + clip[2][2], clip[3][3] + clip[3][2]);
    // Far
    viewFrustum.planes[5] = glm::vec4(clip[0][3] - clip[0][2], clip[1][3] - clip[1][2], clip[2][3] - clip[2][2], clip[3][3] - clip[3][2]);

    for (int i = 0; i < 6; i++) {
        viewFrustum.planes[i] = glm::normalize(viewFrustum.planes[i]);
    }
}

bool Camera::IsBoxInFrustum(const glm::vec3& min, const glm::vec3& max) const {
    for (int i = 0; i < 6; i++) {
        if ((glm::dot(viewFrustum.planes[i], glm::vec4(min.x, min.y, min.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(max.x, min.y, min.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(min.x, max.y, min.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(max.x, max.y, min.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(min.x, min.y, max.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(max.x, min.y, max.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(min.x, max.y, max.z, 1.0f)) < 0.0) &&
            (glm::dot(viewFrustum.planes[i], glm::vec4(max.x, max.y, max.z, 1.0f)) < 0.0)) {
            return false;
        }
    }
    return true;
}
