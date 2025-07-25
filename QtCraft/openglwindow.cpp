#include "openglwindow.h"
#include <QDebug>
#include <QImage>
#include <QTime>
#include <QCursor>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <cstddef>
#include <cmath> // For std::floor

// --- 玩家常量定义 ---
const float PLAYER_HEIGHT = 1.8f;
const float PLAYER_WIDTH = 0.6f;
const float PLAYER_EYE_LEVEL = 1.6f;
const float GRAVITY = -28.0f;
const float JUMP_VELOCITY = 9.0f;
const float MOVE_SPEED = 5.0f;

// --- 区块实现 ---
Chunk::Chunk() {
    memset(blocks, 0, sizeof(blocks));
}

Chunk::~Chunk() {
    if (vbo.isCreated()) vbo.destroy();
    if (vao.isCreated()) vao.destroy();
}

// --- OpenGLWindow 实现 ---
OpenGLWindow::OpenGLWindow(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    connect(&m_timer, &QTimer::timeout, this, &OpenGLWindow::updateGame);
    m_timer.start(16);

    updateCameraVectors();
}

OpenGLWindow::~OpenGLWindow()
{
    makeCurrent();
    for (auto const& [key, val] : m_chunks) {
        delete val;
    }
    m_chunks.clear();
    delete m_texture_atlas;

    if (m_crosshair_vbo.isCreated()) m_crosshair_vbo.destroy();
    if (m_crosshair_vao.isCreated()) m_crosshair_vao.destroy();

    doneCurrent();
}

void OpenGLWindow::initShaders()
{
    // 主场景着色器
    const char *vsrc = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        uniform mat4 vp_matrix;
        uniform mat4 model_matrix;
        out vec2 TexCoord;
        void main()
        {
            gl_Position = vp_matrix * model_matrix * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
        }
    )";

    const char *fsrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoord;
        uniform sampler2D texture_atlas;
        void main()
        {
            FragColor = texture(texture_atlas, TexCoord);
            if(FragColor.a < 0.1) discard;
        }
    )";

    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vsrc)) qFatal("主顶点着色器编译失败");
    if (!m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fsrc)) qFatal("主片段着色器编译失败");
    if (!m_program.link()) qFatal("主着色器程序链接失败");

    m_program.bind();
    m_program.setUniformValue("texture_atlas", 0);
    m_program.release();

    m_vp_matrix_location = m_program.uniformLocation("vp_matrix");
    m_model_matrix_location = m_program.uniformLocation("model_matrix");

    // 准星着色器
    const char* crosshair_vsrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        uniform mat4 proj_matrix;
        void main() {
            gl_Position = proj_matrix * vec4(aPos, 0.0, 1.0);
        }
    )";
    const char* crosshair_fsrc = R"(
        #version 330 core
        out vec4 FragColor;
        void main() {
            FragColor = vec4(1.0, 1.0, 1.0, 1.0); // 白色
        }
    )";

    if (!m_crosshair_program.addShaderFromSourceCode(QOpenGLShader::Vertex, crosshair_vsrc)) qFatal("准星顶点着色器编译失败");
    if (!m_crosshair_program.addShaderFromSourceCode(QOpenGLShader::Fragment, crosshair_fsrc)) qFatal("准星片段着色器编译失败");
    if (!m_crosshair_program.link()) qFatal("准星着色器程序链接失败");

    m_crosshair_proj_matrix_location = m_crosshair_program.uniformLocation("proj_matrix");
}

void OpenGLWindow::initTextures()
{
    QImage image(":/texture_atlas.png");
    if (image.isNull()) {
        qWarning() << "错误：无法从资源加载纹理图集 ':/texture_atlas.png'。";
        qWarning() << "请确认 'texture_atlas.png' 文件已经正确添加到了您的 .qrc 资源文件中，并且资源路径无误。";
        qFatal("纹理加载失败，程序终止。");
    }

    m_texture_atlas = new QOpenGLTexture(image.convertToFormat(QImage::Format_RGBA8888).mirrored());

    m_texture_atlas->setMinificationFilter(QOpenGLTexture::Nearest);
    m_texture_atlas->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_texture_atlas->setWrapMode(QOpenGLTexture::Repeat);
}

