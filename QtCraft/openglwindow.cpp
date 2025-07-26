#include "openglwindow.h"
#include "block.h"
#include <QDebug>
#include <QImage>
#include <QCursor>
#include <QWheelEvent>
#include <glm/gtx/norm.hpp>
#include <limits>
#include <cstddef>
#include <cmath>
#include <map>
#include <algorithm> // for std::max

const float PLAYER_HEIGHT = 1.8f;
const float PLAYER_WIDTH = 0.6f;
const float PLAYER_EYE_LEVEL = 1.6f;
const float GRAVITY = -28.0f;
const float JUMP_VELOCITY = 9.0f;
const float MOVE_SPEED = 5.0f;

// 新的水中物理常量
const float WATER_GRAVITY = -6.0f;
const float SWIM_VELOCITY = 3.0f;
const float WATER_MOVE_SPEED_MULTIPLIER = 0.6f;
const float MAX_SINK_SPEED = -4.0f;

Chunk::Chunk() {
    memset(blocks, 0, sizeof(blocks));
    memset(lightmap, 0, sizeof(lightmap)); // 初始化光照图
}

Chunk::~Chunk() {
    if (vbo.isCreated()) vbo.destroy();
    if (vao.isCreated()) vao.destroy();
    if (vbo_transparent.isCreated()) vbo_transparent.destroy();
    if (vao_transparent.isCreated()) vao_transparent.destroy();
}

// --- Chunk类的光照函数实现 ---
uint8_t Chunk::getLight(const glm::ivec3& pos) const {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) {
        return 0; // 或者处理跨区块的查询
    }
    uint8_t sky = getSkyLight(pos);
    uint8_t block = getBlockLight(pos);
    return std::max(sky, block);
}

void Chunk::setLight(const glm::ivec3& pos, uint8_t light) {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) return;
    lightmap[pos.x][pos.y][pos.z] = light;
}

uint8_t Chunk::getSkyLight(const glm::ivec3& pos) const {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) return 0;
    return (lightmap[pos.x][pos.y][pos.z] >> 4) & 0x0F;
}

void Chunk::setSkyLight(const glm::ivec3& pos, uint8_t light) {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) return;
    uint8_t current_block_light = getBlockLight(pos);
    lightmap[pos.x][pos.y][pos.z] = (light << 4) | current_block_light;
}

uint8_t Chunk::getBlockLight(const glm::ivec3& pos) const {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) return 0;
    return lightmap[pos.x][pos.y][pos.z] & 0x0F;
}

void Chunk::setBlockLight(const glm::ivec3& pos, uint8_t light) {
    if (pos.x < 0 || pos.x >= CHUNK_SIZE || pos.y < 0 || pos.y >= CHUNK_SIZE || pos.z < 0 || pos.z >= CHUNK_SIZE) return;
    uint8_t current_sky_light = getSkyLight(pos);
    lightmap[pos.x][pos.y][pos.z] = (current_sky_light << 4) | light;
}

OpenGLWindow::OpenGLWindow(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    connect(&m_mesh_builder_watcher, &QFutureWatcher<void>::finished, this, &OpenGLWindow::handleChunkMeshReady);
    connect(&m_timer, &QTimer::timeout, this, &OpenGLWindow::updateGame);
    m_timer.start(16);

    m_elapsed_timer.start();
}

OpenGLWindow::~OpenGLWindow()
{
    makeCurrent();
    m_chunks.clear();
    delete m_texture_atlas;
    delete m_hotbar_texture;
    delete m_hotbar_selector_texture;

    if (m_crosshair_vbo.isCreated()) m_crosshair_vbo.destroy();
    if (m_crosshair_vao.isCreated()) m_crosshair_vao.destroy();
    if (m_ui_vbo.isCreated()) m_ui_vbo.destroy();
    if (m_ui_vao.isCreated()) m_ui_vao.destroy();
    if (m_overlay_vbo.isCreated()) m_overlay_vbo.destroy();
    if (m_overlay_vao.isCreated()) m_overlay_vao.destroy();


    doneCurrent();
}

