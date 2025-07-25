#ifndef OPENGLWINDOW_H
#define OPENGLWINDOW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QSet>
#include <vector>
#include <unordered_map>

// --- 新增的头文件 ---
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QList>

#include "FastNoiseLite.h"

// 使用 GLM 的哈希函数
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

// --- 顶点定义 ---
struct Vertex {
    glm::vec3 position;
    glm::vec2 texCoord;
};

// --- 区块定义 ---
class Chunk {
public:
    static const int CHUNK_SIZE = 16;
    Chunk();
    ~Chunk();
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo;
    int vertex_count = 0;
    uint8_t blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE] = {{{0}}};
    bool needs_remeshing = true;

    // --- 新增状态和数据成员 ---
    bool is_building = false;           // 状态锁，防止重复提交构建任务
    glm::ivec3 coords;                  // 存储自身的区块坐标
    std::vector<Vertex> mesh_data;      // 存储后台线程计算好的网格数据
};

// --- 玩家包围盒定义 ---
struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};


class OpenGLWindow : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit OpenGLWindow(QWidget *parent = nullptr);
    ~OpenGLWindow();

protected:
    // --- Qt 事件重载 ---
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    // --- 主游戏循环 ---
    void updateGame();
    // --- 新增槽函数，用于处理网格构建完成的信号 (虽然在此实现中我们不直接用它) ---
    void handleChunkMeshReady();


private:
    // --- 世界管理 ---
    void generateWorld();
    void generateChunk(Chunk* chunk, const glm::ivec3& chunk_coords);
    uint8_t getBlock(const glm::ivec3& world_pos);
    void setBlock(const glm::ivec3& world_pos, uint8_t block_id);
    glm::ivec3 worldToChunkCoords(const glm::ivec3& world_pos);

    // --- 网格构建 (现在在后台线程运行) ---
    void buildChunkMesh(Chunk* chunk);

    // --- 输入与物理 ---
    void processInput();
    void updatePhysics(float deltaTime);
    void resolveCollisions(glm::vec3& position, const glm::vec3& velocity);
    AABB getPlayerAABB(const glm::vec3& position) const;
    void updateCameraVectors();

    // --- 交互 ---
    bool raycast(glm::ivec3& hit_block, glm::ivec3& adjacent_block);

    // --- 初始化辅助函数 ---
    void initShaders();
    void initTextures();
    void initCrosshair();

    // --- 渲染 ---
    QOpenGLShaderProgram m_program;
    QOpenGLTexture *m_texture_atlas = nullptr;
    std::unordered_map<glm::ivec3, Chunk*> m_chunks;
    GLint m_vp_matrix_location;
    GLint m_model_matrix_location;
    QTimer m_timer;

    // --- 准星渲染资源 ---
    QOpenGLVertexArrayObject m_crosshair_vao;
    QOpenGLBuffer m_crosshair_vbo;
    QOpenGLShaderProgram m_crosshair_program;
    GLint m_crosshair_proj_matrix_location;


    // --- 相机/玩家属性 ---
    glm::vec3 m_camera_pos   = glm::vec3(8.0f, 25.0f, 8.0f);
    glm::vec3 m_camera_front;
    glm::vec3 m_camera_up    = glm::vec3(0.0f, 1.0f,  0.0f);

    // 玩家物理状态
    glm::vec3 m_player_velocity = glm::vec3(0.0f);
    bool m_is_on_ground = false;

    // --- 鼠标状态 ---
    float m_yaw   = -90.0f;
    float m_pitch = -45.0f;

    // --- 键盘状态 ---
    QSet<int> m_pressed_keys;

    // --- 帧时间 ---
    float m_delta_time = 0.0f;
    float m_last_frame = 0.0f;

    // --- 状态标志 ---
    bool m_cursor_locked = false;
    bool m_just_locked_cursor = false;
    uint8_t m_current_block_id = 1;

    // --- 新增：多线程相关成员 ---
    QFutureWatcher<void> m_mesh_builder_watcher;
    QMutex m_ready_chunks_mutex;   // 互斥锁，保护 m_ready_chunks
    QList<Chunk*> m_ready_chunks;  // 存储已完成网格构建、待上传到GPU的区块
};

#endif // OPENGLWINDOW_H
