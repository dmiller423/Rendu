#include "IslandApp.hpp"

#include "resources/Library.hpp"

IslandApp::IslandApp(RenderingConfig & config) : CameraApp(config), _waves(8, BufferType::UNIFORM, DataUse::STATIC)
{
	_userCamera.projection(config.screenResolution[0] / config.screenResolution[1], 1.34f, 0.1f, 100.0f);
	_userCamera.pose(glm::vec3(-2.234801,3.446842,-6.892219), glm::vec3(-1.869996,2.552125,-5.859552), glm::vec3(0.210734,0.774429,0.596532));
	
	// Framebuffer to store the rendered atmosphere result before tonemapping and upscaling to the window size.
	const glm::vec2 renderRes = _config.renderingResolution();
	const std::vector<Descriptor> descriptors = {{Layout::RGB32F, Filter::LINEAR_NEAREST, Wrap::CLAMP}, {Layout::RGB32F, Filter::LINEAR_NEAREST, Wrap::CLAMP}};
	_sceneBuffer.reset(new Framebuffer(uint(renderRes[0]), uint(renderRes[1]), descriptors, true));
	_waterPos.reset(new Framebuffer(uint(renderRes[0]), uint(renderRes[1]), {descriptors[1]}, false));
	_waterEffectsHalf.reset(new Framebuffer(uint(renderRes[0])/2, uint(renderRes[1])/2, {descriptors[0]}, false));
	_waterEffectsBlur.reset(new Framebuffer(uint(renderRes[0])/2, uint(renderRes[1])/2, {descriptors[0]}, false));
	_environment.reset(new Framebuffer(TextureShape::Cube, 512, 512, 6, 1, {{Layout::RGB16F, Filter::LINEAR_NEAREST, Wrap::CLAMP}}, false));

	// Lookup table.
	_precomputedScattering = Resources::manager().getTexture("scattering-precomputed", {Layout::RGB32F, Filter::LINEAR_LINEAR, Wrap::CLAMP}, Storage::GPU);
	// Atmosphere screen quad.
	_skyProgram = Resources::manager().getProgram("atmosphere_island", "background_infinity", "atmosphere_island");
	_groundProgram = Resources::manager().getProgram("ground_island");
	_oceanProgram = Resources::manager().getProgram("ocean_island", "ocean_island", "ocean_island", "", "ocean_island", "ocean_island");
	_farOceanProgram = Resources::manager().getProgram("far_ocean_island", "far_ocean_island", "ocean_island");
	_waterCopy = Resources::manager().getProgram2D("water_copy");
	_underwaterProgram = Resources::manager().getProgram2D("ocean_underwater");
	// Final tonemapping screen quad.
	_tonemap = Resources::manager().getProgram2D("tonemap");

	// Sun direction.
	_lightDirection = glm::normalize(glm::vec3(0.437f, 0.482f, -0.896f));
	_skyMesh = Resources::manager().getMesh("plane", Storage::GPU);

	// Ground.
	_terrain.reset(new Terrain(1024, 4567));
	_materials = Resources::manager().getTexture("island_material_diff", {Layout::SRGB8, Filter::LINEAR_LINEAR, Wrap::REPEAT}, Storage::GPU);
	_materialNormals = Resources::manager().getTexture("island_material_nor", {Layout::RGB8, Filter::LINEAR_LINEAR, Wrap::REPEAT}, Storage::GPU);

	// Fbm noise used to disturb smooth transitions.
	_transitionNoise.width = _transitionNoise.height = 512;
	_transitionNoise.depth = _transitionNoise.levels = 1,
	_transitionNoise.shape = TextureShape::D2;
	_transitionNoise.images.emplace_back(_transitionNoise.width, _transitionNoise.height, 1);
	PerlinNoise generator;
	generator.generateLayers(_transitionNoise.images[0], 4, 0.5f, 2.0f, 0.01f);
	_transitionNoise.upload({Layout::R32F, Filter::LINEAR, Wrap::REPEAT}, false);

	// Ocean.
	_oceanMesh = Library::generateGrid(_gridOceanRes, 1.0f);
	_oceanMesh.upload();
	_farOceanMesh = Library::generateCylinder(64, 128.0f, 256.0f);
	_farOceanMesh.upload();
	_absorbScatterOcean = Resources::manager().getTexture("absorbscatterwater", {Layout::SRGB8, Filter::LINEAR, Wrap::CLAMP}, Storage::GPU);
	_caustics = Resources::manager().getTexture("caustics", {Layout::R8, Filter::LINEAR_LINEAR, Wrap::REPEAT}, Storage::GPU);
	_waveNormals = Resources::manager().getTexture("wave_normals", {Layout::RGB8, Filter::LINEAR_LINEAR, Wrap::REPEAT}, Storage::GPU);
	_foam = Resources::manager().getTexture("foam", {Layout::SRGB8_ALPHA8, Filter::LINEAR_LINEAR, Wrap::REPEAT}, Storage::GPU);
	_brdfLUT = Resources::manager().getTexture("brdf-precomputed", {Layout::RG32F, Filter::LINEAR_LINEAR, Wrap::CLAMP}, Storage::GPU);

	GLUtilities::setDepthState(true);

	checkGLError();
	// Tesselation options.
	const float pSize = 128.0f;
	_maxLevelX = std::log2(pSize);
	_maxLevelY = pSize;
	_distanceScale = 1.0f / (float(_sceneBuffer->width()) / 1920.0f) * 6.0f;

	generateWaves();

}

