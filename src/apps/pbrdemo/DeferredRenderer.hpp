#pragma once

#include "DeferredLight.hpp"
#include "ForwardLight.hpp"

#include "renderers/DebugLightRenderer.hpp"

#include "AmbientQuad.hpp"
#include "scene/Scene.hpp"
#include "renderers/Renderer.hpp"
#include "renderers/Culler.hpp"

#include "graphics/Framebuffer.hpp"
#include "input/ControllableCamera.hpp"

#include "processing/SSAO.hpp"

#include "Common.hpp"

/**
 \brief Available G-buffer layers.
 \ingroup PBRDemo
 */
enum class TextureType {
	Albedo  = 0, ///< (or base color)
	Normal  = 1,
	Effects = 2, ///< Roughness, metallicness, ambient occlusion factor, ID.
	Depth   = 3
};

/**
 \brief Performs deferred rendering of a scene.
 \sa DeferredLight
 \ingroup PBRDemo
 */
class DeferredRenderer final : public Renderer {

public:
	/** Constructor.
	 \param resolution the initial rendering resolution
	 \param mode the shadow rendering algorithm
	 \param ssao should screen space ambient occlusion be computed
	 \param name the debug name
	 */
	explicit DeferredRenderer(const glm::vec2 & resolution, ShadowMode mode, bool ssao, const std::string & name);

	/** Set the scene to render.
	 \param scene the new scene
	 */
	void setScene(const std::shared_ptr<Scene> & scene);

	/** \copydoc Renderer::draw */
	void draw(const Camera & camera, Framebuffer & framebuffer, size_t layer = 0) override;

	/** \copydoc Renderer::resize
	 */
	void resize(unsigned int width, unsigned int height) override;

	/** \copydoc Renderer::interface */
	void interface() override;

	/** \return the framebuffer containing the scene depth information */
	const Framebuffer * sceneDepth() const;

	/** \return the texture containing the scene normal information */
	const Texture * sceneNormal() const;

private:

	/** Render the scene opaque objects.
	 \param visibles list of indices of visible objects
	 \param view the camera view matrix
	 \param proj the camera projection matrix
	 */
	void renderOpaque(const Culler::List & visibles, const glm::mat4 & view, const glm::mat4 & proj);

	/** Render the scene transparent objects.
	 \param visibles list of indices of visible objects 
	 \param view the camera view matrix
	 \param proj the camera projection matrix
	 */
	void renderTransparent(const Culler::List & visibles, const glm::mat4 & view, const glm::mat4 & proj);
	
	/** Render the scene background to the G-buffer.
	 \param view the camera view matrix
	 \param proj the camera projection matrix
	 \param pos the camera position
	 */
	void renderBackground(const glm::mat4 & view, const glm::mat4 & proj, const glm::vec3 & pos);

	std::unique_ptr<Framebuffer> _gbuffer;			///< G-buffer.
	std::unique_ptr<Framebuffer> _lightBuffer;		///< Lighting accumulation.
	std::unique_ptr<SSAO> _ssaoPass;				///< SSAO processing.
	std::unique_ptr<AmbientQuad> _ambientScreen;	///< Ambient lighting contribution rendering.
	std::unique_ptr<DeferredLight> _lightRenderer;	///< The lights renderer.
	std::unique_ptr<ForwardLight> _fwdLightsGPU;	///< The lights forward renderer for transparent objects.
	
	const Program * _objectProgram;		 ///< Basic PBR program
	const Program * _parallaxProgram;	 ///< Parallax mapping PBR program
	const Program * _emissiveProgram;	 ///< Emissive program
	const Program * _transparentProgram; ///< Transparent PBR program

	const Program * _skyboxProgram; ///< Skybox program.
	const Program * _bgProgram;		///< Planar background program.
	const Program * _atmoProgram;   ///< Atmospheric scattering program.

	const Texture * _textureBrdf; ///< The BRDF lookup table.

	std::shared_ptr<Scene> _scene;	///< The scene to render
	std::unique_ptr<Culler>	_culler;	///< Objects culler.

	bool _applySSAO			 = true;  ///< Screen space ambient occlusion.
	ShadowMode  _shadowMode	 = ShadowMode::VARIANCE;  ///< Shadow mapping technique to use.
};
