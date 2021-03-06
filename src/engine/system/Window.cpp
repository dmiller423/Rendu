#include "Window.hpp"

#include "input/InputCallbacks.hpp"
#include "input/Input.hpp"
#include "graphics/GLUtilities.hpp"
#include "graphics/Framebuffer.hpp"
#include "resources/ResourcesManager.hpp"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_glfw.h>

#include <GLFW/glfw3.h>

Window::Window(const std::string & name, RenderingConfig & config, bool convertToSRGB, bool escapeQuit, bool hidden) :
	_config(config), _allowEscape(escapeQuit), _convertToSRGB(convertToSRGB) {
	// Initialize glfw, which will create and setup an OpenGL context.
	if(!glfwInit()) {
		Log::Error() << Log::OpenGL << "Could not start GLFW3" << std::endl;
		return;
	}
	const int openGLMajor = 4;
	const int openGLMinor = 0;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, openGLMajor);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, openGLMinor);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_FOCUSED, hidden ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);

	if(config.fullscreen) {
		const GLFWvidmode * mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
		glfwWindowHint(GLFW_RED_BITS, mode->redBits);
		glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
		glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
		glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
		/// \note We might want to impose the configured size here. This means the monitor could be set in a non-native mode.
		_window = glfwCreateWindow(mode->width, mode->height, name.c_str(), glfwGetPrimaryMonitor(), nullptr);
	} else {
		// Create a window with a given size. Width and height are defined in the configuration.
		_window = glfwCreateWindow(int(_config.initialWidth), int(_config.initialHeight), name.c_str(), nullptr, nullptr);
	}

	if(!_window) {
		Log::Error() << Log::OpenGL << "Could not open window with GLFW3" << std::endl;
		glfwTerminate();
		return;
	}

	if(_config.forceAspectRatio) {
		glfwSetWindowAspectRatio(_window, int(_config.initialWidth), int(_config.initialHeight));
	}
	// Bind the OpenGL context and the new window.
	glfwMakeContextCurrent(_window);

	if(gl3wInit()) {
		Log::Error() << Log::OpenGL << "Failed to initialize OpenGL" << std::endl;
		return;
	}
	if(!gl3wIsSupported(openGLMajor, openGLMinor)) {
		Log::Error() << Log::OpenGL << "OpenGL " << openGLMajor << "." << openGLMinor << " not supported\n"
					 << std::endl;
		return;
	}

	// Setup the GPU state.
	GLUtilities::setup();
	GLUtilities::setSRGBState(_convertToSRGB);

	// Setup callbacks for various interactions and inputs.
	glfwSetFramebufferSizeCallback(_window, resize_callback);		  // Resizing the window
	glfwSetKeyCallback(_window, key_callback);						  // Pressing a key
	glfwSetCharCallback(_window, char_callback);						  // Outputing a text char (for ImGui)
	glfwSetMouseButtonCallback(_window, mouse_button_callback);		  // Clicking the mouse buttons
	glfwSetCursorPosCallback(_window, cursor_pos_callback);			  // Moving the cursor
	glfwSetScrollCallback(_window, scroll_callback);					  // Scrolling
	glfwSetJoystickCallback(joystick_callback);						  // Joystick
	glfwSetWindowIconifyCallback(_window, iconify_callback);			  // Window minimization
	glfwSwapInterval(_config.vsync ? (_config.rate == 30 ? 2 : 1) : 0); // 60 FPS V-sync

	// We will need basic resources for ImGui.
	Resources::manager().addResources("../../../resources/common");
	setupImGui();

	// Check the window position and size (if we are on a screen smaller than the initial size).
	glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
	glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);

	// On HiDPI screens, we have to consider the internal resolution for all framebuffers size.
	int width, height;
	glfwGetFramebufferSize(_window, &width, &height);
	config.screenResolution = glm::vec2(width, height);

	// Compute point density by computing the ratio.
	const float screenDensity = float(width) / float(_config.windowFrame[2]);
	Input::manager().densityEvent(screenDensity);

	// Update the resolution.
	Input::manager().resizeEvent(width, height);
	Framebuffer::backbufferResized(width, height);
}