void OpenGLWindow::initCrosshair()
{
    float vertices[] = {
        // 水平线
        -10.0f, 0.0f,
        10.0f, 0.0f,
        // 垂直线
        0.0f, -10.0f,
        0.0f,  10.0f
    };

    m_crosshair_vao.create();
    m_crosshair_vao.bind();

    m_crosshair_vbo.create();
    m_crosshair_vbo.bind();
    m_crosshair_vbo.allocate(vertices, sizeof(vertices));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    m_crosshair_vao.release();
    m_crosshair_vbo.release();
}


void OpenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();

    initShaders();
    initTextures();
    initCrosshair();

    generateWorld();

    glClearColor(0.39f, 0.58f, 0.93f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

// --- 改动点: 更新世界生成 ---
// 在 openglwindow.cpp 中找到并替换下面的函数

// --- 新增: 单个区块地形生成函数 ---
void OpenGLWindow::generateChunk(Chunk* chunk, const glm::ivec3& chunk_coords)
{
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);

    FastNoiseLite distortion_noise;
    distortion_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    distortion_noise.SetFrequency(0.05f);

    // 您修改后的参数
    int octaves = 5;
    float persistence = 0.5f;
    float lacunarity = 2.2f;
    float base_frequency = 0.1f;
    float base_amplitude = 20.0f;
    float distortion_strength = 10.0f;

    for (int x = 0; x < Chunk::CHUNK_SIZE; ++x) {
        for (int z = 0; z < Chunk::CHUNK_SIZE; ++z) {
            int world_x = chunk_coords.x * Chunk::CHUNK_SIZE + x;
            int world_z = chunk_coords.z * Chunk::CHUNK_SIZE + z;

            float distortion_x = distortion_noise.GetNoise((float)world_x, (float)world_z) * distortion_strength;
            float distortion_z = distortion_noise.GetNoise((float)world_x + 543.21f, (float)world_z - 123.45f) * distortion_strength;

            float total_noise = 0.0f;
            float frequency = base_frequency;
            float amplitude = base_amplitude;

            for (int i = 0; i < octaves; ++i) {
                total_noise += noise.GetNoise(
                                   (float)world_x * frequency + distortion_x,
                                   (float)world_z * frequency + distortion_z
                                   ) * amplitude;
                amplitude *= persistence;
                frequency *= lacunarity;
            }

            int sea_level = 8; // 您可以调整海平面高度
            int terrain_height = static_cast<int>(total_noise) + sea_level;

            for (int y = 0; y < Chunk::CHUNK_SIZE; ++y) {
                int world_y = chunk_coords.y * Chunk::CHUNK_SIZE + y;

                if (world_y > terrain_height) {
                    if (world_y <= sea_level) {
                        // --- 核心改动：生成水方块 ---
                        chunk->blocks[x][y][z] = 4; // 4 是我们定义的水方块ID
                    } else {
                        chunk->blocks[x][y][z] = 0; // 空气
                    }
                } else {
                    if (world_y == terrain_height && world_y > sea_level) {
                        chunk->blocks[x][y][z] = 3; // 草
                    } else if (world_y > terrain_height - 5) {
                        chunk->blocks[x][y][z] = 2; // 泥土
                    } else {
                        chunk->blocks[x][y][z] = 1; // 石头
                    }
                }
            }
        }
    }
}


// --- 改动点: 更新世界生成逻辑，增加垂直区块 ---
void OpenGLWindow::generateWorld() {
    int world_size_in_chunks = 8;
    int world_height_in_chunks = 4; // 世界垂直高度为4个区块 (64个方块高)

    // 循环创建所有区块
    for (int x = -world_size_in_chunks / 2; x < world_size_in_chunks / 2; ++x) {
        for (int z = -world_size_in_chunks / 2; z < world_size_in_chunks / 2; ++z) {
            for (int y = -1; y < world_height_in_chunks-1; ++y) { // 从y=-1开始，让世界可以有更深的地下
                glm::ivec3 chunk_coords(x, y, z);
                Chunk* new_chunk = new Chunk();
                m_chunks[chunk_coords] = new_chunk;

                generateChunk(new_chunk, chunk_coords);
            }
        }
    }
    qDebug() << "生成了" << m_chunks.size() << "个区块。";
}


void OpenGLWindow::resizeGL(int w, int h)
{
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
}