void OpenGLWindow::initShaders()
{
    // --- 修改顶点着色器 ---
    const char *vsrc = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aTexCoord;
        layout (location = 2) in float aLightLevel; // 新增：接收光照等级属性

        uniform mat4 vp_matrix;
        uniform mat4 model_matrix;

        out vec2 TexCoord;
        out float LightLevel; // 新增：传递给片段着色器

        void main()
        {
            gl_Position = vp_matrix * model_matrix * vec4(aPos, 1.0);
            TexCoord = aTexCoord;
            LightLevel = aLightLevel; // 传递光照等级
        }
    )";

    // --- 修改片段着色器 ---
    const char *fsrc = R"(
        #version 330 core
        out vec4 FragColor;

        in vec2 TexCoord;
        in float LightLevel; // 新增：接收光照等级

        uniform sampler2D texture_atlas;

        void main()
        {
            vec4 texColor = texture(texture_atlas, TexCoord);
            if(texColor.a < 0.1) discard;

            // 根据光照等级调整最终颜色
            // 将 [0, 15] 的光照等级映射到 [0.2, 1] 的亮度因子（0.2作为基础环境光）
            float lightFactor = 0.2 + (LightLevel / 15.0) * 0.8;
            FragColor = vec4(texColor.rgb * lightFactor, texColor.a);
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

    // UI着色器
    const char* ui_vsrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoord;
        uniform mat4 proj_matrix;
        uniform mat4 model_matrix;
        uniform vec2 uv_offset;
        uniform vec2 uv_scale;
        out vec2 TexCoord;
        void main() {
            gl_Position = proj_matrix * model_matrix * vec4(aPos, 0.0, 1.0);
            TexCoord = aTexCoord * uv_scale + uv_offset;
        }
    )";
    const char* ui_fsrc = R"(
        #version 330 core
        out vec4 FragColor;
        in vec2 TexCoord;
        uniform sampler2D ourTexture;
        uniform vec4 ourColor;
        void main() {
            FragColor = texture(ourTexture, TexCoord) * ourColor;
        }
    )";
    if (!m_ui_program.addShaderFromSourceCode(QOpenGLShader::Vertex, ui_vsrc)) qFatal("UI顶点着色器编译失败");
    if (!m_ui_program.addShaderFromSourceCode(QOpenGLShader::Fragment, ui_fsrc)) qFatal("UI片段着色器编译失败");
    if (!m_ui_program.link()) qFatal("UI着色器程序链接失败");

    m_ui_program.bind();
    m_ui_program.setUniformValue("ourTexture", 0);
    m_ui_program.release();

    m_ui_proj_matrix_location = m_ui_program.uniformLocation("proj_matrix");
    m_ui_model_matrix_location = m_ui_program.uniformLocation("model_matrix");
    m_ui_color_location = m_ui_program.uniformLocation("ourColor");
    m_ui_uv_offset_location = m_ui_program.uniformLocation("uv_offset");
    m_ui_uv_scale_location = m_ui_program.uniformLocation("uv_scale");

    // --- 新增：初始化水下效果叠加层着色器 ---
    const char* overlay_vsrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        void main() {
            gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
        }
    )";
    const char* overlay_fsrc = R"(
        #version 330 core
        out vec4 FragColor;
        uniform vec4 overlay_color;
        void main() {
            FragColor = overlay_color;
        }
    )";

    if (!m_overlay_program.addShaderFromSourceCode(QOpenGLShader::Vertex, overlay_vsrc)) qFatal("叠加层顶点着色器编译失败");
    if (!m_overlay_program.addShaderFromSourceCode(QOpenGLShader::Fragment, overlay_fsrc)) qFatal("叠加层片段着色器编译失败");
    if (!m_overlay_program.link()) qFatal("叠加层着色器程序链接失败");

    m_overlay_color_location = m_overlay_program.uniformLocation("overlay_color");
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

    QImage hotbar_image(":/hotbar.png");
    if (hotbar_image.isNull()) {
        qWarning() << "错误：无法从资源加载 ':/hotbar.png'。请检查qrc文件和路径。";
        qFatal("hotbar.png 纹理加载失败，程序终止。");
    }
    m_hotbar_texture = new QOpenGLTexture(hotbar_image.convertToFormat(QImage::Format_RGBA8888).mirrored());
    m_hotbar_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_hotbar_texture->setMinificationFilter(QOpenGLTexture::Nearest);


    QImage selector_image(":/hotbar_selector.png");
    if (selector_image.isNull()) {
        qWarning() << "错误：无法从资源加载 ':/hotbar_selector.png'。请检查qrc文件和路径。";
        qFatal("hotbar_selector.png 纹理加载失败，程序终止。");
    }
    m_hotbar_selector_texture = new QOpenGLTexture(selector_image.convertToFormat(QImage::Format_RGBA8888).mirrored());
    m_hotbar_selector_texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_hotbar_selector_texture->setMinificationFilter(QOpenGLTexture::Nearest);
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

void OpenGLWindow::initInventoryBar()
{
    float vertices[] = {
        // pos      // tex
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,

        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f
    };

    m_ui_vao.create();
    m_ui_vbo.create();

    m_ui_vao.bind();
    m_ui_vbo.bind();
    m_ui_vbo.allocate(vertices, sizeof(vertices));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    m_ui_vao.release();
    m_ui_vbo.release();
}

void OpenGLWindow::initOverlay() {
    float vertices[] = {
        // 三角形 1
        -1.0f, -1.0f,
        1.0f, -1.0f,
        1.0f,  1.0f,
        // 三角形 2
        1.0f,  1.0f,
        -1.0f,  1.0f,
        -1.0f, -1.0f
    };

    m_overlay_vao.create();
    m_overlay_vao.bind();

    m_overlay_vbo.create();
    m_overlay_vbo.bind();
    m_overlay_vbo.allocate(vertices, sizeof(vertices));

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    m_overlay_vao.release();
    m_overlay_vbo.release();
}

