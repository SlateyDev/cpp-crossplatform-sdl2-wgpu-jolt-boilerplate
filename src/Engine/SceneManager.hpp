#ifndef ABERRANT_ENGINE_SCENEMANAGER_HPP
#define ABERRANT_ENGINE_SCENEMANAGER_HPP

#include <memory>

#include "Scene.hpp"

class SceneManager {
    SceneManager() = default;
    std::unique_ptr<Scene> activeScene = std::make_unique<Scene>();

public:
    static SceneManager& GetInstance();

    Scene* GetActiveScene() const;
};

#endif