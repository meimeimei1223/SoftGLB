#include "ShaderProgram.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

ShaderProgram::ShaderProgram() : mHandle(0) {}

ShaderProgram::~ShaderProgram() {
    glDeleteProgram(mHandle);
}

bool ShaderProgram::loadShaders(const char* vsFilename, const char* fsFilename) {
    string vsString = fileToString(vsFilename);
    string fsString = fileToString(fsFilename);
    const GLchar* vsPtr = vsString.c_str();
    const GLchar* fsPtr = fsString.c_str();

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vs, 1, &vsPtr, NULL);
    glShaderSource(fs, 1, &fsPtr, NULL);

    glCompileShader(vs); checkCompileErrors(vs, VERTEX);
    glCompileShader(fs); checkCompileErrors(fs, FRAGMENT);

    mHandle = glCreateProgram();
    if (mHandle == 0) {
        std::cerr << "Unable to create shader program!" << std::endl;
        return false;
    }

    glAttachShader(mHandle, vs);
    glAttachShader(mHandle, fs);
    glLinkProgram(mHandle);
    checkCompileErrors(mHandle, PROGRAM);

    glDeleteShader(vs);
    glDeleteShader(fs);

    mUniformLocations.clear();
    return true;
}

void ShaderProgram::use() {
    if (mHandle > 0) glUseProgram(mHandle);
}

GLuint ShaderProgram::getProgram() const {
    return mHandle;
}

GLint ShaderProgram::getUniformLocation(const GLchar* name) {
    auto it = mUniformLocations.find(name);
    if (it == mUniformLocations.end())
        mUniformLocations[name] = glGetUniformLocation(mHandle, name);
    return mUniformLocations[name];
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec2& v) {
    glUniform2f(getUniformLocation(name), v.x, v.y);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec3& v) {
    glUniform3f(getUniformLocation(name), v.x, v.y, v.z);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec4& v) {
    glUniform4f(getUniformLocation(name), v.x, v.y, v.z, v.w);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::mat4& m) {
    glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(m));
}

void ShaderProgram::setUniform(const GLchar* name, int v) {
    glUniform1i(getUniformLocation(name), v);
}

void ShaderProgram::setUniform(const GLchar* name, bool v) {
    glUniform1i(getUniformLocation(name), static_cast<int>(v));
}

string ShaderProgram::fileToString(const string& filename) {
    std::stringstream ss;
    std::ifstream file;
    try {
        file.open(filename, std::ios::in);
        if (!file.fail()) ss << file.rdbuf();
        file.close();
    } catch (std::exception& ex) {
        std::cerr << "Error reading shader: " << filename << std::endl;
    }
    return ss.str();
}

void ShaderProgram::checkCompileErrors(GLuint shader, ShaderType type) {
    int status = 0;
    if (type == PROGRAM) {
        glGetProgramiv(shader, GL_LINK_STATUS, &status);
        if (status == GL_FALSE) {
            GLint length = 0;
            glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &length);
            string log(length, ' ');
            glGetProgramInfoLog(shader, length, &length, &log[0]);
            std::cerr << "Shader link error: " << log << std::endl;
        }
    } else {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
        if (status == GL_FALSE) {
            GLint length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            string log(length, ' ');
            glGetShaderInfoLog(shader, length, &length, &log[0]);
            std::cerr << "Shader compile error: " << log << std::endl;
        }
    }
}