void OpenGLWindow::initializeGL()
{
    initializeOpenGLFunctions();
    initShaders();
    initTextures();
    initCrosshair();
    initInventoryBar();
    initOverlay(); // 调用新的初始化函数
    generateWorld();
    glClearColor(0.39f, 0.58f, 0.93f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

void OpenGLWindow::generateChunk(Chunk* chunk, const glm::ivec3& chunk_coords)
{
    FastNoiseLite noise;
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);

    FastNoiseLite distortion_noise;
    distortion_noise.SetNoiseType(FastNoiseLite::NoiseType_Perlin);
    distortion_noise.SetFrequency(0.05f);

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
            int sea_level = 8;
            int terrain_height = static_cast<int>(total_noise) + sea_level;

            for (int y = 0; y < Chunk::CHUNK_SIZE; ++y) {
                int world_y = chunk_coords.y * Chunk::CHUNK_SIZE + y;

                BlockType blockToPlace = BlockType::Air;
                if (world_y > terrain_height) {
                    if (world_y <= sea_level) {
                        blockToPlace = BlockType::Water;
                    }
                } else {
                    if (world_y == terrain_height && world_y > sea_level) {
                        blockToPlace = BlockType::Grass;
                    } else if (world_y > terrain_height - 5) {
                        blockToPlace = BlockType::Dirt;
                    } else {
                        blockToPlace = BlockType::Stone;
                    }
                }
                chunk->blocks[x][y][z] = static_cast<uint8_t>(blockToPlace);
            }
        }
    }
}

// --- 简单的天空光照传播 ---
void OpenGLWindow::propagateLight() {
    qDebug() << "开始计算光照...";
    for (auto const& [coords, chunk_ptr] : m_chunks) {
        for (int x = 0; x < Chunk::CHUNK_SIZE; ++x) {
            for (int z = 0; z < Chunk::CHUNK_SIZE; ++z) {
                // 从上到下扫描天空光
                uint8_t current_sky_light = Light::MAX_SKY_LIGHT;
                for (int y = Chunk::CHUNK_SIZE - 1; y >= 0; --y) {
                    BlockType block_type = static_cast<BlockType>(chunk_ptr->blocks[x][y][z]);
                    if (block_type != BlockType::Air && block_type != BlockType::Water) {
                        current_sky_light = 0; // 遇到不透明方块，光线被阻挡
                    }
                    chunk_ptr->setSkyLight({x,y,z}, current_sky_light);
                }
            }
        }
    }
    qDebug() << "光照计算完成。";
}

void OpenGLWindow::generateWorld() {
    int world_size_in_chunks = 24;
    int world_height_in_chunks = 8;

    for (int x = -world_size_in_chunks / 2; x < world_size_in_chunks / 2; ++x) {
        for (int z = -world_size_in_chunks / 2; z < world_size_in_chunks / 2; ++z) {
            for (int y = -1; y < world_height_in_chunks-1; ++y) {
                glm::ivec3 chunk_coords(x, y, z);
                auto new_chunk = std::make_unique<Chunk>();
                new_chunk->coords = chunk_coords;
                generateChunk(new_chunk.get(), chunk_coords);
                m_chunks[chunk_coords] = std::move(new_chunk);
            }
        }
    }
    qDebug() << "生成了" << m_chunks.size() << "个区块。";

    // --- 新增：在世界生成后计算一次光照 ---
    propagateLight();

    for(auto const& [coords, chunk] : m_chunks){
        chunk->needs_remeshing = true;
    }
}


void OpenGLWindow::resizeGL(int w, int h)
{
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
}

