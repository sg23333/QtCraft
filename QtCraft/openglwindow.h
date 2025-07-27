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
#include <memory>
#include <map>
#include <queue> // 新增

// --- 新增的头文件 ---
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QList>
#include <QElapsedTimer>

#include "FastNoiseLite.h"
#include "camera.h"
#include "block.h"
#include "inventory.h"


// 使用 GLM 的哈希函数
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

// --- 区块定义 ---
class Chunk {
public:
    static const int CHUNK_SIZE = 16;
    Chunk();
    ~Chunk();

    // 不透明物体的缓冲
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo;
    int vertex_count = 0;
    std::vector<Vertex> mesh_data;

    // 半透明物体的缓冲
    QOpenGLVertexArrayObject vao_transparent;
    QOpenGLBuffer vbo_transparent;
    int vertex_count_transparent = 0;
    std::vector<Vertex> mesh_data_transparent;

    uint8_t blocks[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE] = {{{0}}};
    uint8_t lighting[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE] = {{{0}}}; // 新增：光照数据
    bool needs_remeshing = true;

    // --- 新增状态和数据成员 ---
    bool is_building = false;
    glm::ivec3 coords;
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
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void updateGame();
    void handleChunkMeshReady();

private:
    // --- 光照更新队列 ---
    std::queue<std::pair<glm::ivec3, int>> m_light_propagation_queue;
    std::queue<std::pair<glm::ivec3, int>> m_light_removal_queue;

    void generateWorld();
    void generateChunk(Chunk* chunk, const glm::ivec3& chunk_coords);
    uint8_t getBlock(const glm::ivec3& world_pos);
    void setBlock(const glm::ivec3& world_pos, BlockType block_id);
    glm::ivec3 worldToChunkCoords(const glm::ivec3& world_pos);
    void buildChunkMesh(Chunk* chunk);
    void processInput();
    void updatePhysics(float deltaTime);
    void resolveCollisions(glm::vec3& position, const glm::vec3& velocity);
    AABB getPlayerAABB(const glm::vec3& position) const;
    bool raycast(glm::ivec3& hit_block, glm::ivec3& adjacent_block);
    void initShaders();
    int findSafeSpawnY(int x, int z); // 新增函数声明

    // --- 新增光照相关函数 ---
    void initializeSunlight();
    void propagateLight();
    void depropagateLight();
    uint8_t getLight(const glm::ivec3& world_pos);
    void setLight(const glm::ivec3& world_pos, uint8_t level);

    void initTextures();
    void initCrosshair();
    void initInventoryBar();
    void initOverlay(); // 新增初始化函数

    QOpenGLShaderProgram m_program;
    QOpenGLTexture *m_texture_atlas = nullptr;
    std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>> m_chunks;
    GLint m_vp_matrix_location;
    GLint m_model_matrix_location;
    QTimer m_timer;

    // 2D UI 渲染资源
    QOpenGLVertexArrayObject m_ui_vao;
    QOpenGLBuffer m_ui_vbo;
    QOpenGLShaderProgram m_ui_program;
    QOpenGLTexture *m_hotbar_texture = nullptr;
    QOpenGLTexture *m_hotbar_selector_texture = nullptr;
    GLint m_ui_proj_matrix_location;
    GLint m_ui_model_matrix_location;
    GLint m_ui_color_location;
    GLint m_ui_uv_offset_location;
    GLint m_ui_uv_scale_location;


    // 准星
    QOpenGLVertexArrayObject m_crosshair_vao;
    QOpenGLBuffer m_crosshair_vbo;
    QOpenGLShaderProgram m_crosshair_program;
    GLint m_crosshair_proj_matrix_location;

    // 水下效果专用资源
    QOpenGLVertexArrayObject m_overlay_vao;
    QOpenGLBuffer m_overlay_vbo;
    QOpenGLShaderProgram m_overlay_program;
    GLint m_overlay_color_location;


    Camera m_camera;
    Inventory m_inventory;
    glm::vec3 m_player_velocity = glm::vec3(0.0f);
    bool m_is_on_ground = false;

    // 新增状态
    bool m_is_in_water = false;

    QSet<int> m_pressed_keys;

    QElapsedTimer m_elapsed_timer;

    bool m_cursor_locked = false;
    bool m_just_locked_cursor = false;

    QFutureWatcher<void> m_mesh_builder_watcher;
    QMutex m_ready_chunks_mutex;
    QList<Chunk*> m_ready_chunks;
};

#endif // OPENGLWINDOW_H
