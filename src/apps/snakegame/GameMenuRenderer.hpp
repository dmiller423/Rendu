#pragma once

#include "renderers/Renderer.hpp"
#include "graphics/Program.hpp"
#include "GameMenu.hpp"

/**
 \brief Renders a game menu.
 \ingroup SnakeGame
 */
class GameMenuRenderer: public Renderer {
public:
	
	/** Constructor.
	 \param config the configuration to apply when setting up
	 */
	GameMenuRenderer(RenderingConfig & config);
	
	/** Draw the menu
	 \param menu the menu to draw
	 */
	void draw(const GameMenu & menu);
	
	/** Empty draw call. */
	void draw(){};
	
	/** Perform once-per-frame update (buttons, GUI,...) */
	void update();
	
	/** Empty physics simulation update.
	 \param fullTime the time elapsed since the beginning of the render loop
	 \param frameTime the duration of the last frame
	 */
	void physics(double fullTime, double frameTime);
	
	/** Handle a window resize event.
	 \param width the new width
	 \param height the new height
	 */
	void resize(unsigned int width, unsigned int height);
	
	/** Clean internal resources. */
	void clean();
	
	/** Return the absolute unit size of the button mesh.
	 \return the dimensions of the button mesh
	 */
	glm::vec2 getButtonSize();
	
private:
	
	const Program * _backgroundProgram; //< Background images rendering.
	const Program * _buttonProgram; //< Buttons rendering.
	const Program * _imageProgram; //< Fixed images rendering.
	const Program * _fontProgram; //< Labels font rendering.
	const Mesh * _button; //< Button main mesh (with border).
	const Mesh * _buttonIn; //< Button interior mesh.
	const Mesh * _toggle; //< Toggle main mesh (with border).
	const Mesh * _toggleIn; //< Toggle interior mesh.
	const Mesh * _quad; //< Quad mesh for images.

};