void OpenGLWindow::updateGame()
{
    float delta_time = m_elapsed_timer.restart() / 1000.0f;

    processInput();
    updatePhysics(delta_time);

    m_ready_chunks_mutex.lock();
    if (!m_ready_chunks.isEmpty()) {
        makeCurrent();
        for (Chunk* chunk : m_ready_chunks) {
            if(chunk->mesh_data.size() > 0) {
                if (!chunk->vao.isCreated()) chunk->vao.create();
                chunk->vao.bind();

                if (!chunk->vbo.isCreated()) {
                    chunk->vbo.create();
                    chunk->vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
                }
                chunk->vbo.bind();
                chunk->vbo.allocate(chunk->mesh_data.data(), chunk->mesh_data.size() * sizeof(Vertex));
                chunk->vertex_count = chunk->mesh_data.size();

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
                // --- 新增：设置光照属性指针 ---
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, lightLevel));

                chunk->vao.release();
            }
            chunk->mesh_data.clear();
            chunk->mesh_data.shrink_to_fit();

            if(chunk->mesh_data_transparent.size() > 0) {
                if (!chunk->vao_transparent.isCreated()) chunk->vao_transparent.create();
                chunk->vao_transparent.bind();

                if (!chunk->vbo_transparent.isCreated()) {
                    chunk->vbo_transparent.create();
                    chunk->vbo_transparent.setUsagePattern(QOpenGLBuffer::DynamicDraw);
                }
                chunk->vbo_transparent.bind();
                chunk->vbo_transparent.allocate(chunk->mesh_data_transparent.data(), chunk->mesh_data_transparent.size() * sizeof(Vertex));
                chunk->vertex_count_transparent = chunk->mesh_data_transparent.size();

                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoord));
                // --- 新增：设置光照属性指针 ---
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, lightLevel));

                chunk->vao_transparent.release();
            }
            chunk->mesh_data_transparent.clear();
            chunk->mesh_data_transparent.shrink_to_fit();

            chunk->is_building = false;
        }
        doneCurrent();
        m_ready_chunks.clear();
    }
    m_ready_chunks_mutex.unlock();

    for (auto const& [coords, chunk] : m_chunks) {
        if (chunk->needs_remeshing && !chunk->is_building) {
            chunk->is_building = true;
            chunk->needs_remeshing = false;
            QtConcurrent::run(this, &OpenGLWindow::buildChunkMesh, chunk.get());
        }
    }

    update();
}

void OpenGLWindow::handleChunkMeshReady() {}