void Window::perform(Action action) {
	switch(action) {
		case Action::Quit:
			glfwSetWindowShouldClose(_window, GLFW_TRUE);
			break;
		case Action::Vsync: {
			if(_config.vsync) {
				glfwSwapInterval(0);
			} else {
				glfwSwapInterval(_config.rate == 30 ? 2 : 1);
			}
			_config.vsync = !_config.vsync;
			break;
		}
		case Action::Fullscreen: {
			// Are we currently fullscreen?
			const bool fullscreen = glfwGetWindowMonitor(_window) != nullptr;

			if(fullscreen) {
				// Restore the window position and size.
				glfwSetWindowMonitor(_window, nullptr, _config.windowFrame[0], _config.windowFrame[1], _config.windowFrame[2], _config.windowFrame[3], 0);
				// Check the window position and size (if we are on a screen smaller than the initial size).
				glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
				glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);
			} else {
				// Backup the window current frame.
				glfwGetWindowPos(_window, &_config.windowFrame[0], &_config.windowFrame[1]);
				glfwGetWindowSize(_window, &_config.windowFrame[2], &_config.windowFrame[3]);
				// Move to fullscreen on the primary monitor.
				GLFWmonitor * monitor	= glfwGetPrimaryMonitor();
				const GLFWvidmode * mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
			}
			_config.fullscreen = !fullscreen;
			// On some hardware, V-sync options can be lost.
			glfwSwapInterval(_config.vsync ? (_config.rate == 30 ? 2 : 1) : 0);

			// On HiDPI screens, we have to consider the internal resolution for all framebuffers size.
			int width, height;
			glfwGetFramebufferSize(_window, &width, &height);
			_config.screenResolution = glm::vec2(width, height);

			int wwidth, hheight;
			glfwGetWindowSize(_window, &wwidth, &hheight);
			// Compute point density by computing the ratio.
			const float screenDensity = float(width) / float(wwidth);
			Input::manager().densityEvent(screenDensity);

			// Update the resolution.
			Input::manager().resizeEvent(width, height);
		} break;
		default:
			break;
	}
}

bool Window::nextFrame() {
	if(_frameStarted){
		// Render the interface.
		ImGui::Render();
		// ImGui is not sRGB aware, we have to disable linear to
		// sRGB conversion when writing to the backbuffer.
		GLUtilities::setSRGBState(false);
		// Draw ImGui as-is...
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		// ...and restore.
		GLUtilities::setSRGBState(_convertToSRGB);
		
		//Display the result for the current rendering loop.
		glfwSwapBuffers(_window);
	}
	// Notify GPU for book-keeping.
	GLUtilities::nextFrame();

	// Update events (inputs,...).
	Input::manager().update();
	// Handle quitting.
	if(_allowEscape && Input::manager().pressed(Input::Key::Escape)) {
		perform(Action::Quit);
	}
	// Check if the backbuffer was resized.
	if(Input::manager().resized()){
		const uint w = uint(Input::manager().size()[0]);
		const uint h = uint(Input::manager().size()[1]);
		Framebuffer::backbufferResized(w, h);
	}
	// Start new GUI frame.
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	_frameStarted = true;
	return !glfwWindowShouldClose(_window);
}

Window::~Window() {
	// Make sure rendering is done.
	if(_frameStarted){
		ImGui::EndFrame();
	}
	GLUtilities::sync();
	// Clean the interface.
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	// Clean other resources
	Resources::manager().clean();
	// Close GL context and any other GLFW resources.
	glfwDestroyWindow(_window);
	glfwTerminate();
}