void OpenGLWindow::updateGame()
{
    float currentFrame = QTime::currentTime().msecsSinceStartOfDay() / 1000.0f;
    if (m_last_frame == 0.0f) m_last_frame = currentFrame;
    m_delta_time = currentFrame - m_last_frame;
    m_last_frame = currentFrame;

    processInput();
    updatePhysics(m_delta_time);

    for (auto const& [coords, chunk] : m_chunks) {
        if (chunk->needs_remeshing) {
            buildChunkMesh(chunk, coords);
            chunk->needs_remeshing = false;
        }
    }

    update();
}

void OpenGLWindow::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- 1. 绘制3D世界 ---
    // !! 核心改动：在绘制世界前开启混合 !!
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program.bind();
    glActiveTexture(GL_TEXTURE0);
    m_texture_atlas->bind();

    glm::vec3 eye_pos = m_camera_pos + glm::vec3(0.0f, PLAYER_EYE_LEVEL, 0.0f);
    glm::mat4 view = glm::lookAt(eye_pos, eye_pos + m_camera_front, m_camera_up);
    float aspect_ratio = float(width()) / float(height());
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect_ratio, 0.1f, 500.0f);
    glm::mat4 vp = projection * view;
    glUniformMatrix4fv(m_vp_matrix_location, 1, GL_FALSE, glm::value_ptr(vp));

    for (auto const& [coords, chunk] : m_chunks) {
        if (chunk->vertex_count > 0) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(coords * Chunk::CHUNK_SIZE));
            glUniformMatrix4fv(m_model_matrix_location, 1, GL_FALSE, glm::value_ptr(model));
            chunk->vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, chunk->vertex_count);
        }
    }
    m_program.release();


    // --- 2. 绘制2D准星 (绘制准星前关闭混合，因为它是不透明的) ---
    glDisable(GL_BLEND); // <-- 关闭混合，避免影响后续的2D渲染
    glDisable(GL_DEPTH_TEST);

    m_crosshair_program.bind();
    float w = width();
    float h = height();
    glm::mat4 proj = glm::ortho(-w/2.0f, w/2.0f, -h/2.0f, h/2.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(m_crosshair_proj_matrix_location, 1, GL_FALSE, glm::value_ptr(proj));
    m_crosshair_vao.bind();
    glDrawArrays(GL_LINES, 0, 4);
    m_crosshair_vao.release();
    m_crosshair_program.release();
}


void OpenGLWindow::processInput()
{
    m_player_velocity.x = 0.0f;
    m_player_velocity.z = 0.0f;

    glm::vec3 flat_front = glm::vec3(m_camera_front.x, 0.0f, m_camera_front.z);
    if (glm::length2(flat_front) < 0.0001f) {
        flat_front = glm::vec3(0.0f);
    } else {
        flat_front = glm::normalize(flat_front);
    }

    if (m_pressed_keys.contains(Qt::Key_W)) {
        m_player_velocity += flat_front * MOVE_SPEED;
    }
    if (m_pressed_keys.contains(Qt::Key_S)) {
        m_player_velocity -= flat_front * MOVE_SPEED;
    }

    glm::vec3 right = glm::normalize(glm::cross(flat_front, m_camera_up));
    if (m_pressed_keys.contains(Qt::Key_A)) {
        m_player_velocity -= right * MOVE_SPEED;
    }
    if (m_pressed_keys.contains(Qt::Key_D)) {
        m_player_velocity += right * MOVE_SPEED;
    }
}

void OpenGLWindow::updatePhysics(float deltaTime)
{
    m_player_velocity.y += GRAVITY * deltaTime;
    resolveCollisions(m_camera_pos, m_player_velocity * deltaTime);
}

AABB OpenGLWindow::getPlayerAABB(const glm::vec3& position) const
{
    float half_width = PLAYER_WIDTH / 2.0f;
    return {
        position - glm::vec3(half_width, 0.0f, half_width),
        position + glm::vec3(half_width, PLAYER_HEIGHT, half_width)
    };
}

