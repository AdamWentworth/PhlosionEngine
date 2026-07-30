// Card.h

#pragma once

#include "engine/ui/Rect.h"
#include <glm/glm.hpp>
#include <string>

class Shader;

enum class CardType {
    Starter,
    Shop,
    Bench,
    Item
};

struct CardData {
    std::string pokemonName;
    int cost = 0;
    int level = 0;
    std::string label;
    std::string imagePath;
    glm::vec2 uvMin{0.0f, 0.0f};
    glm::vec2 uvMax{1.0f, 1.0f};
    CardType type = CardType::Shop;
};

class Card {
public:
    Card(const ui::Rect& rect, const std::string& imagePath);
    Card(const Card&) = delete;
    Card& operator=(const Card&) = delete;
    Card(Card&& other) noexcept;
    Card& operator=(Card&& other) noexcept;
    ~Card();

    void draw(Shader* uiShader) const;
    bool isPointInside(int x, int y) const;

    void setRect(const ui::Rect& r) { rect = r; }
    ui::Rect getRect() const { return rect; }

    void setImagePath(const std::string& path) { imagePath = path; }
    std::string getImagePath() const { return imagePath; }

    void setData(const CardData& data) { cardData = data; }
    const CardData& getData() const { return cardData; }

    static void setGlobalFramePath(const std::string& path);

    // NEW: cleanup for shared GL resources (static VAO/VBO/EBO + static frame texture)
    // Call while GL context is alive (wired via UIManager::shutdown()).
    static void shutdownSharedGL();

private:
    ui::Rect rect;
    std::string imagePath;
    unsigned int textureID;
    int imgWidth, imgHeight, imgChannels;

    CardData cardData;

    static std::string framePath;
    static unsigned int frameTextureID;
    static bool frameLoaded;

    unsigned int loadTexture(const std::string& path);
    static void loadFrameTexture();
};
