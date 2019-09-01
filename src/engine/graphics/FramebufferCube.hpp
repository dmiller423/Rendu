#pragma once

#include "graphics/Framebuffer.hpp"
#include "graphics/GPUObjects.hpp"
#include "Common.hpp"

/**
 \brief Represent a cubemap rendering target, of any size, format and type, backed by an OpenGL framebuffer composed of six layers.
 \see GPU::Vert::Object_layer, GPU::Geom::Object_layer
 \ingroup Graphics
 */
class FramebufferCube {

public:
	
	/** \brief How the cubemap framebuffer should be used. This determines the shape of the depth buffer. */
	enum class CubeMode {
		COMBINED, ///< Render to the 6 cube faces at the same time.
		SLICED ///< Render to each layer separately.
	};

	/** Setup the framebuffer (attachments, renderbuffer, depth buffer, textures IDs,...)
	 \param side the width and height of each face of the framebuffer
	 \param descriptor contains the precise format and filtering to use
	 \param mode will the framebuffer be used for layered rendering or for per-face rendering
	 \param depthBuffer should the framebuffer contain a depth buffer to properly handle 3D geometry
	 */
	FramebufferCube(unsigned int side, const Descriptor & descriptor, CubeMode mode, bool depthBuffer);

	/**
	 Bind the framebuffer as a whole.
	 */
	void bind() const;
	
	/**
	 Bind a specific layer of the framebuffer.
	 \param slice the layer index
	 */
	void bind(size_t slice) const;

	/**
	 Set the viewport to the size of the framebuffer.
	 */
	void setViewport() const;

	/**
	 Unbind the framebuffer.
	 \note Technically bind the window backbuffer.
	 */
	void unbind() const;

	/**
	 Resize the framebuffer to new dimensions.
	 \param side the new width and height for each face
	 */
	void resize(unsigned int side);

	/** Clean internal resources.
	 */
	void clean();

	/**
	 Query the cubemap texture backing the framebuffer.
	 \return the texture
	 */
	const Texture * textureId() const { return &_idColor; }

	/**
	 Query the framebuffer side size.
	 \return the width/height of each face
	 */
	unsigned int side() const { return _side; }

	/** Copy assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	FramebufferCube & operator=(const FramebufferCube &) = delete;
	
	/** Copy constructor (disabled). */
	FramebufferCube(const FramebufferCube &) = delete;
	
	/** Move assignment operator (disabled).
	 \return a reference to the object assigned to
	 */
	FramebufferCube & operator=(FramebufferCube &&) = delete;
	
	/** Move constructor (disabled). */
	FramebufferCube(FramebufferCube &&) = delete;
	
private:
	unsigned int _side; ///< The size of each cubemap face sides.

	GLuint _id;		  ///< The framebuffer ID.
	Texture _idColor; ///< The color texture.
	Texture _idDepth; ///< The depth buffer.

	bool _useDepth; ///< Denotes if the framebuffer is backed by a depth buffer.
};