void OpenGLWindow::resolveCollisions(glm::vec3& position, const glm::vec3& velocity)
{
    m_is_on_ground = false;

    // Y-axis collision
    position.y += velocity.y;
    AABB player_box = getPlayerAABB(position);

    for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
        for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
            for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
                if (getBlock({x, y, z}) != 0) {
                    AABB block_box = {glm::vec3(x, y, z), glm::vec3(x + 1, y + 1, z + 1)};
                    if (player_box.max.x > block_box.min.x && player_box.min.x < block_box.max.x &&
                        player_box.max.y > block_box.min.y && player_box.min.y < block_box.max.y &&
                        player_box.max.z > block_box.min.z && player_box.min.z < block_box.max.z)
                    {
                        if (velocity.y > 0) {
                            position.y = block_box.min.y - PLAYER_HEIGHT - 0.0001f;
                        } else if (velocity.y < 0) {
                            position.y = block_box.max.y;
                            m_is_on_ground = true;
                        }
                        m_player_velocity.y = 0;
                        player_box = getPlayerAABB(position);
                    }
                }
            }
        }
    }

    // X-axis collision
    position.x += velocity.x;
    player_box = getPlayerAABB(position);

    for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
        for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
            for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
                if (getBlock({x, y, z}) != 0) {
                    AABB block_box = {glm::vec3(x, y, z), glm::vec3(x + 1, y + 1, z + 1)};
                    if (player_box.max.x > block_box.min.x && player_box.min.x < block_box.max.x &&
                        player_box.max.y > block_box.min.y && player_box.min.y < block_box.max.y &&
                        player_box.max.z > block_box.min.z && player_box.min.z < block_box.max.z)
                    {
                        if (velocity.x > 0) {
                            position.x = block_box.min.x - (PLAYER_WIDTH / 2.0f) - 0.0001f;
                        } else if (velocity.x < 0) {
                            position.x = block_box.max.x + (PLAYER_WIDTH / 2.0f) + 0.0001f;
                        }
                        m_player_velocity.x = 0;
                        player_box = getPlayerAABB(position);
                    }
                }
            }
        }
    }

    // Z-axis collision
    position.z += velocity.z;
    player_box = getPlayerAABB(position);

    for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
        for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
            for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
                if (getBlock({x, y, z}) != 0) {
                    AABB block_box = {glm::vec3(x, y, z), glm::vec3(x + 1, y + 1, z + 1)};
                    if (player_box.max.x > block_box.min.x && player_box.min.x < block_box.max.x &&
                        player_box.max.y > block_box.min.y && player_box.min.y < block_box.max.y &&
                        player_box.max.z > block_box.min.z && player_box.min.z < block_box.max.z)
                    {
                        if (velocity.z > 0) {
                            position.z = block_box.min.z - (PLAYER_WIDTH / 2.0f) - 0.0001f;
                        } else if (velocity.z < 0) {
                            position.z = block_box.max.z + (PLAYER_WIDTH / 2.0f) + 0.0001f;
                        }
                        m_player_velocity.z = 0;
                        player_box = getPlayerAABB(position);
                    }
                }
            }
        }
    }
}

// --- 改动点: 增加按键'3'选择草方块 ---
void OpenGLWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_cursor_locked = false;
        setCursor(Qt::ArrowCursor);
    }
    if (event->key() == Qt::Key_1) m_current_block_id = 1; // 原石
    if (event->key() == Qt::Key_2) m_current_block_id = 2; // 泥土
    if (event->key() == Qt::Key_3) m_current_block_id = 3; // 草方块

    if (event->key() == Qt::Key_Space && m_is_on_ground) {
        m_player_velocity.y = JUMP_VELOCITY;
        m_is_on_ground = false;
    }

    if (!event->isAutoRepeat()) {
        m_pressed_keys.insert(event->key());
    }
}

void OpenGLWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        m_pressed_keys.remove(event->key());
    }
}

void OpenGLWindow::mousePressEvent(QMouseEvent *event)
{
    if (!m_cursor_locked) {
        m_cursor_locked = true;
        setCursor(Qt::BlankCursor);
        QCursor::setPos(mapToGlobal(rect().center()));
        m_just_locked_cursor = true;
        return;
    }

    glm::ivec3 hit_block, adjacent_block;
    if (raycast(hit_block, adjacent_block)) {
        if (event->button() == Qt::LeftButton) {
            setBlock(hit_block, 0);
        } else if (event->button() == Qt::RightButton) {
            setBlock(adjacent_block, m_current_block_id);
        }
    }
}

void OpenGLWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_just_locked_cursor) {
        m_just_locked_cursor = false;
        return;
    }
    if (!m_cursor_locked) { return; }

    QPoint center = rect().center();
    QPoint currentPos = event->pos();
    if (currentPos == center) { return; }

    float xoffset = currentPos.x() - center.x();
    float yoffset = center.y() - currentPos.y();
    QCursor::setPos(mapToGlobal(center));

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    m_yaw += xoffset;
    m_pitch += yoffset;

    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;

    updateCameraVectors();
}

void OpenGLWindow::updateCameraVectors()
{
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    m_camera_front = glm::normalize(front);
}


uint8_t OpenGLWindow::getBlock(const glm::ivec3& world_pos) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);
    auto it = m_chunks.find(chunk_coords);
    if (it == m_chunks.end()) {
        return 0;
    }

    Chunk* chunk = it->second;
    glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;
    return chunk->blocks[local_pos.x][local_pos.y][local_pos.z];
}

void OpenGLWindow::setBlock(const glm::ivec3& world_pos, uint8_t block_id) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);

    auto it = m_chunks.find(chunk_coords);
    if (it == m_chunks.end()) {
        qDebug() << "无法在不存在的区块中放置方块。";
        return;
    }

    Chunk* chunk = it->second;
    glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;

    chunk->blocks[local_pos.x][local_pos.y][local_pos.z] = block_id;
    chunk->needs_remeshing = true;

    // Trigger remeshing for adjacent chunks if block is on a chunk border
    if (local_pos.x == 0) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(-1, 0, 0));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
    if (local_pos.x == Chunk::CHUNK_SIZE - 1) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(1, 0, 0));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
    if (local_pos.y == 0) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(0, -1, 0));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
    if (local_pos.y == Chunk::CHUNK_SIZE - 1) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(0, 1, 0));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
    if (local_pos.z == 0) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(0, 0, -1));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
    if (local_pos.z == Chunk::CHUNK_SIZE - 1) {
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(world_pos + glm::ivec3(0, 0, 1));
        if(m_chunks.count(neighbor_chunk_coords)) m_chunks.at(neighbor_chunk_coords)->needs_remeshing = true;
    }
}


glm::ivec3 OpenGLWindow::worldToChunkCoords(const glm::ivec3& world_pos) {
    return {
        (int)floor(world_pos.x / (float)Chunk::CHUNK_SIZE),
        (int)floor(world_pos.y / (float)Chunk::CHUNK_SIZE),
        (int)floor(world_pos.z / (float)Chunk::CHUNK_SIZE)
    };
}

bool OpenGLWindow::raycast(glm::ivec3 &hit_block, glm::ivec3 &adjacent_block)
{
    glm::vec3 ray_origin = m_camera_pos + glm::vec3(0.0f, PLAYER_EYE_LEVEL, 0.0f);
    glm::vec3 ray_direction = m_camera_front;
    if (glm::length2(ray_direction) < 0.0001f) { return false; }

    glm::ivec3 current_pos = glm::floor(ray_origin);
    glm::ivec3 last_pos = current_pos;

    glm::ivec3 step = glm::sign(ray_direction);
    glm::vec3 t_delta = 1.0f / glm::abs(ray_direction);

    glm::vec3 t_max;
    t_max.x = (ray_direction.x > 0.0f) ? (current_pos.x + 1.0f - ray_origin.x) * t_delta.x : (ray_origin.x - current_pos.x) * t_delta.x;
    t_max.y = (ray_direction.y > 0.0f) ? (current_pos.y + 1.0f - ray_origin.y) * t_delta.y : (ray_origin.y - current_pos.y) * t_delta.y;
    t_max.z = (ray_direction.z > 0.0f) ? (current_pos.z + 1.0f - ray_origin.z) * t_delta.z : (ray_origin.z - current_pos.z) * t_delta.z;

    for (int i = 0; i < 10; ++i) {
        last_pos = current_pos;

        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) {
                current_pos.x += step.x; t_max.x += t_delta.x;
            } else {
                current_pos.z += step.z; t_max.z += t_delta.z;
            }
        } else {
            if (t_max.y < t_max.z) {
                current_pos.y += step.y; t_max.y += t_delta.y;
            } else {
                current_pos.z += step.z; t_max.z += t_delta.z;
            }
        }

        if (getBlock(current_pos) != 0) {
            hit_block = current_pos;
            adjacent_block = last_pos;
            return true;
        }
    }
    return false;
}