void Window::setupImGui() {
	ImGui::CreateContext();
	ImGuiIO & io = ImGui::GetIO();
	// Load font.
	const auto * fontData = Resources::manager().getData("Lato-Regular.ttf");
	if(fontData){
		ImFontConfig font = ImFontConfig();
		font.FontData = (void*)(fontData->data());
		font.FontDataSize = fontData->size();
		font.SizePixels = 16.0f;
		// Font data is managed by the resource manager.
		font.FontDataOwnedByAtlas = false;
		io.Fonts->AddFont(&font);
	}
	ImGui_ImplGlfw_InitForOpenGL(_window, false);
	ImGui_ImplOpenGL3_Init("#version 410");
	
	// Customize the style.
	ImGui::StyleColorsDark();
	ImGuiStyle & style = ImGui::GetStyle();
	// Colors.
	ImVec4 * colors						    = style.Colors;
	const ImVec4 bgColor					= ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	const ImVec4 buttonColor				= ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
	const ImVec4 buttonHoverColor			= ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
	const ImVec4 buttonActiveColor			= ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
	const ImVec4 textColor					= ImVec4(0.82f, 0.82f, 0.82f, 1.00f);

	colors[ImGuiCol_Text]                   = textColor;
	colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_CheckMark]              = textColor;

	colors[ImGuiCol_WindowBg]               = bgColor;
	colors[ImGuiCol_ChildBg]                = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_PopupBg]                = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
	colors[ImGuiCol_Border]                 = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
	colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_FrameBg]                = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
	colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
	colors[ImGuiCol_FrameBgActive]          = ImVec4(0.26f, 0.26f, 0.26f, 1.00f);
	colors[ImGuiCol_TitleBg]                = bgColor;
	colors[ImGuiCol_TitleBgActive]          = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_TitleBgCollapsed]       = bgColor;
	colors[ImGuiCol_MenuBarBg]              = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
	colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_ScrollbarGrab]          = buttonColor;
	colors[ImGuiCol_ScrollbarGrabHovered]   = buttonHoverColor;
	colors[ImGuiCol_ScrollbarGrabActive]    = buttonActiveColor;
	colors[ImGuiCol_SliderGrab]             = buttonColor;
	colors[ImGuiCol_SliderGrabActive]       = buttonActiveColor;
	colors[ImGuiCol_Button]                 = buttonColor;
	colors[ImGuiCol_ButtonHovered]          = buttonHoverColor;
	colors[ImGuiCol_ButtonActive]           = buttonActiveColor;
	colors[ImGuiCol_Header]                 = buttonColor;
	colors[ImGuiCol_HeaderHovered]          = buttonHoverColor;
	colors[ImGuiCol_HeaderActive]           = buttonActiveColor;
	colors[ImGuiCol_Separator]              = buttonColor;
	colors[ImGuiCol_SeparatorHovered]       = buttonHoverColor;
	colors[ImGuiCol_SeparatorActive]        = buttonActiveColor;
	colors[ImGuiCol_ResizeGrip]             = buttonColor;
	colors[ImGuiCol_ResizeGripHovered]      = buttonHoverColor;
	colors[ImGuiCol_ResizeGripActive]       = buttonActiveColor;
	colors[ImGuiCol_Tab]                    = buttonColor;
	colors[ImGuiCol_TabHovered]             = buttonHoverColor;
	colors[ImGuiCol_TabActive]              = ImVec4(0.41f, 0.41f, 0.41f, 1.00f);
	colors[ImGuiCol_TabUnfocused]           = ImVec4(0.16f, 0.16f, 0.16f, 0.97f);
	colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.43f, 0.43f, 0.43f, 1.00f);
	colors[ImGuiCol_PlotLines]              = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
	colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);
	colors[ImGuiCol_PlotHistogram]          = ImVec4(0.62f, 0.62f, 0.62f, 1.00f);
	colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
	colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.31f, 0.31f, 0.31f, 1.00f);
	colors[ImGuiCol_TableBorderLight]       = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
	colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	colors[ImGuiCol_TableRowBgAlt]          = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_TextSelectedBg]         = buttonColor;
	colors[ImGuiCol_DragDropTarget]         = textColor;
	colors[ImGuiCol_NavHighlight]           = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
	colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
	colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
	colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.50f, 0.50f, 0.50f, 0.35f);

	// Frames.
	style.FrameRounding		 = 5.0f;
	style.GrabRounding		 = 3.0f;
	style.WindowRounding	 = 5.0f;
	style.ScrollbarRounding  = 12.0f;
	style.ScrollbarSize		 = 12.0f;
	style.WindowTitleAlign.x = 0.5f;
	style.FramePadding.y	 = 4.0f;
	style.ItemSpacing.y		 = 3.0f;
}
