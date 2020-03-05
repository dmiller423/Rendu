#include "graphics/Program.hpp"
#include "graphics/GLUtilities.hpp"
#include "resources/ResourcesManager.hpp"


Program::Program(const std::string & vertexName, const std::string & fragmentName, const std::string & geometryName) :
	_vertexName(vertexName), _fragmentName(fragmentName), _geometryName(geometryName) {

	std::map<std::string, int> bindings;
	const std::string vertexContent   = Resources::manager().getStringWithIncludes(_vertexName + ".vert");
	const std::string fragmentContent = Resources::manager().getStringWithIncludes(_fragmentName + ".frag");
	const std::string geometryContent = _geometryName.empty() ? "" : Resources::manager().getStringWithIncludes(_geometryName + ".geom");
	const std::string debugName		  = "(" + _vertexName + ", " + (_geometryName.empty() ? "" : (_geometryName + ", ")) + _fragmentName + ")";

	_id = GLUtilities::createProgram(vertexContent, fragmentContent, geometryContent, bindings, debugName);
	_uniforms.clear();

	// Get the number of active uniforms and their maximum length.
	GLint count = 0;
	GLint size  = 0;
	glGetProgramiv(_id, GL_ACTIVE_UNIFORMS, &count);
	glGetProgramiv(_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &size);

	glUseProgram(_id);
	for(GLuint i = 0; i < GLuint(count); ++i) {
		// Get infos (name, name length, type,...) of each uniform.
		std::vector<GLchar> uname(size);
		GLenum utype;
		GLint usize		= 0;
		GLsizei ulength = 0;
		glGetActiveUniform(_id, i, size, &ulength, &usize, &utype, &uname[0]);
		const std::string name(&uname[0]);
		// Skip empty or default uniforms (starting with 'gl_').
		if(usize == 0 || name.empty() || (name.size() > 3 && name.substr(0, 3) == "gl_")) {
			continue;
		}
		// Register uniform using its name.
		// /!\ the uniform location can be different from the uniform ID.
		_uniforms[name] = glGetUniformLocation(_id, name.c_str());
		// If the size of the uniform is > 1, we have an array.
		if(usize > 1) {
			// Extract the array name from the 'name[0]' string.
			const std::string subname = name.substr(0, name.find_first_of('['));
			// Get the location of the other array elements.
			for(GLsizei j = 1; j < usize; ++j) {
				const std::string vname = subname + "[" + std::to_string(j) + "]";
				_uniforms[vname]		= glGetUniformLocation(_id, vname.c_str());
			}
		}
	}
	// Register texture slots.
	for(auto & texture : bindings) {
		glUniform1i(_uniforms[texture.first], texture.second);
		checkGLErrorInfos("Unused texture \"" + texture.first + "\" in program " + debugName + ".");
	}

	glUseProgram(0);
	checkGLError();
}

void Program::cacheUniformArray(const std::string & name, const std::vector<glm::vec3> & vals) {
	// Store the vec3s elements in a cache, to avoid re-setting them at each frame.
	glUseProgram(_id);
	for(size_t i = 0; i < vals.size(); ++i) {
		const std::string elementName = name + "[" + std::to_string(i) + "]";
		_vec3s[elementName]			  = vals[i];
		glUniform3fv(_uniforms[elementName], 1, &(_vec3s[elementName][0]));
	}
	glUseProgram(0);
	checkGLError();
}

void Program::reload() {
	std::map<std::string, int> bindings;
	const std::string vertexContent   = Resources::manager().getStringWithIncludes(_vertexName + ".vert");
	const std::string fragmentContent = Resources::manager().getStringWithIncludes(_fragmentName + ".frag");
	const std::string geometryContent = _geometryName.empty() ? "" : Resources::manager().getStringWithIncludes(_geometryName + ".geom");
	const std::string debugName		  = "(" + _vertexName + ", " + (_geometryName.empty() ? "" : (_geometryName + ", ")) + _fragmentName + ")";
	_id								  = GLUtilities::createProgram(vertexContent, fragmentContent, geometryContent, bindings, debugName);

	// For each stored uniform, update its location, and update textures slots and cached values.
	glUseProgram(_id);
	for(auto & uni : _uniforms) {
		_uniforms[uni.first] = glGetUniformLocation(_id, uni.first.c_str());
		if(_vec3s.count(uni.first) > 0) {
			glUniform3fv(_uniforms[uni.first], 1, &(_vec3s[uni.first][0]));
		}
	}
	// Register texture slots.
	for(auto & texture : bindings) {
		glUniform1i(_uniforms[texture.first], texture.second);
		checkGLErrorInfos("Unused texture \"" + texture.first + "\" in program " + debugName + ".");
	}
	glUseProgram(0);
}