void IslandApp::generateWaves(){
	// Compute Gerstner waves parameters with some variance.
	// Generate a set of low and high frequency waves.
	const float baseALow = 0.025f;
	const float baseAHigh = 0.02f;
	const float angleVar = 0.5f;
	const float basewLow = 2.5f;
	const float basewHigh = 10.0f;

	for(int i = 0; i < 3; ++i){
		auto & wv = _waves[i];
		wv.AQwp[0] = baseALow + Random::Float(-0.01f, 0.01f);
		wv.AQwp[1] = 3.0f*Random::Float(0.1f, 0.5f);
		wv.AQwp[2] = basewLow + Random::Float(-1.5f, 1.5f);
		wv.AQwp[3] = Random::Float(0.2f, 1.5f);
		// Angle.
		wv.DiAngleActive[2] = (2.0f/3.0f) * (i + Random::Float(-angleVar, angleVar)) * glm::pi<float>();
		wv.DiAngleActive[0] = std::cos(wv.DiAngleActive[2]);
		wv.DiAngleActive[1] = std::sin(wv.DiAngleActive[2]);
		// Ensure Q normalization.
		wv.AQwp[1] /= (wv.AQwp[0] * wv.AQwp[2] * 8.0f);
	}
	for(int i = 3; i < 8; ++i){
		auto & wv = _waves[i];
		wv.AQwp[0] = baseAHigh + Random::Float(-0.01f, 0.01f);
		wv.AQwp[1] = 3.0f*Random::Float(0.6f, 1.0f);
		wv.AQwp[2] = basewHigh + Random::Float(-3.0f, 8.0f);
		wv.AQwp[3] = Random::Float(1.0f, 3.0f);
		// Angle.
		wv.DiAngleActive[2] = ((2.0f/5.0f) * (i + Random::Float(-angleVar, angleVar)) - 1.0f) * glm::pi<float>();
		wv.DiAngleActive[0] = std::cos(wv.DiAngleActive[2]);
		wv.DiAngleActive[1] = std::sin(wv.DiAngleActive[2]);
		// Ensure Q normalization.
		wv.AQwp[1] /= (wv.AQwp[0] * wv.AQwp[2] * 8.0f);
	}
	_waves.upload();
}

