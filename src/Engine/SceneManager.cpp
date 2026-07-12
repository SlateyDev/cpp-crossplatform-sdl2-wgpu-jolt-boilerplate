#include "SceneManager.h"

SceneManager& SceneManager::GetInstance() {
    static SceneManager instance;
    return instance;
}

Scene* SceneManager::GetActiveScene() const {
    return activeScene.get();
}