void OpenGLWindow::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);

    m_program.bind();
    glActiveTexture(GL_TEXTURE0);
    m_texture_atlas->bind();

    glm::vec3 player_pos_backup = m_camera.Position;
    m_camera.Position.y += PLAYER_EYE_LEVEL;
    glm::mat4 view = m_camera.GetViewMatrix();
    m_camera.Position = player_pos_backup;

    float aspect_ratio = float(width()) / float(height());
    glm::mat4 projection = glm::perspective(glm::radians(m_camera.Zoom), aspect_ratio, 0.1f, 500.0f);

    m_camera.UpdateFrustum(projection, view);

    glm::mat4 vp = projection * view;
    glUniformMatrix4fv(m_vp_matrix_location, 1, GL_FALSE, glm::value_ptr(vp));

    glDepthMask(GL_TRUE);
    for (auto const& [coords, chunk_ptr] : m_chunks) {
        Chunk* chunk = chunk_ptr.get();
        glm::vec3 min_aabb = glm::vec3(coords * Chunk::CHUNK_SIZE);
        glm::vec3 max_aabb = min_aabb + glm::vec3(Chunk::CHUNK_SIZE);

        if (chunk->vertex_count > 0 && chunk->vao.isCreated()&& m_camera.IsBoxInFrustum(min_aabb, max_aabb)) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(coords * Chunk::CHUNK_SIZE));
            glUniformMatrix4fv(m_model_matrix_location, 1, GL_FALSE, glm::value_ptr(model));
            chunk->vao.bind();
            glDrawArrays(GL_TRIANGLES, 0, chunk->vertex_count);
            chunk->vao.release();
        }
    }

    std::multimap<float, Chunk*> sorted_transparent_chunks;
    for (auto const& [coords, chunk_ptr] : m_chunks) {
        Chunk* chunk = chunk_ptr.get();
        if (chunk->vertex_count_transparent > 0) {
            glm::vec3 chunk_center = glm::vec3(chunk->coords * Chunk::CHUNK_SIZE) + glm::vec3(Chunk::CHUNK_SIZE / 2.0f);
            float dist = glm::distance2(m_camera.Position, chunk_center);
            sorted_transparent_chunks.insert({dist, chunk});
        }
    }

    glDepthMask(GL_FALSE);
    for (auto it = sorted_transparent_chunks.rbegin(); it != sorted_transparent_chunks.rend(); ++it) {
        Chunk* chunk = it->second;
        glm::vec3 min_aabb = glm::vec3(chunk->coords * Chunk::CHUNK_SIZE);
        glm::vec3 max_aabb = min_aabb + glm::vec3(Chunk::CHUNK_SIZE);

        if (chunk->vao_transparent.isCreated() && m_camera.IsBoxInFrustum(min_aabb, max_aabb)) {
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(chunk->coords * Chunk::CHUNK_SIZE));
            glUniformMatrix4fv(m_model_matrix_location, 1, GL_FALSE, glm::value_ptr(model));
            chunk->vao_transparent.bind();
            glDrawArrays(GL_TRIANGLES, 0, chunk->vertex_count_transparent);
            chunk->vao_transparent.release();
        }
    }
    glDepthMask(GL_TRUE);

    m_program.release();

    if (m_is_in_water) {
        glDisable(GL_DEPTH_TEST);

        m_overlay_program.bind();
        m_overlay_program.setUniformValue(m_overlay_color_location, QVector4D(0.1f, 0.4f, 0.8f, 0.4f));

        m_overlay_vao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_overlay_vao.release();
        m_overlay_program.release();
    }


    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_ui_program.bind();
    glm::mat4 ui_projection = glm::ortho(0.0f, (float)width(), 0.0f, (float)height());
    glUniformMatrix4fv(m_ui_proj_matrix_location, 1, GL_FALSE, glm::value_ptr(ui_projection));
    glUniform4f(m_ui_color_location, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform2f(m_ui_uv_offset_location, 0.0f, 0.0f);
    glUniform2f(m_ui_uv_scale_location, 1.0f, 1.0f);

    m_ui_vao.bind();

    float hotbar_width = 364.0f;
    float hotbar_height = 44.0f;
    float hotbar_x = (width() - hotbar_width) / 2.0f;
    float hotbar_y = 0.0f;
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(hotbar_x, hotbar_y, 0.0f));
    model = glm::scale(model, glm::vec3(hotbar_width, hotbar_height, 1.0f));
    glUniformMatrix4fv(m_ui_model_matrix_location, 1, GL_FALSE, glm::value_ptr(model));
    m_hotbar_texture->bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);

    m_texture_atlas->bind();
    float item_icon_size = 32.0f;
    glUniform2f(m_ui_uv_scale_location, Texture::TileWidth, 1.0f);

    for (int i = 0; i < INVENTORY_SLOTS; ++i) {
        BlockType item_type = m_inventory.getItem(i).type;
        if (item_type != BlockType::Air) {
            int texture_index = 0;
            switch (item_type) {
            case BlockType::Stone: texture_index = Texture::Stone; break;
            case BlockType::Dirt:  texture_index = Texture::Dirt;  break;
            case BlockType::Grass: texture_index = Texture::GrassSide; break;
            case BlockType::Water: texture_index = Texture::Water; break;
            default: continue;
            }
            float u_offset = texture_index * Texture::TileWidth;
            glUniform2f(m_ui_uv_offset_location, u_offset, 0.0f);

            float item_x = hotbar_x + 6.0f + (i * 40.0f);
            float item_y = hotbar_y + 6.0f;
            glm::mat4 item_model = glm::mat4(1.0f);
            item_model = glm::translate(item_model, glm::vec3(item_x, item_y, 0.0f));
            item_model = glm::scale(item_model, glm::vec3(item_icon_size, item_icon_size, 1.0f));
            glUniformMatrix4fv(m_ui_model_matrix_location, 1, GL_FALSE, glm::value_ptr(item_model));

            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    glUniform2f(m_ui_uv_offset_location, 0.0f, 0.0f);
    glUniform2f(m_ui_uv_scale_location, 1.0f, 1.0f);
    float selector_size = 48.0f;
    float selector_x = hotbar_x - 2.0f + (m_inventory.getSelectedSlot() * 40.0f);
    float selector_y = hotbar_y - 2.0f;
    model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(selector_x, selector_y, 0.0f));
    model = glm::scale(model, glm::vec3(selector_size, selector_size, 1.0f));
    glUniformMatrix4fv(m_ui_model_matrix_location, 1, GL_FALSE, glm::value_ptr(model));
    m_hotbar_selector_texture->bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);

    m_ui_vao.release();
    m_ui_program.release();

    glEnable(GL_CULL_FACE);

    m_crosshair_program.bind();
    glm::mat4 crosshair_proj = glm::ortho(-width()/2.0f, width()/2.0f, -height()/2.0f, height()/2.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(m_crosshair_proj_matrix_location, 1, GL_FALSE, glm::value_ptr(crosshair_proj));
    m_crosshair_vao.bind();
    glDrawArrays(GL_LINES, 0, 4);
    m_crosshair_vao.release();
    m_crosshair_program.release();
}

void OpenGLWindow::processInput()
{
}