void IslandApp::draw() {

	const glm::mat4 camToWorld = glm::inverse(_userCamera.view());
	const glm::mat4 clipToCam  = glm::inverse(_userCamera.projection());
	const glm::mat4 camToWorldNoT = glm::mat4(glm::mat3(camToWorld));
	const glm::mat4 clipToWorld   = camToWorldNoT * clipToCam;
	const glm::mat4 mvp = _userCamera.projection() * _userCamera.view();
	const glm::vec3 camDir = _userCamera.direction();
	const glm::vec3 & camPos = _userCamera.position();
	const glm::vec2 invRenderSize = 1.0f / glm::vec2(_sceneBuffer->width(), _sceneBuffer->height());
	const float time = _stopTime ? 0.1f : float(timeElapsed());
	// If needed, update the skybox.
	if(_shouldUpdateSky){
		GLUtilities::setDepthState(false);
		GLUtilities::setBlendState(false);
		_environment->setViewport();

		_skyProgram->use();
		_skyProgram->uniform("viewPos", glm::vec3(0.0f));
		_skyProgram->uniform("lightDirection", _lightDirection);
		GLUtilities::bindTexture(_precomputedScattering, 0);

		for(uint lid = 0; lid < 6; ++lid){
			_environment->bind(lid);
			const glm::mat4 clipToWorldFace  = glm::inverse(Library::boxVPs[lid]);
			_skyProgram->uniform("clipToWorld", clipToWorldFace);
			GLUtilities::drawMesh(*_skyMesh);
		}
		_environment->unbind();

		_terrain->generateShadowMap(_lightDirection);
		_shouldUpdateSky = false;
	}

	_sceneBuffer->bind();
	_sceneBuffer->setViewport();
	_sceneBuffer->clear(glm::vec4(10000.0f), 1.0f);
	GLUtilities::setDepthState(true);
	GLUtilities::setBlendState(false);
	GLUtilities::clearColor({0.0f,0.0f,0.0f,1.0f});

	_primsGround.begin();
	// Render the ground.
	if(_showTerrain){

		const glm::vec3 frontPos = camPos + camDir;
		// Clamp based on the terrain heightmap dimensions in world space.
		const float extent = 0.25f * std::abs(float(_terrain->map().width) * _terrain->texelSize() - 0.5f*_terrain->meshSize());
		glm::vec3 frontPosClamped = glm::clamp(frontPos, -extent, extent);
		frontPosClamped[1] = 0.0f;

		// Compensate for grid translation.
		const Frustum camFrustum(mvp);

		_groundProgram->use();
		_groundProgram->uniform("mvp", mvp);
		_groundProgram->uniform("shift", frontPosClamped);
		_groundProgram->uniform("lightDirection", _lightDirection);
		_groundProgram->uniform("camDir", camDir);
		_groundProgram->uniform("camPos", camPos);
		_groundProgram->uniform("texelSize", _terrain->texelSize());
		_groundProgram->uniform("invMapSize", 1.0f/float(_terrain->map().width));
		_groundProgram->uniform("invGridSize", 1.0f/float(_terrain->gridSize()));

		GLUtilities::bindTexture(_terrain->map(), 0);
		GLUtilities::bindTexture(_transitionNoise, 1);
		GLUtilities::bindTexture(_materials, 2);
		GLUtilities::bindTexture(_materialNormals, 3);
		GLUtilities::bindTexture(_terrain->shadowMap(), 4);


		for(const Terrain::Cell & cell : _terrain->cells()){
			// Compute equivalent of vertex shader vertex transformation.
			const float levelSize = std::exp2(float(cell.level)) * _terrain->texelSize();
			glm::vec3 mini = _terrain->texelSize() * cell.mesh.bbox.minis + glm::round(frontPosClamped/levelSize) * levelSize;
			glm::vec3 maxi = _terrain->texelSize() * cell.mesh.bbox.maxis + glm::round(frontPosClamped/levelSize) * levelSize;
			mini[1] = -5.0f; maxi[1] = 5.0f;
			BoundingBox box(mini, maxi);
			if(!camFrustum.intersects(box)){
				continue;
			}
			_groundProgram->uniform("debugCol", false);
			GLUtilities::drawMesh(cell.mesh);

			// Debug view.
			if(_showWire){
				GLUtilities::setPolygonState(PolygonMode::LINE, Faces::ALL);
				GLUtilities::setDepthState(true, DepthEquation::LEQUAL, true);
				_groundProgram->uniform("debugCol", true);
				GLUtilities::drawMesh(cell.mesh);
				GLUtilities::setPolygonState(PolygonMode::FILL, Faces::ALL);
				GLUtilities::setDepthState(true, DepthEquation::LESS, true);
			}
		}
	}
	_primsGround.end();

	// Render the ocean.
	_primsOcean.begin();
	const bool isUnderwater = camPos.y < 0.00f;
	if(_showOcean){
		// Start by copying the visible terrain.
		_waterEffectsHalf->bind();
		GLUtilities::setDepthState(false);
		_waterEffectsHalf->setViewport();
		_waterCopy->use();
		GLUtilities::bindTexture(_sceneBuffer->texture(0), 0);
		GLUtilities::bindTexture(_sceneBuffer->texture(1), 1);
		GLUtilities::bindTexture(_caustics, 2);
		GLUtilities::bindTexture(_waveNormals, 3);
		_waterCopy->uniform("time", time);
		ScreenQuad::draw();
		GLUtilities::setDepthState(true);
		_waterEffectsHalf->unbind();

		_blur.process(_waterEffectsHalf->texture(0), *_waterEffectsBlur);

		// Blit full res position map.
		GLUtilities::blit(*_sceneBuffer->texture(1), *_waterPos, Filter::NEAREST);

		_sceneBuffer->bind();
		_sceneBuffer->setViewport();

		if(isUnderwater){
			GLUtilities::setCullState(true, Faces::FRONT);
		}
		_oceanProgram->use();
		_oceanProgram->uniform("mvp", mvp);
		_oceanProgram->uniform("shift", glm::round(camPos));
		_oceanProgram->uniform("maxLevelX", _maxLevelX);
		_oceanProgram->uniform("maxLevelY", _maxLevelY);
		_oceanProgram->uniform("distanceScale", _distanceScale);
		_oceanProgram->uniform("debugCol", false);
		_oceanProgram->uniform("camDir", camDir);
		_oceanProgram->uniform("camPos", camPos);
		_oceanProgram->uniform("distantProxy", false);
		_oceanProgram->uniform("time", time);
		_oceanProgram->uniform("invTargetSize", invRenderSize);
		_oceanProgram->uniform("invTexelSize", 1.0f/_terrain->texelSize());
		_oceanProgram->uniform("invMapSize", 1.0f/float(_terrain->map().width));

		GLUtilities::bindBuffer(_waves, 0);
		GLUtilities::bindTexture(_foam, 0);
		GLUtilities::bindTexture(_waterEffectsHalf->texture(0), 1);
		GLUtilities::bindTexture(_waterPos->texture(0), 2);
		GLUtilities::bindTexture(_waterEffectsBlur->texture(0), 3);
		GLUtilities::bindTexture(_absorbScatterOcean, 4);
		GLUtilities::bindTexture(_waveNormals, 5);
		GLUtilities::bindTexture(_environment->texture(), 6);
		GLUtilities::bindTexture(_brdfLUT, 7);
		GLUtilities::bindTexture(_terrain->shadowMap(), 8);
		GLUtilities::drawTesselatedMesh(_oceanMesh, 4);

		// Debug view.
		if(_showWire){
			GLUtilities::setPolygonState(PolygonMode::LINE, Faces::ALL);
			GLUtilities::setDepthState(true, DepthEquation::LEQUAL, true);
			_oceanProgram->uniform("debugCol", true);
			GLUtilities::drawTesselatedMesh(_oceanMesh, 4);
			GLUtilities::setPolygonState(PolygonMode::FILL, Faces::ALL);
			GLUtilities::setDepthState(true, DepthEquation::LESS, true);
		}

		if(isUnderwater){
			GLUtilities::setCullState(true, Faces::BACK);
			// We have to redo the low-res copy and blur, because we need the blurred surface to appear.
			// But we won't render the sky.
			_waterEffectsHalf->bind();
			GLUtilities::setDepthState(false);
			_waterEffectsHalf->setViewport();
			_waterCopy->use();
			GLUtilities::bindTexture(_sceneBuffer->texture(0), 0);
			GLUtilities::bindTexture(_sceneBuffer->texture(1), 1);
			GLUtilities::bindTexture(_caustics, 2);
			GLUtilities::bindTexture(_waveNormals, 3);
			_waterCopy->uniform("time", time);
			ScreenQuad::draw();
			GLUtilities::setDepthState(true);
			_waterEffectsHalf->unbind();

			_blur.process(_waterEffectsHalf->texture(0), *_waterEffectsBlur);

			// Blit full res position map.
			GLUtilities::blit(*_sceneBuffer->texture(1), *_waterPos, Filter::NEAREST);

			// Render full screen effect.
			_sceneBuffer->bind();
			_sceneBuffer->setViewport();
			GLUtilities::setDepthState(false);
			_underwaterProgram->use();
			_underwaterProgram->uniform("mvp", mvp);
			_underwaterProgram->uniform("camDir", camDir);
			_underwaterProgram->uniform("camPos", camPos);
			_underwaterProgram->uniform("time", time);
			_underwaterProgram->uniform("invTargetSize", invRenderSize);

			GLUtilities::bindBuffer(_waves, 0);
			GLUtilities::bindTexture(_foam, 0);
			GLUtilities::bindTexture(_waterEffectsHalf->texture(0), 1);
			GLUtilities::bindTexture(_waterPos->texture(0), 2);
			GLUtilities::bindTexture(_waterEffectsBlur->texture(0), 3);
			GLUtilities::bindTexture(_absorbScatterOcean, 4);
			GLUtilities::bindTexture(_waveNormals, 5);
			GLUtilities::bindTexture(_environment->texture(), 6);
			ScreenQuad::draw();
			GLUtilities::setDepthState(true);

		}

		// Far ocean, using a cylinder as support to cast rays intersecting the ocean plane.
		if(!isUnderwater){

			GLUtilities::setDepthState(true, DepthEquation::ALWAYS, true);
			_farOceanProgram->use();
			_farOceanProgram->uniform("mvp", mvp);
			_farOceanProgram->uniform("camPos", camPos);
			_farOceanProgram->uniform("debugCol", false);
			_farOceanProgram->uniform("time", time);
			_farOceanProgram->uniform("distantProxy", true);
			_farOceanProgram->uniform("waterGridHalf", float(_gridOceanRes-2)*0.5f);
			_farOceanProgram->uniform("groundGridHalf", _terrain->meshSize()*0.5f);
			_farOceanProgram->uniform("invTargetSize", invRenderSize);

			GLUtilities::bindBuffer(_waves, 0);
			GLUtilities::bindTexture(_foam, 0);
			GLUtilities::bindTexture(_waterEffectsHalf->texture(0), 1);
			GLUtilities::bindTexture(_waterPos->texture(0), 2);
			GLUtilities::bindTexture(_waterEffectsBlur->texture(0), 3);
			GLUtilities::bindTexture(_absorbScatterOcean, 4);
			GLUtilities::bindTexture(_waveNormals, 5);
			GLUtilities::bindTexture(_environment->texture(), 6);
			GLUtilities::bindTexture(_brdfLUT, 7);
			GLUtilities::drawMesh(_farOceanMesh);

			// Debug view.
			if(_showWire){
				GLUtilities::setPolygonState(PolygonMode::LINE, Faces::ALL);
				GLUtilities::setDepthState(true, DepthEquation::LEQUAL, true);
				_farOceanProgram->uniform("debugCol", true);
				GLUtilities::drawMesh(_farOceanMesh);
				GLUtilities::setPolygonState(PolygonMode::FILL, Faces::ALL);
				GLUtilities::setDepthState(true, DepthEquation::LESS, true);
			}
			GLUtilities::setDepthState(true, DepthEquation::LESS, true);
		}

	}
	_primsOcean.end();

	// Render the sky.
	if(_showSky && !(_showOcean && isUnderwater)){
		GLUtilities::setDepthState(true, DepthEquation::LEQUAL, false);

		_skyProgram->use();
		_skyProgram->uniform("clipToWorld", clipToWorld);
		_skyProgram->uniform("viewPos", camPos);
		_skyProgram->uniform("lightDirection", _lightDirection);
		GLUtilities::bindTexture(_precomputedScattering, 0);
		GLUtilities::drawMesh(*_skyMesh);
	}
	_sceneBuffer->unbind();

	GLUtilities::setDepthState(false, DepthEquation::LESS, true);
	// Tonemapping and final screen.
	GLUtilities::setViewport(0, 0, int(_config.screenResolution[0]), int(_config.screenResolution[1]));
	Framebuffer::backbuffer()->bind(Framebuffer::Mode::SRGB);
	_tonemap->use();
	ScreenQuad::draw(_sceneBuffer->texture());
	Framebuffer::backbuffer()->unbind();
}

