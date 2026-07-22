#ifndef ABERRANT_ENGINE_MESHRENDERER_HPP
#define ABERRANT_ENGINE_MESHRENDERER_HPP

#include "Renderable.hpp"

class MeshRenderer : public Renderable {
    // Model* model;

public:
    // Color modulate;
    // BoundingBox boundingBox;
    // void setModel(Model* model);
    // Model* getModel();
    void Render() override;
};

#endif