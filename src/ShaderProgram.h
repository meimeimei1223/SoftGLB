#ifndef SHADER_PROGRAM_H
#define SHADER_PROGRAM_H

#include <string>
#include <map>
#define GLEW_STATIC
#include "GL/glew.h"
#include "glm/glm.hpp"

using std::string;

class ShaderProgram {
public:
    ShaderProgram();
    ~ShaderProgram();

    enum ShaderType { VERTEX, FRAGMENT, PROGRAM };

    bool loadShaders(const char* vsFilename, const char* fsFilename);
    void use();
    GLuint getProgram() const;
    GLint  getUniformLocation(const GLchar* name);

    void setUniform(const GLchar* name, const glm::vec2& v);
    void setUniform(const GLchar* name, const glm::vec3& v);
    void setUniform(const GLchar* name, const glm::vec4& v);
    void setUniform(const GLchar* name, const glm::mat4& m);
    void setUniform(const GLchar* name, int  v);
    void setUniform(const GLchar* name, bool v);

private:
    GLuint mHandle;
    std::map<string, GLint> mUniformLocations;

    string fileToString(const string& filename);
    void   checkCompileErrors(GLuint shader, ShaderType type);
};

#endif