void OpenGLWindow::updatePhysics(float deltaTime)
{
    // --- 1. 判断玩家环境 ---
    glm::ivec3 player_head_pos = glm::floor(m_camera.Position + glm::vec3(0.0f, PLAYER_EYE_LEVEL, 0.0f));
    BlockType block_at_head = static_cast<BlockType>(getBlock(player_head_pos));
    m_is_in_water = (block_at_head == BlockType::Water);

    // --- 2. 获取玩家输入 ---
    glm::vec3 inputVelocity(0.0f);
    glm::vec3 flat_front = glm::normalize(glm::vec3(m_camera.Front.x, 0.0f, m_camera.Front.z));
    glm::vec3 flat_right = glm::normalize(glm::cross(flat_front, glm::vec3(0.0,1.0,0.0)));


    if (m_pressed_keys.contains(Qt::Key_W)) inputVelocity += flat_front;
    if (m_pressed_keys.contains(Qt::Key_S)) inputVelocity -= flat_front;
    if (m_pressed_keys.contains(Qt::Key_A)) inputVelocity -= flat_right;
    if (m_pressed_keys.contains(Qt::Key_D)) inputVelocity += flat_right;

    // --- 3. 根据环境应用物理规则 ---
    if (m_is_in_water) {
        m_is_on_ground = false;

        m_player_velocity.y += WATER_GRAVITY * deltaTime;

        if (m_pressed_keys.contains(Qt::Key_Space)) {
            m_player_velocity.y = SWIM_VELOCITY;
        }

        if (m_player_velocity.y < MAX_SINK_SPEED) {
            m_player_velocity.y = MAX_SINK_SPEED;
        }

        if (glm::length(inputVelocity) > 0.0f) {
            inputVelocity = glm::normalize(inputVelocity) * MOVE_SPEED * WATER_MOVE_SPEED_MULTIPLIER;
        }

    } else {
        m_player_velocity.y += GRAVITY * deltaTime;

        if (m_pressed_keys.contains(Qt::Key_Space) && m_is_on_ground) {
            m_player_velocity.y = JUMP_VELOCITY;
            m_is_on_ground = false;
        }

        if (glm::length(inputVelocity) > 0.0f) {
            inputVelocity = glm::normalize(inputVelocity) * MOVE_SPEED;
        }
    }

    // --- 4. 应用最终速度并进行碰撞检测 ---
    m_player_velocity.x = inputVelocity.x;
    m_player_velocity.z = inputVelocity.z;

    resolveCollisions(m_camera.Position, m_player_velocity * deltaTime);
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
    AABB player_box;

    position.x += velocity.x;
    player_box = getPlayerAABB(position);

    for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
        for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
            for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
                BlockType block_type = static_cast<BlockType>(getBlock({x, y, z}));
                if (block_type != BlockType::Air && block_type != BlockType::Water) {
                    AABB block_box = {glm::vec3(x, y, z), glm::vec3(x + 1, y + 1, z + 1)};
                    if (player_box.max.x > block_box.min.x && player_box.min.x < block_box.max.x &&
                        player_box.max.y > block_box.min.y && player_box.min.y < block_box.max.y &&
                        player_box.max.z > block_box.min.z && player_box.min.z < block_box.max.z)
                    {
                        if (velocity.x > 0) position.x = block_box.min.x - (PLAYER_WIDTH / 2.0f) - 0.0001f;
                        else if (velocity.x < 0) position.x = block_box.max.x + (PLAYER_WIDTH / 2.0f) + 0.0001f;
                        player_box = getPlayerAABB(position);
                    }
                }
            }
        }
    }

    position.z += velocity.z;
    player_box = getPlayerAABB(position);

    for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
        for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
            for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
                BlockType block_type = static_cast<BlockType>(getBlock({x, y, z}));
                if (block_type != BlockType::Air && block_type != BlockType::Water) {
                    AABB block_box = {glm::vec3(x, y, z), glm::vec3(x + 1, y + 1, z + 1)};
                    if (player_box.max.x > block_box.min.x && player_box.min.x < block_box.max.x &&
                        player_box.max.y > block_box.min.y && player_box.min.y < block_box.max.y &&
                        player_box.max.z > block_box.min.z && player_box.min.z < block_box.max.z)
                    {
                        if (velocity.z > 0) position.z = block_box.min.z - (PLAYER_WIDTH / 2.0f) - 0.0001f;
                        else if (velocity.z < 0) position.z = block_box.max.z + (PLAYER_WIDTH / 2.0f) + 0.0001f;
                        player_box = getPlayerAABB(position);
                    }
                }
            }
        }
    }

    position.y += velocity.y;
    player_box = getPlayerAABB(position);

    for (int y = floor(player_box.min.y); y <= floor(player_box.max.y); ++y) {
        for (int x = floor(player_box.min.x); x <= floor(player_box.max.x); ++x) {
            for (int z = floor(player_box.min.z); z <= floor(player_box.max.z); ++z) {
                BlockType block_type = static_cast<BlockType>(getBlock({x, y, z}));
                if (block_type != BlockType::Air && block_type != BlockType::Water) {
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
}


void OpenGLWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        m_cursor_locked = false;
        setCursor(Qt::ArrowCursor);
    }
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
        m_inventory.setSlot(event->key() - Qt::Key_1);
    }

    if (!event->isAutoRepeat()) m_pressed_keys.insert(event->key());
}

void OpenGLWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) m_pressed_keys.remove(event->key());
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
            setBlock(hit_block, BlockType::Air);
        }
        else if (event->button() == Qt::RightButton) {
            BlockType selected_block = m_inventory.getSelectedBlockType();
            if (selected_block != BlockType::Air) {
                setBlock(adjacent_block, selected_block);
            }
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

    m_camera.ProcessMouseMovement(xoffset, yoffset);
}

void OpenGLWindow::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0) {
        m_inventory.prevSlot();
    } else if (event->angleDelta().y() < 0) {
        m_inventory.nextSlot();
    }
    update();
}