// ===================================================================
// ===== 核心改动点：buildChunkMesh 函数中的纹理坐标计算 =====
// ===================================================================
void OpenGLWindow::buildChunkMesh(Chunk* chunk, const glm::ivec3& chunk_coords)
{
    makeCurrent();

    std::vector<Vertex> vertices;

    const glm::vec3 face_vertices[6][4] = {
        { {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1} }, // Front (+z)
        { {1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0} }, // Back (-z)
        { {0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0} }, // Top (+y)
        { {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1} }, // Bottom (-y)
        { {1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1} }, // Right (+x)
        { {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0} }  // Left (-x)
    };

    for (int x = 0; x < Chunk::CHUNK_SIZE; ++x) {
        for (int y = 0; y < Chunk::CHUNK_SIZE; ++y) {
            for (int z = 0; z < Chunk::CHUNK_SIZE; ++z) {
                uint8_t block_id = chunk->blocks[x][y][z];
                if (block_id == 0) continue;

                // --- 新增：判断当前方块是否透明 ---
                bool is_current_block_transparent = (block_id == 4);

                glm::ivec3 block_pos(x, y, z);
                const glm::ivec3 neighbors[6] = {
                    { 0,  0,  1}, { 0,  0, -1}, { 0,  1,  0},
                    { 0, -1,  0}, { 1,  0,  0}, {-1,  0,  0}
                };

                for (int i = 0; i < 6; ++i) {
                    glm::ivec3 neighbor_world_pos = chunk_coords * Chunk::CHUNK_SIZE + block_pos + neighbors[i];
                    uint8_t neighbor_id = getBlock(neighbor_world_pos);

                    // --- 核心改动：新的面剔除逻辑 ---
                    bool should_draw_face = false;
                    if (is_current_block_transparent) {
                        // 透明方块(水)只在与不同类型的方块相邻时绘制面
                        if (neighbor_id != block_id) {
                            should_draw_face = true;
                        }
                    } else {
                        // 不透明方块(石头、草等)在与透明方块(空气、水)相邻时绘制面
                        if (neighbor_id == 0 || neighbor_id == 4) {
                            should_draw_face = true;
                        }
                    }

                    if (should_draw_face) {
                        // --- 核心纹理选择逻辑 ---
                        float atlas_width = 5.0f; // !! 改动：图集现在有5个纹理
                        float tile_width = 1.0f / atlas_width;
                        float u_offset = 0.0f;
                        int texture_index = 0;

                        switch (block_id) {
                        case 1: texture_index = 0; break; // 原石
                        case 2: texture_index = 1; break; // 泥土
                        case 3: // 草方块
                            switch (i) {
                            case 2: texture_index = 2; break; // 顶
                            case 3: texture_index = 1; break; // 底
                            default: texture_index = 3; break;// 侧
                            }
                            break;
                        case 4: texture_index = 4; break; // !! 新增：水方块
                        }

                        u_offset = texture_index * tile_width;
                        glm::vec3 block_pos_f = glm::vec3(block_pos);
                        Vertex v1 = { block_pos_f + face_vertices[i][0], { u_offset, 0.0f } };
                        Vertex v2 = { block_pos_f + face_vertices[i][1], { u_offset + tile_width, 0.0f } };
                        Vertex v3 = { block_pos_f + face_vertices[i][2], { u_offset + tile_width, 1.0f } };
                        Vertex v4 = { block_pos_f + face_vertices[i][3], { u_offset, 1.0f } };
                        vertices.push_back(v1); vertices.push_back(v2); vertices.push_back(v3);
                        vertices.push_back(v1); vertices.push_back(v3); vertices.push_back(v4);
                    }
                }
            }
        }
    }

    // --- VBO/VAO 更新 (这部分代码无需改动) ---
    if (!chunk->vao.isCreated()) chunk->vao.create();
    chunk->vao.bind();
    if (!chunk->vbo.isCreated()) {
        chunk->vbo.create();
        chunk->vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    }
    chunk->vbo.bind();
    chunk->vbo.allocate(vertices.data(), vertices.size() * sizeof(Vertex));
    chunk->vertex_count = vertices.size();
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
    chunk->vao.release();
    doneCurrent();
}
