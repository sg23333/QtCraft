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
#include <queue>

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

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/hash.hpp>

// 定义世界和区块的维度常量
const int CHUNK_SIZE_XZ = 16;
const int WORLD_HEIGHT_IN_BLOCKS = 128; // 一个区块柱的完整高度

class Chunk {
public:
    // 为了方便，保留了旧的常量名，但建议使用新的常量
    static const int CHUNK_SIZE = CHUNK_SIZE_XZ;
    static const int CHUNK_HEIGHT = WORLD_HEIGHT_IN_BLOCKS;

    Chunk();
    ~Chunk();

    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer vbo;
    int vertex_count = 0;
    std::vector<Vertex> mesh_data;

    QOpenGLVertexArrayObject vao_transparent;
    QOpenGLBuffer vbo_transparent;
    int vertex_count_transparent = 0;
    std::vector<Vertex> mesh_data_transparent;

    // 区块现在存储一个完整的方块柱
    uint8_t blocks[CHUNK_SIZE_XZ][WORLD_HEIGHT_IN_BLOCKS][CHUNK_SIZE_XZ] = {{{0}}};
    uint8_t lighting[CHUNK_SIZE_XZ][WORLD_HEIGHT_IN_BLOCKS][CHUNK_SIZE_XZ] = {{{0}}};
    bool needs_remeshing = true;

    bool is_building = false;
    glm::ivec3 coords; // y分量将始终为0，代表区块柱的基底
};

struct AABB {
    glm::vec3 min;
    glm::vec3 max;
};

// --- 新增：定义光照更新节点结构体 ---
// 用于在洪水填充算法中传递方块位置和光照等级，比 std::pair 更清晰
struct LightNode {
    glm::ivec3 pos;
    uint8_t level;
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
    // --- 光照系统成员变量和函数修改 ---
    std::queue<LightNode> m_light_propagation_queue; // 更新队列类型为 LightNode
    // m_light_removal_queue 已被移除，因为光照移除现在是 setBlock 中的局部操作

    // 新增私有函数，用于即时光照更新
    void removeLight(std::queue<LightNode>& removal_queue);
    void propagateLight(std::queue<LightNode>& propagation_queue);
    // ------------------------------------

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
    int findSafeSpawnY(int x, int z);

    void initializeSunlight();
    uint8_t getLight(const glm::ivec3& world_pos);
    void setLight(const glm::ivec3& world_pos, uint8_t level);

    void initTextures();
    void initCrosshair();
    void initInventoryBar();
    void initOverlay();

    QOpenGLShaderProgram m_program;
    QOpenGLTexture *m_texture_atlas = nullptr;
    std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>> m_chunks;
    GLint m_vp_matrix_location;
    GLint m_model_matrix_location;
    QTimer m_timer;

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

    QOpenGLVertexArrayObject m_crosshair_vao;
    QOpenGLBuffer m_crosshair_vbo;
    QOpenGLShaderProgram m_crosshair_program;
    GLint m_crosshair_proj_matrix_location;

    QOpenGLVertexArrayObject m_overlay_vao;
    QOpenGLBuffer m_overlay_vbo;
    QOpenGLShaderProgram m_overlay_program;
    GLint m_overlay_color_location;

    Camera m_camera;
    Inventory m_inventory;
    glm::vec3 m_player_velocity = glm::vec3(0.0f);
    bool m_is_on_ground = false;
    bool m_is_in_water = false;
    bool m_is_flying = false; // 飞行状态

    QSet<int> m_pressed_keys;

    QElapsedTimer m_elapsed_timer;
    QElapsedTimer m_space_press_timer; // 用于检测双击

    bool m_cursor_locked = false;
    bool m_just_locked_cursor = false;

    QFutureWatcher<void> m_mesh_builder_watcher;
    QMutex m_ready_chunks_mutex;
    QList<Chunk*> m_ready_chunks;
};

#endif // OPENGLWINDOW_H