uint8_t OpenGLWindow::getBlock(const glm::ivec3& world_pos) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);
    auto it = m_chunks.find(chunk_coords);
    if (it == m_chunks.end()) return static_cast<uint8_t>(BlockType::Air);

    Chunk* chunk = it->second.get();
    glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;

    // 边界检查
    if (local_pos.x < 0 || local_pos.x >= Chunk::CHUNK_SIZE ||
        local_pos.y < 0 || local_pos.y >= Chunk::CHUNK_SIZE ||
        local_pos.z < 0 || local_pos.z >= Chunk::CHUNK_SIZE)
    {
        // 跨区块查询，这里简化处理，实际应查询邻近区块
        return static_cast<uint8_t>(BlockType::Air);
    }

    return chunk->blocks[local_pos.x][local_pos.y][local_pos.z];
}

// --- OpenGLWindow类的光照获取/设置函数 ---
uint8_t OpenGLWindow::getLight(const glm::ivec3& world_pos) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);
    auto it = m_chunks.find(chunk_coords);
    if (it == m_chunks.end()) {
        int world_size_in_chunks = 24;
        int world_height_in_chunks = 8;
        int world_height = world_height_in_chunks * Chunk::CHUNK_SIZE;
        if (world_pos.y >= world_height / 2) return Light::MAX_SKY_LIGHT;
        return 0;
    }

    Chunk* chunk = it->second.get();
    glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;
    return chunk->getLight(local_pos);
}

void OpenGLWindow::setLight(const glm::ivec3& world_pos, uint8_t light) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);
    auto it = m_chunks.find(chunk_coords);
    if (it != m_chunks.end()) {
        Chunk* chunk = it->second.get();
        glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;
        chunk->setLight(local_pos, light);
        chunk->needs_remeshing = true;
    }
}

void OpenGLWindow::setBlock(const glm::ivec3& world_pos, BlockType block_id) {
    glm::ivec3 chunk_coords = worldToChunkCoords(world_pos);
    auto it = m_chunks.find(chunk_coords);
    if (it == m_chunks.end()) {
        qDebug() << "无法在不存在的区块中放置方块。";
        return;
    }

    Chunk* chunk = it->second.get();
    glm::ivec3 local_pos = world_pos - chunk_coords * Chunk::CHUNK_SIZE;

    chunk->blocks[local_pos.x][local_pos.y][local_pos.z] = static_cast<uint8_t>(block_id);

    // --- 在放置/破坏方块后，需要重新计算光照并更新相关区块 ---
    propagateLight();

    chunk->needs_remeshing = true;

    auto trigger_neighbor_remesh = [&](const glm::ivec3& offset) {
        glm::ivec3 neighbor_world_pos = world_pos + offset;
        glm::ivec3 neighbor_chunk_coords = worldToChunkCoords(neighbor_world_pos);
        if (neighbor_chunk_coords != chunk_coords) {
            auto neighbor_it = m_chunks.find(neighbor_chunk_coords);
            if (neighbor_it != m_chunks.end()) {
                neighbor_it->second->needs_remeshing = true;
            }
        }
    };

    if (local_pos.x == 0) trigger_neighbor_remesh({-1, 0, 0});
    if (local_pos.x == Chunk::CHUNK_SIZE - 1) trigger_neighbor_remesh({1, 0, 0});
    if (local_pos.y == 0) trigger_neighbor_remesh({0, -1, 0});
    if (local_pos.y == Chunk::CHUNK_SIZE - 1) trigger_neighbor_remesh({0, 1, 0});
    if (local_pos.z == 0) trigger_neighbor_remesh({0, 0, -1});
    if (local_pos.z == Chunk::CHUNK_SIZE - 1) trigger_neighbor_remesh({0, 0, 1});
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
    glm::vec3 ray_origin = m_camera.Position + glm::vec3(0.0f, PLAYER_EYE_LEVEL, 0.0f);
    glm::vec3 ray_direction = m_camera.Front;
    if (glm::length2(ray_direction) < 0.0001f) { return false; }

    glm::ivec3 current_pos = glm::floor(ray_origin);
    glm::ivec3 last_pos;

    glm::ivec3 step = glm::sign(ray_direction);
    glm::vec3 t_delta = 1.0f / glm::abs(ray_direction);
    glm::vec3 t_max;

    t_max.x = (ray_direction.x > 0.0f) ? (current_pos.x + 1.0f - ray_origin.x) * t_delta.x : (ray_origin.x - current_pos.x) * t_delta.x;
    t_max.y = (ray_direction.y > 0.0f) ? (current_pos.y + 1.0f - ray_origin.y) * t_delta.y : (ray_origin.y - current_pos.y) * t_delta.y;
    t_max.z = (ray_direction.z > 0.0f) ? (current_pos.z + 1.0f - ray_origin.z) * t_delta.z : (ray_origin.z - current_pos.z) * t_delta.z;

    for (int i = 0; i < 100; ++i) {
        last_pos = current_pos;

        if (t_max.x < t_max.y) {
            if (t_max.x < t_max.z) { current_pos.x += step.x; t_max.x += t_delta.x; }
            else { current_pos.z += step.z; t_max.z += t_delta.z; }
        } else {
            if (t_max.y < t_max.z) { current_pos.y += step.y; t_max.y += t_delta.y; }
            else { current_pos.z += step.z; t_max.z += t_delta.z; }
        }

        if (getBlock(current_pos) != static_cast<uint8_t>(BlockType::Air)) {
            hit_block = current_pos;
            adjacent_block = last_pos;
            return true;
        }
    }
    return false;
}

