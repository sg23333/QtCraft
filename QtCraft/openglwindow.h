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
#include <QElapsedTimer> // <-- 1. 添加 QElapsedTimer

#include "FastNoiseLite.h"
#include "block.h"      // <-- 2. 包含新的 block.h

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
    bool is_building = false;
    glm::ivec3 coords;
    std::vector<Vertex> mesh_data;
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
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private slots:
    void updateGame();
    void handleChunkMeshReady();

private:
    void generateWorld();
    void generateChunk(Chunk* chunk, const glm::ivec3& chunk_coords);
    uint8_t getBlock(const glm::ivec3& world_pos);
    void setBlock(const glm::ivec3& world_pos, BlockType block_id); // <-- 参数类型改为 BlockType
    glm::ivec3 worldToChunkCoords(const glm::ivec3& world_pos);
    void buildChunkMesh(Chunk* chunk);
    void processInput();
    void updatePhysics(float deltaTime);
    void resolveCollisions(glm::vec3& position, const glm::vec3& velocity);
    AABB getPlayerAABB(const glm::vec3& position) const;
    void updateCameraVectors();
    bool raycast(glm::ivec3& hit_block, glm::ivec3& adjacent_block);
    void initShaders();
    void initTextures();
    void initCrosshair();

    QOpenGLShaderProgram m_program;
    QOpenGLTexture *m_texture_atlas = nullptr;
    std::unordered_map<glm::ivec3, Chunk*> m_chunks;
    GLint m_vp_matrix_location;
    GLint m_model_matrix_location;
    QTimer m_timer;

    QOpenGLVertexArrayObject m_crosshair_vao;
    QOpenGLBuffer m_crosshair_vbo;
    QOpenGLShaderProgram m_crosshair_program;
    GLint m_crosshair_proj_matrix_location;

    glm::vec3 m_camera_pos   = glm::vec3(8.0f, 25.0f, 8.0f);
    glm::vec3 m_camera_front;
    glm::vec3 m_camera_up    = glm::vec3(0.0f, 1.0f,  0.0f);
    glm::vec3 m_player_velocity = glm::vec3(0.0f);
    bool m_is_on_ground = false;

    float m_yaw   = -90.0f;
    float m_pitch = -45.0f;

    QSet<int> m_pressed_keys;

    // --- 3. 修改帧时间相关的成员变量 ---
    QElapsedTimer m_elapsed_timer; // 使用 QElapsedTimer 替代旧的 float 变量

    bool m_cursor_locked = false;
    bool m_just_locked_cursor = false;
    BlockType m_current_block_id = BlockType::Stone; // <-- 使用 BlockType

    QFutureWatcher<void> m_mesh_builder_watcher;
    QMutex m_ready_chunks_mutex;
    QList<Chunk*> m_ready_chunks;
};

#endif // OPENGLWINDOW_H