void IslandApp::update() {
	CameraApp::update();

	_primsGround.value();
	if(ImGui::Begin("Island")){
		ImGui::Text("%.1f ms, %.1f fps", frameTime() * 1000.0f, frameRate());
		ImGui::Text("Rendering res.: %ux%u", _sceneBuffer->width(), _sceneBuffer->height());
		ImGui::Text("Ground: %llu primitives, ocean: %llu primitives ", _primsGround.value(), _primsOcean.value());
		if(ImGui::DragFloat3("Light dir", &_lightDirection[0], 0.001f, -1.0f, 1.0f)) {
			_lightDirection = glm::normalize(_lightDirection);
			_shouldUpdateSky = true;
		}

		ImGui::Checkbox("Terrain##showcheck", &_showTerrain);
		ImGui::SameLine();
		ImGui::Checkbox("Ocean##showcheck", &_showOcean);
		ImGui::SameLine();
		ImGui::Checkbox("Sky##showcheck", &_showSky);
		ImGui::Checkbox("Show wire", &_showWire); ImGui::SameLine();
		ImGui::Checkbox("Stop time", &_stopTime);

		if(ImGui::CollapsingHeader("Tessellation")){
			ImGui::DragFloat("maxLevelX", &_maxLevelX);
			ImGui::DragFloat("maxLevelY", &_maxLevelY);
			ImGui::DragFloat("distanceScale", &_distanceScale);

		}

		if(ImGui::CollapsingHeader("Terrain")){
			_terrain->interface();
		}

		if(ImGui::CollapsingHeader("Ocean")){
			bool dirtyWaves = false;
			for(int i = 0; i < 8; ++i){
				auto & wave = _waves[i];
				const std::string name = "Wave " + std::to_string(i);
				if(ImGui::TreeNode(name.c_str())){
					bool active = wave.DiAngleActive[3] > 0.001f;
					if(ImGui::Checkbox("Enabled", &active)){
						wave.DiAngleActive[3] = active ? 1.0f : 0.0f;
						dirtyWaves = true;
					}
					if(active){
						dirtyWaves = ImGui::SliderFloat("Ai", &wave.AQwp[0], 0.0f, 1.0f) || dirtyWaves;
						dirtyWaves = ImGui::SliderFloat("Qi", &wave.AQwp[1], 0.0f, 1.0f) || dirtyWaves;
						dirtyWaves = ImGui::SliderFloat("wi", &wave.AQwp[2], 0.0f, 1.0f) || dirtyWaves;
						dirtyWaves = ImGui::SliderFloat("phi", &wave.AQwp[3], 0.0f, glm::pi<float>()) || dirtyWaves;
						if(ImGui::SliderFloat("Angle", &wave.DiAngleActive[2], 0.0f, glm::two_pi<float>())){
							dirtyWaves = true;
							wave.DiAngleActive[0] = std::cos(wave.DiAngleActive[2]);
							wave.DiAngleActive[1] = std::sin(wave.DiAngleActive[2]);
						}
					}
					ImGui::TreePop();
				}
				if(i == 2){
					ImGui::Separator();
				}
			}
			if(dirtyWaves){
				_waves.upload();
			}

		}

		if(ImGui::CollapsingHeader("Camera")){
			_userCamera.interface();
		}
	}
	ImGui::End();
}

void IslandApp::resize() {
	_sceneBuffer->resize(_config.renderingResolution());
	_waterPos->resize(_config.renderingResolution());
	_waterEffectsHalf->resize(_config.renderingResolution()/2.0f);
	_waterEffectsBlur->resize(_config.renderingResolution()/2.0f);
}

void IslandApp::clean() {
	_sceneBuffer->clean();
	_waterEffectsHalf->clean();
	_waterEffectsBlur->clean();
	_waterPos->clean();
	_environment->clean();
	_blur.clean();
	_terrain->clean();
}
