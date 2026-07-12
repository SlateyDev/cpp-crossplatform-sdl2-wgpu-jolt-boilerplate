#pragma once

#include <memory>

#include "Scene.h"

class SceneManager {
    SceneManager() = default;
    std::unique_ptr<Scene> activeScene = std::make_unique<Scene>();

public:
    static SceneManager& GetInstance();

    Scene* GetActiveScene() const;
};