void OpenGLWindow::buildChunkMesh(Chunk* chunk)
{
    std::vector<Vertex> vertices_opaque;
    std::vector<Vertex> vertices_transparent;

    const glm::ivec3& chunk_coords = chunk->coords;

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
                BlockType block_id = static_cast<BlockType>(chunk->blocks[x][y][z]);
                if (block_id == BlockType::Air) continue;

                glm::ivec3 block_pos(x, y, z);
                const glm::ivec3 neighbors[6] = {
                    {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}
                };

                for (int i = 0; i < 6; ++i) {
                    glm::ivec3 neighbor_world_pos = chunk_coords * Chunk::CHUNK_SIZE + block_pos + neighbors[i];
                    BlockType neighbor_id = static_cast<BlockType>(getBlock(neighbor_world_pos));
                    uint8_t neighbor_light_level = getLight(neighbor_world_pos);

                    bool is_neighbor_transparent = (neighbor_id == BlockType::Water);

                    bool should_draw_face = false;
                    if (block_id == BlockType::Water) {
                        if (neighbor_id != BlockType::Water) {
                            should_draw_face = true;
                        }
                    } else {
                        if (neighbor_id == BlockType::Air || is_neighbor_transparent) {
                            should_draw_face = true;
                        }
                    }

                    if (should_draw_face) {
                        int texture_index = 0;
                        switch (block_id) {
                        case BlockType::Stone: texture_index = Texture::Stone; break;
                        case BlockType::Dirt:  texture_index = Texture::Dirt;  break;
                        case BlockType::Grass:
                            switch (i) {
                            case 2:  texture_index = Texture::GrassTop; break;
                            case 3:  texture_index = Texture::Dirt;     break;
                            default: texture_index = Texture::GrassSide;break;
                            }
                            break;
                        case BlockType::Water: texture_index = Texture::Water; break;
                        default: continue;
                        }

                        float u_offset = texture_index * Texture::TileWidth;
                        glm::vec3 block_pos_f = glm::vec3(block_pos);

                        Vertex v[4];
                        v[0] = { block_pos_f + face_vertices[i][0], { u_offset, 0.0f }, (float)neighbor_light_level };
                        v[1] = { block_pos_f + face_vertices[i][1], { u_offset + Texture::TileWidth, 0.0f }, (float)neighbor_light_level };
                        v[2] = { block_pos_f + face_vertices[i][2], { u_offset + Texture::TileWidth, 1.0f }, (float)neighbor_light_level };
                        v[3] = { block_pos_f + face_vertices[i][3], { u_offset, 1.0f }, (float)neighbor_light_level };

                        if (block_id == BlockType::Water) {
                            glm::ivec3 pos_above = chunk_coords * Chunk::CHUNK_SIZE + block_pos + glm::ivec3(0, 1, 0);
                            BlockType block_above = static_cast<BlockType>(getBlock(pos_above));

                            if (block_above == BlockType::Air) {
                                for(int k = 0; k < 4; ++k) {
                                    if(face_vertices[i][k].y == 1.0f) {
                                        v[k].position.y -= 0.2f;
                                    }
                                }
                            }
                        }

                        if (block_id == BlockType::Water) {
                            vertices_transparent.push_back(v[0]); vertices_transparent.push_back(v[1]); vertices_transparent.push_back(v[2]);
                            vertices_transparent.push_back(v[0]); vertices_transparent.push_back(v[2]); vertices_transparent.push_back(v[3]);

                            if (i == 2) {
                                vertices_transparent.push_back(v[0]); vertices_transparent.push_back(v[2]); vertices_transparent.push_back(v[1]);
                                vertices_transparent.push_back(v[0]); vertices_transparent.push_back(v[3]); vertices_transparent.push_back(v[2]);
                            }
                        } else {
                            vertices_opaque.push_back(v[0]); vertices_opaque.push_back(v[1]); vertices_opaque.push_back(v[2]);
                            vertices_opaque.push_back(v[0]); vertices_opaque.push_back(v[2]); vertices_opaque.push_back(v[3]);
                        }
                    }
                }
            }
        }
    }

    chunk->mesh_data = std::move(vertices_opaque);
    chunk->mesh_data_transparent = std::move(vertices_transparent);
    m_ready_chunks_mutex.lock();
    m_ready_chunks.append(chunk);
    m_ready_chunks_mutex.unlock();
}