void Program::validate() const {
	glValidateProgram(_id);
	int status = -2;
	glGetProgramiv(_id, GL_VALIDATE_STATUS, &status);
	Log::Error() << Log::OpenGL << "Program with shaders: " << _vertexName << ", " << _fragmentName << " is " << (status == GL_TRUE ? "" : "not ") << "validated." << std::endl;
	int infoLogLength = 0;
	glGetProgramiv(_id, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength <= 0) {
		Log::Error() << Log::OpenGL << "No log for validation." << std::endl;
		return;
	}
	std::vector<char> infoLog(infoLogLength);
	glGetProgramInfoLog(_id, infoLogLength, nullptr, &infoLog[0]);
	Log::Error() << Log::OpenGL << "Log for validation: " << &infoLog[0] << std::endl;
}

void Program::saveBinary(const std::string & outputPath) const {
	int count = 0;
	glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &count);
	if(count <= 0) {
		Log::Error() << Log::OpenGL << "GL driver does not support program binary export." << std::endl;
		return;
	}
	int length = 0;
	glGetProgramiv(_id, GL_PROGRAM_BINARY_LENGTH, &length);
	if(length <= 0) {
		Log::Error() << Log::OpenGL << "No binary for program using shaders (" << _vertexName << "," << _fragmentName << ")." << std::endl;
		return;
	}
	GLenum format;
	std::vector<char> binary(length);
	glGetProgramBinary(_id, length, nullptr, &format, &binary[0]);

	Resources::saveRawDataToExternalFile(outputPath + "_(" + _vertexName + "," + _fragmentName + ")_" + std::to_string(uint(format)) + ".bin", &binary[0], binary.size());
}

void Program::use() const {
	glUseProgram(_id);
}

void Program::clean() const {
	glDeleteProgram(_id);
}

void Program::uniform(const std::string & name, bool t) const {
	if(_uniforms.count(name) > 0) {
		glUniform1i(_uniforms.at(name), int(t));
	}
}

void Program::uniform(const std::string & name, int t) const {
	if(_uniforms.count(name) > 0) {
		glUniform1i(_uniforms.at(name), t);
	}
}

void Program::uniform(const std::string & name, float t) const {
	if(_uniforms.count(name) > 0) {
		glUniform1f(_uniforms.at(name), t);
	}
}

void Program::uniform(const std::string & name, size_t count, const float * t) const {
	if(_uniforms.count(name) > 0) {
		glUniform1fv(_uniforms.at(name), GLsizei(count), t);
	}
}

void Program::uniform(const std::string & name, const glm::vec2 & t) const {
	if(_uniforms.count(name) > 0) {
		glUniform2fv(_uniforms.at(name), 1, &t[0]);
	}
}

void Program::uniform(const std::string & name, const glm::vec3 & t) const {
	if(_uniforms.count(name) > 0) {
		glUniform3fv(_uniforms.at(name), 1, &t[0]);
	}
}

void Program::uniform(const std::string & name, const glm::vec4 & t) const {
	if(_uniforms.count(name) > 0) {
		glUniform4fv(_uniforms.at(name), 1, &t[0]);
	}
}

void Program::uniform(const std::string & name, const glm::mat3 & t) const {
	if(_uniforms.count(name) > 0) {
		glUniformMatrix3fv(_uniforms.at(name), 1, GL_FALSE, &t[0][0]);
	}
}

void Program::uniform(const std::string & name, const glm::mat4 & t) const {
	if(_uniforms.count(name) > 0) {
		glUniformMatrix4fv(_uniforms.at(name), 1, GL_FALSE, &t[0][0]);
	}
}
